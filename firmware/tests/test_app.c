/* End-to-end: the real superloop against the simulated board. */
#include "airtime.h"
#include "app.h"
#include "config.h"
#include "lorawan.h"
#include "payload.h"
#include "power.h"
#include "sim.h"
#include "unit.h"

static void run_for(uint32_t seconds)
{
    const uint32_t end = sim.now_s + seconds;
    while (sim.now_s < end) enter_deep_sleep(app_step());
}

/* Count airtime in every rolling 24 h window by replaying uplink times. */
static void test_never_exceeds_fair_use_at_sf12(void)
{
    sim_reset_board();
    sim.sf = 12;
    app_init();
    run_for(3 * 86400);
    const double per_day = sim.uplinks / 3.0;
    CHECK(per_day >= 18 && per_day <= 21);
    CHECK(app_budget()->used_ms <= AIRTIME_DAILY_LIMIT_MS);
    CHECK_EQ(sim.slept_with_panel_awake, 0);
}

static void test_sf7_paced_by_min_interval(void)
{
    sim_reset_board();
    sim.sf = 7;
    app_init();
    run_for(86400);
    /* 86400 / 300 = 288 wakes; the join takes one */
    CHECK(sim.uplinks >= 280 && sim.uplinks <= 290);
    CHECK(app_budget()->used_ms <= AIRTIME_DAILY_LIMIT_MS);
}

static void test_reboot_does_not_reset_budget(void)
{
    sim_reset_board();
    sim.sf = 12;
    app_init();
    run_for(12 * 3600);
    const uint32_t used = app_budget()->used_ms;
    CHECK(used > 0);
    const uint32_t before = sim.uplinks;

    for (int i = 0; i < 5; i++) {          /* a flaky brownout: five resets */
        sim_reboot();
        app_init();
        CHECK(app_budget()->used_ms >= used);
        enter_deep_sleep(app_step());
    }
    run_for(11 * 3600);
    /* Whatever happened, the window never exceeded the limit. */
    CHECK(app_budget()->used_ms <= AIRTIME_DAILY_LIMIT_MS);
    CHECK(sim.uplinks - before <= 20);
}

static void test_payload_carries_sf_and_loaded_battery(void)
{
    sim_reset_board();
    sim.sf = 10;
    app_init();
    run_for(4 * 3600);
    payload_fields_t f;
    CHECK(payload_unpack(sim.last_payload, sim.last_payload_len, &f));
    CHECK_EQ(f.sf, 10);
    CHECK(f.joined);
    CHECK(f.battery_loaded);
    CHECK_EQ(f.temp_cc, 2508);
}

static void test_no_coverage_is_bounded(void)
{
    sim_reset_board();
    sim.join_failures_remaining = UINT32_MAX;
    app_init();
    run_for(7 * 86400);
    /* 12 backoff attempts, then ~4/day dormant retries */
    CHECK(sim.joins_attempted <= MAX_JOIN_ATTEMPTS + 7 * 5);
    CHECK_EQ(lorawan_state(), LW_JOIN_DORMANT);
    CHECK_EQ(sim.uplinks, 0);
}

static void test_sensor_failure_still_reports(void)
{
    sim_reset_board();
    sim.sensor_present = false;
    app_init();
    run_for(3 * 3600);
    payload_fields_t f;
    CHECK(sim.uplinks > 0);
    CHECK(payload_unpack(sim.last_payload, sim.last_payload_len, &f));
    CHECK(f.sensor_error);
    CHECK(app_stats()->sensor_failures > 0);
}

int main(void)
{
    printf("test_app\n");
    RUN(test_never_exceeds_fair_use_at_sf12);
    RUN(test_sf7_paced_by_min_interval);
    RUN(test_reboot_does_not_reset_budget);
    RUN(test_payload_carries_sf_and_loaded_battery);
    RUN(test_no_coverage_is_bounded);
    RUN(test_sensor_failure_still_reports);
    UNIT_DONE("test_app");
}
