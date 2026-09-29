#include "airtime.h"
#include "config.h"
#include "lorawan.h"
#include "sim.h"
#include "unit.h"

static airtime_budget_t b;

static void setup(uint32_t limit)
{
    sim_reset_board();
    airtime_init(&b, limit, sim.now_s);
    lorawan_init(sim.now_s);
}

static void test_joins_immediately_when_covered(void)
{
    setup(30000);
    CHECK_EQ(lorawan_service(sim.now_s, &b), LW_JOINED);
    CHECK_EQ(sim.joins_attempted, 1);
    CHECK_EQ(b.used_ms, airtime_ms(12, JOIN_REQUEST_APP_EQUIV));   /* join is charged */
}

static void test_backoff_doubles_to_cap(void)
{
    setup(864000);                         /* budget out of the way */
    sim.join_failures_remaining = UINT32_MAX;
    const uint32_t expect[] = { 30, 60, 120, 240, 480, 960, 1920, 3600, 3600, 3600, 3600 };
    for (unsigned i = 0; i < sizeof expect / sizeof expect[0]; i++) {
        lorawan_service(sim.now_s, &b);
        CHECK_EQ(lorawan_seconds_until_join(sim.now_s), expect[i]);
        /* not due yet: no transmission */
        const uint32_t before = sim.joins_attempted;
        lorawan_service(sim.now_s + 1, &b);
        CHECK_EQ(sim.joins_attempted, before);
        sim.now_s += expect[i];
    }
}

static void test_goes_dormant_then_retries_rarely(void)
{
    setup(864000);
    sim.join_failures_remaining = UINT32_MAX;
    for (unsigned i = 0; i < MAX_JOIN_ATTEMPTS; i++) {
        lorawan_service(sim.now_s, &b);
        sim.now_s += lorawan_seconds_until_join(sim.now_s);
    }
    CHECK_EQ(lorawan_state(), LW_JOIN_DORMANT);
    CHECK_EQ(sim.joins_attempted, MAX_JOIN_ATTEMPTS);
    /* Over a day in a coverage hole: at most a few more attempts. */
    const uint32_t start = sim.now_s;
    while (sim.now_s < start + 86400) {
        lorawan_service(sim.now_s, &b);
        sim.now_s += 600;
    }
    CHECK(sim.joins_attempted - MAX_JOIN_ATTEMPTS <= 86400 / DORMANT_RETRY_S + 1);
    CHECK_EQ(lorawan_state(), LW_JOIN_DORMANT);
}

static void test_recovers_after_failures(void)
{
    setup(30000);
    sim.join_failures_remaining = 3;
    for (int i = 0; i < 10 && !lorawan_is_joined(); i++) {
        lorawan_service(sim.now_s, &b);
        sim.now_s += lorawan_seconds_until_join(sim.now_s);
    }
    CHECK(lorawan_is_joined());
    CHECK_EQ(sim.joins_attempted, 4);
    CHECK_EQ(lorawan_backoff_s(), JOIN_BACKOFF_START_S);   /* reset on success */
}

static void test_joins_respect_fair_use(void)
{
    setup(30000);
    sim.join_failures_remaining = UINT32_MAX;
    b.used_ms = 30000 - 100;               /* almost spent */
    lorawan_service(sim.now_s, &b);
    CHECK_EQ(sim.joins_attempted, 0);
    CHECK(lorawan_seconds_until_join(sim.now_s) > 80000);  /* waits for the reset */
}

static void test_send_requires_join(void)
{
    setup(30000);
    const uint8_t p[9] = {1};
    CHECK(!lorawan_send_unconfirmed(p, sizeof p));
    lorawan_service(sim.now_s, &b);
    CHECK(lorawan_send_unconfirmed(p, sizeof p));
    lorawan_session_lost(sim.now_s);
    CHECK(!lorawan_is_joined());
}

int main(void)
{
    printf("test_lorawan\n");
    RUN(test_joins_immediately_when_covered);
    RUN(test_backoff_doubles_to_cap);
    RUN(test_goes_dormant_then_retries_rarely);
    RUN(test_recovers_after_failures);
    RUN(test_joins_respect_fair_use);
    RUN(test_send_requires_join);
    UNIT_DONE("test_lorawan");
}
