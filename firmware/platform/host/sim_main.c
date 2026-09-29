/* Host simulator: runs the real firmware (app.c, power.c, drivers) against
 * the simulated board and projects battery life.
 *
 *   ./build/sim --days 14 --sf 12
 *   ./build/sim --days 7 --sf 7 --limit 864000     # own gateway, 1% duty cycle
 *   ./build/sim --days 2 --no-coverage              # join backoff / dormant
 *   ./build/sim --days 3 --sf 12 --pbm display.pbm  # dump the e-ink image
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "airtime.h"
#include "app.h"
#include "lorawan.h"
#include "power.h"
#include "sim.h"

#define BATTERY_MAH 1200.0
#define SIX_MONTHS_DAYS 182.0

int main(int argc, char **argv)
{
    int days = 14, sf = 12, no_coverage = 0, cold = 0;
    const char *pbm = NULL;
    long limit = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--days") && i + 1 < argc) days = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sf") && i + 1 < argc) sf = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pbm") && i + 1 < argc) pbm = argv[++i];
        else if (!strcmp(argv[i], "--limit") && i + 1 < argc) limit = atol(argv[++i]);
        else if (!strcmp(argv[i], "--no-coverage")) no_coverage = 1;
        else if (!strcmp(argv[i], "--cold")) cold = 1;
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }

    sim_reset_board();
    sim.sf = (uint8_t)sf;
    sim.time_valid = 1;
    sim.unix_offset = 1790000000u;          /* arbitrary epoch for the clock line */
    if (no_coverage) sim.join_failures_remaining = UINT32_MAX;

    app_init();
    if (limit > 0) {
        /* override the compiled-in fair-use limit for "own gateway" runs */
        ((airtime_budget_t *)app_budget())->limit_ms = (uint32_t)limit;
    }

    const uint32_t start = sim.now_s, end = start + (uint32_t)days * 86400u;
    uint32_t max_day_ms = 0;

    while (sim.now_s < end) {
        /* diurnal temperature: 10 degC +/- 6, humidity inverse, slow pressure drift */
        const double hours = (sim.now_s - start) / 3600.0;
        const double temp_c = (cold ? -8.0 : 14.0) + 6.0 * sin(2 * M_PI * (hours - 9) / 24.0);
        sim.adc_t = 519888 + (int32_t)((temp_c - 25.08) * 3175);
        sim.adc_h = 30000 - (int32_t)((temp_c - 14.0) * 400);
        sim.adc_p = 415148 + (int32_t)(1500 * sin(2 * M_PI * hours / 90.0));
        sim.battery_mv = (uint16_t)(4500 - (sim.now_s - start) / 86400u);

        const uint32_t sleep_s = app_step();
        enter_deep_sleep(sleep_s);

        /* peak airtime used within any 24 h budget window */
        if (app_budget()->used_ms > max_day_ms) max_day_ms = app_budget()->used_ms;
    }

    const app_stats_t *st = app_stats();
    const double sim_days = (sim.now_s - start) / 86400.0;
    const double mah = sim_total_uah() / 1000.0;
    const double mah_6mo = mah / sim_days * SIX_MONTHS_DAYS;

    printf("Simulated %.1f days at SF%d, fair-use limit %u ms/24h%s\n",
           sim_days, sf, app_budget()->limit_ms, no_coverage ? ", NO COVERAGE" : "");
    printf("  join attempts        %u (state: %s)\n", sim.joins_attempted,
           lorawan_is_joined() ? "joined" : lorawan_state() == LW_JOIN_DORMANT ? "dormant" : "waiting");
    printf("  uplinks sent         %u  (%.1f/day, %u blocked by budget)\n",
           st->uplinks_sent, st->uplinks_sent / sim_days, st->uplinks_blocked_by_budget);
    printf("  airtime per uplink   %u ms   peak window usage %u ms of %u\n",
           airtime_ms((uint8_t)sf, 9), max_day_ms, app_budget()->limit_ms);
    printf("  display              %u full, %u partial, %u skipped (cold)\n",
           sim.eink_full, sim.eink_partial, st->display_skipped_cold);
    printf("  panel awake at sleep %u   (must be 0)\n", sim.slept_with_panel_awake);
    printf("  charge used          %.2f mAh  [sleep %.1f  uplink %.1f  join %.2f  sensor %.2f  display %.1f]\n",
           mah, sim.uah_sleep / 1000, sim.uah_uplink / 1000, sim.uah_join / 1000,
           sim.uah_sensor / 1000, sim.uah_display / 1000);
    printf("  projected 6 months   %.0f mAh = %.0f%% of %.0f mAh\n",
           mah_6mo, 100.0 * mah_6mo / BATTERY_MAH, BATTERY_MAH);

    if (pbm) {
        if (sim_write_pbm(pbm)) printf("  display image        %s\n", pbm);
        else { perror(pbm); return 1; }
    }
    return 0;
}
