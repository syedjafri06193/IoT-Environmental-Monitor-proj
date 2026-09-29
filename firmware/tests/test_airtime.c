#include "airtime.h"
#include "config.h"
#include "hal.h"
#include "sim.h"
#include "unit.h"

static void test_airtime_matches_reference(void)
{
    /* Cross-checked against tools/airtime_calc.py and published calculators.
     * Note: the design doc's §16.2 figures (1318.9 / 56.6 ms) are for a
     * 7-byte application payload; our 9-byte v1 payload is 1482.8 ms at SF12. */
    CHECK_EQ(airtime_us(12, 7), 1318912);
    CHECK_EQ(airtime_us(7, 7), 56576);
    CHECK_EQ(airtime_us(12, 9), 1482752);
    CHECK_EQ(airtime_us(7, 9), 56576);
    CHECK_EQ(airtime_us(12, 12), 1482752);
    CHECK_EQ(airtime_us(7, 12), 61696);
    CHECK_EQ(airtime_ms(12, 9), 1483);          /* rounded up, never down */
    CHECK_EQ(airtime_ms(7, 9), 57);
}

static void test_low_data_rate_optimisation_applied(void)
{
    /* Without LDRO SF12 would be understated by ~20% (§15.1). */
    CHECK(airtime_us(11, 9) > airtime_us(10, 9) * 19 / 10);
    CHECK_EQ(airtime_us(11, 9), 741376);
}

static void test_invalid_sf_never_transmits(void)
{
    airtime_budget_t b;
    sim_reset_board();
    airtime_init(&b, 30000, 0);
    CHECK_EQ(airtime_ms(6, 9), UINT32_MAX);
    CHECK(!airtime_may_transmit(&b, 0, 13, 9));
}

static void test_budget_blocks_at_limit(void)
{
    airtime_budget_t b;
    sim_reset_board();
    airtime_init(&b, 30000, 1000);
    int sent = 0;
    while (airtime_may_transmit(&b, 1000, 12, 9)) { airtime_record(&b, 1000, 12, 9); sent++; }
    CHECK_EQ(sent, 20);                              /* 20 x 1483 = 29660 <= 30000 */
    CHECK(b.used_ms <= 30000);
    CHECK(!airtime_may_transmit(&b, 1000 + 86399, 12, 9));
    CHECK(airtime_may_transmit(&b, 1000 + 86400, 12, 9));   /* window reset */
    CHECK_EQ(b.used_ms, 0);
}

static void test_budget_survives_reboot(void)
{
    airtime_budget_t b, after;
    sim_reset_board();
    airtime_init(&b, 30000, 5000);
    for (int i = 0; i < 15; i++) airtime_record(&b, 5000 + i * 100, 12, 9);
    sim_reboot();                                    /* RAM gone, backup regs kept */
    airtime_init(&after, 30000, 9000);
    CHECK_EQ(after.used_ms, b.used_ms);
    CHECK_EQ(after.window_start_s, 5000);
}

static void test_corrupt_backup_starts_fresh(void)
{
    airtime_budget_t b;
    sim_reset_board();
    airtime_init(&b, 30000, 100);
    airtime_record(&b, 100, 12, 9);
    hal_backup_write(2, 12345);                      /* used_ms tampered */
    airtime_init(&b, 30000, 200);
    CHECK_EQ(b.used_ms, 0);
}

static void test_clock_backwards_keeps_spend(void)
{
    airtime_budget_t b;
    sim_reset_board();
    airtime_init(&b, 30000, 50000);
    airtime_record(&b, 50000, 12, 9);
    airtime_init(&b, 30000, 10);                     /* RTC went backwards */
    CHECK_EQ(b.used_ms, 1483);
    CHECK_EQ(b.window_start_s, 10);
}

static void test_interval_derived_from_budget(void)
{
    airtime_budget_t b;
    sim_reset_board();
    airtime_init(&b, 30000, 0);
    /* SF12 fresh window: 86400 s / 20 msgs = 4320 s */
    CHECK_EQ(airtime_next_interval_s(&b, 0, 12, 9), 4320);
    /* SF7: 526 affordable -> 164 s, clamped up to MIN_INTERVAL_S */
    CHECK_EQ(airtime_next_interval_s(&b, 0, 7, 9), MIN_INTERVAL_S);
    /* Spent: sleep toward the reset but no longer than MAX_INTERVAL_S */
    b.used_ms = 29000;
    CHECK_EQ(airtime_next_interval_s(&b, 80000, 12, 9), 6400);
    CHECK_EQ(airtime_next_interval_s(&b, 1000, 12, 9), MAX_INTERVAL_S);
    /* Own gateway (1% duty cycle) at SF12 */
    airtime_init(&b, 864000, 0);
    b.used_ms = 0;
    CHECK_EQ(airtime_next_interval_s(&b, 0, 12, 9), MIN_INTERVAL_S);
}

int main(void)
{
    printf("test_airtime\n");
    RUN(test_airtime_matches_reference);
    RUN(test_low_data_rate_optimisation_applied);
    RUN(test_invalid_sf_never_transmits);
    RUN(test_budget_blocks_at_limit);
    RUN(test_budget_survives_reboot);
    RUN(test_corrupt_backup_starts_fresh);
    RUN(test_clock_backwards_keeps_spend);
    RUN(test_interval_derived_from_budget);
    UNIT_DONE("test_airtime");
}
