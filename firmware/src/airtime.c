#include "airtime.h"
#include "config.h"
#include "hal.h"

#define BKP_MAGIC_IDX  0u
#define BKP_START_IDX  1u
#define BKP_USED_IDX   2u
#define BKP_CHECK_IDX  3u
#define BKP_MAGIC      0xA17E0001u

#define LORAWAN_OVERHEAD 13u   /* MHDR 1 + FHDR 7 + FPort 1 + MIC 4 */

static uint32_t check_word(uint32_t start, uint32_t used)
{
    return ~(start ^ (used * 2654435761u) ^ 0x5A5A5A5Au);
}

uint32_t airtime_us(uint8_t sf, uint8_t app_bytes)
{
    if (sf < 7 || sf > 12) return UINT32_MAX;

    const uint32_t t_sym_us = (1u << sf) * 8u;           /* 2^SF / 125 kHz */
    const int32_t  ldro     = (sf >= 11) ? 1 : 0;
    const int32_t  pl       = (int32_t)app_bytes + (int32_t)LORAWAN_OVERHEAD;

    /* preamble (8 + 4.25 symbols) = 49/4 symbols */
    const uint32_t t_preamble_us = (49u * t_sym_us) / 4u;

    const int32_t num = 8 * pl - 4 * (int32_t)sf + 28 + 16 /* CRC */;
    const int32_t den = 4 * ((int32_t)sf - 2 * ldro);
    int32_t n_payload = 8;
    if (num > 0) n_payload += ((num + den - 1) / den) * 5;  /* CR 4/5 */

    return t_preamble_us + (uint32_t)n_payload * t_sym_us;
}

uint32_t airtime_ms(uint8_t sf, uint8_t app_bytes)
{
    uint32_t us = airtime_us(sf, app_bytes);
    if (us == UINT32_MAX) return UINT32_MAX;
    return (us + 999u) / 1000u;       /* round up: under-counting is the unsafe direction */
}

void airtime_persist(const airtime_budget_t *b)
{
    hal_backup_write(BKP_START_IDX, b->window_start_s);
    hal_backup_write(BKP_USED_IDX, b->used_ms);
    hal_backup_write(BKP_CHECK_IDX, check_word(b->window_start_s, b->used_ms));
    hal_backup_write(BKP_MAGIC_IDX, BKP_MAGIC);
}

void airtime_init(airtime_budget_t *b, uint32_t limit_ms, uint32_t now_s)
{
    b->limit_ms = limit_ms;

    const uint32_t start = hal_backup_read(BKP_START_IDX);
    const uint32_t used  = hal_backup_read(BKP_USED_IDX);
    const bool valid = hal_backup_read(BKP_MAGIC_IDX) == BKP_MAGIC &&
                       hal_backup_read(BKP_CHECK_IDX) == check_word(start, used);

    if (valid && now_s >= start && now_s - start < SECONDS_PER_DAY) {
        /* Reboot inside a live window: keep counting (§15.2). */
        b->window_start_s = start;
        b->used_ms = used;
    } else if (valid && now_s < start) {
        /* Clock went backwards: keep the spend, restart the window from now.
         * Conservative - never forgets airtime already used. */
        b->window_start_s = now_s;
        b->used_ms = used;
    } else {
        b->window_start_s = now_s;
        b->used_ms = 0;
    }
    airtime_persist(b);
}

static void roll_window(airtime_budget_t *b, uint32_t now_s)
{
    if (now_s < b->window_start_s) {
        b->window_start_s = now_s;
        airtime_persist(b);
    } else if (now_s - b->window_start_s >= SECONDS_PER_DAY) {
        b->window_start_s = now_s;
        b->used_ms = 0;
        airtime_persist(b);
    }
}

bool airtime_may_transmit(airtime_budget_t *b, uint32_t now_s, uint8_t sf, uint8_t bytes)
{
    roll_window(b, now_s);
    const uint32_t toa = airtime_ms(sf, bytes);
    if (toa == UINT32_MAX) return false;
    return b->used_ms + toa <= b->limit_ms;
}

void airtime_record(airtime_budget_t *b, uint32_t now_s, uint8_t sf, uint8_t bytes)
{
    roll_window(b, now_s);
    b->used_ms += airtime_ms(sf, bytes);
    airtime_persist(b);
}

uint32_t airtime_seconds_until_reset(const airtime_budget_t *b, uint32_t now_s)
{
    if (now_s < b->window_start_s) return SECONDS_PER_DAY;
    const uint32_t elapsed = now_s - b->window_start_s;
    return elapsed >= SECONDS_PER_DAY ? 0u : SECONDS_PER_DAY - elapsed;
}

uint32_t airtime_next_interval_s(airtime_budget_t *b, uint32_t now_s, uint8_t sf, uint8_t bytes)
{
    roll_window(b, now_s);
    const uint32_t toa          = airtime_ms(sf, bytes);
    const uint32_t remaining_ms = b->used_ms >= b->limit_ms ? 0u : b->limit_ms - b->used_ms;
    const uint32_t seconds_left = airtime_seconds_until_reset(b, now_s);

    if (remaining_ms < toa) {
        /* Budget spent: sleep to the reset, but keep waking to read the
         * sensor and refresh the display at the maximum interval. */
        uint32_t s = seconds_left ? seconds_left : 1u;
        return s < MAX_INTERVAL_S ? s : MAX_INTERVAL_S;
    }

    const uint32_t affordable = remaining_ms / toa;
    uint32_t interval = seconds_left / affordable;
    if (interval < MIN_INTERVAL_S) interval = MIN_INTERVAL_S;
    if (interval > MAX_INTERVAL_S) interval = MAX_INTERVAL_S;
    return interval;
}
