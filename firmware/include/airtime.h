#ifndef AIRTIME_H
#define AIRTIME_H
/* Own fair-use accounting (design doc §3.3, §15.2). The LoRaWAN stack
 * enforces the regulatory duty cycle; nothing enforces the operator's
 * 30 s/day except this module. */
#include <stdbool.h>
#include <stdint.h>

#define SECONDS_PER_DAY 86400u

typedef struct {
    uint32_t window_start_s;   /* RTC seconds at start of the 24 h window */
    uint32_t used_ms;          /* airtime consumed in this window */
    uint32_t limit_ms;         /* e.g. 30000 for TTN */
} airtime_budget_t;

/* Time on air for a LoRaWAN uplink carrying `app_bytes` of application
 * payload at 125 kHz, CR 4/5, 8-symbol preamble, explicit header, CRC on.
 * Low-data-rate optimisation applied at SF11/SF12. Rounded up to whole ms. */
uint32_t airtime_us(uint8_t sf, uint8_t app_bytes);
uint32_t airtime_ms(uint8_t sf, uint8_t app_bytes);

/* Restores the budget from backup registers if valid, else starts fresh. */
void airtime_init(airtime_budget_t *b, uint32_t limit_ms, uint32_t now_s);

bool airtime_may_transmit(airtime_budget_t *b, uint32_t now_s, uint8_t sf, uint8_t bytes);
void airtime_record(airtime_budget_t *b, uint32_t now_s, uint8_t sf, uint8_t bytes);

/* Seconds until the current window resets. */
uint32_t airtime_seconds_until_reset(const airtime_budget_t *b, uint32_t now_s);

/* Uplink interval derived from what is left of the budget (§3.3). */
uint32_t airtime_next_interval_s(airtime_budget_t *b, uint32_t now_s, uint8_t sf, uint8_t bytes);

/* Persist explicitly (airtime_record already does). */
void airtime_persist(const airtime_budget_t *b);

#endif
