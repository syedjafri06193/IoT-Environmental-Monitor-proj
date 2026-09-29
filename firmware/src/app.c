#include "app.h"
#include <string.h>
#include "config.h"
#include "hal.h"
#include "lorawan.h"
#include "lorawan_port.h"
#include "payload.h"
#include "sensor.h"
#include "watchdog.h"

#define MAX_CONSECUTIVE_SEND_FAILS 5u
#define UNJOINED_MAX_SLEEP_S       MAX_INTERVAL_S

static airtime_budget_t budget;
static app_stats_t      stats;
static uint32_t         consecutive_send_fails;

const app_stats_t      *app_stats(void)  { return &stats; }
const airtime_budget_t *app_budget(void) { return &budget; }

void app_init(void)
{
    const uint32_t now = hal_rtc_seconds();
    memset(&stats, 0, sizeof stats);
    consecutive_send_fails = 0;
    /* Restores the fair-use window across resets (§15.2). */
    airtime_init(&budget, AIRTIME_DAILY_LIMIT_MS, now);
    sensor_init();
    display_init();
    lorawan_init(now);
}

static void read_battery(reading_t *r)
{
    /* §5.4: under-load voltage from the previous transmit reveals rising
     * internal resistance long before the resting voltage moves. */
    const uint16_t loaded = lw_port_last_tx_battery_mv();
    r->battery_loaded = loaded != 0;
    r->battery_mv = loaded ? loaded : hal_battery_mv();
}

uint32_t app_step(void)
{
    watchdog_refresh();
    const uint32_t now = hal_rtc_seconds();

    reading_t r;
    memset(&r, 0, sizeof r);
    if (!sensor_read_forced(&r)) stats.sensor_failures++;
    r.taken_at_s = now;
    read_battery(&r);
    if (r.sensor_ok) display_history_add(r.temp_cc, now);

    const lw_state_t lw_state = lorawan_service(now, &budget);
    const uint8_t sf = lorawan_current_sf();

    if (lw_state == LW_JOINED) {
        if (airtime_may_transmit(&budget, now, sf, PAYLOAD_SIZE)) {
            uint8_t buf[PAYLOAD_SIZE];
            const payload_fields_t f = payload_from_reading(&r, sf, true);
            payload_pack(&f, buf);
            if (lorawan_send_unconfirmed(buf, sizeof buf)) {
                airtime_record(&budget, now, sf, PAYLOAD_SIZE);
                stats.uplinks_sent++;
                consecutive_send_fails = 0;
            } else {
                stats.uplinks_failed++;
                if (++consecutive_send_fails >= MAX_CONSECUTIVE_SEND_FAILS) {
                    lorawan_session_lost(now);
                    consecutive_send_fails = 0;
                }
            }
        } else {
            stats.uplinks_blocked_by_budget++;
        }
    }

    display_model_t m;
    memset(&m, 0, sizeof m);
    m.reading = r;
    m.sf = sf;
    m.joined = lorawan_is_joined();
    m.no_network = lorawan_state() == LW_JOIN_DORMANT;
    m.airtime_used_pct = (uint8_t)(budget.used_ms >= budget.limit_ms ? 100u
                                   : budget.used_ms * 100u / budget.limit_ms);
    m.time_valid = lw_port_time(&m.unix_offset);
    /* The panel shares the enclosure with the sensor, so the ambient reading
     * stands in for panel temperature. Unknown -> assume in range. */
    m.panel_temp_cc = r.sensor_ok ? r.temp_cc : 2000;

    stats.last_display = display_update(&m, now);
    switch (stats.last_display) {
    case DISPLAY_REFRESH_FULL:    stats.display_full++;         break;
    case DISPLAY_REFRESH_PARTIAL: stats.display_partial++;      break;
    case DISPLAY_SKIP_COLD:       stats.display_skipped_cold++; break;
    default: break;
    }

    /* Interval derived from the remaining budget, never hardcoded (§3.3). */
    uint32_t sleep_s;
    if (lorawan_is_joined()) {
        sleep_s = airtime_next_interval_s(&budget, hal_rtc_seconds(), sf, PAYLOAD_SIZE);
    } else {
        sleep_s = lorawan_seconds_until_join(hal_rtc_seconds());
        if (sleep_s > UNJOINED_MAX_SLEEP_S) sleep_s = UNJOINED_MAX_SLEEP_S;
        if (sleep_s == 0) sleep_s = 1;
    }
    return sleep_s;
}
