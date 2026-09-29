#include <string.h>
#include "config.h"
#include "display.h"
#include "eink.h"
#include "sim.h"
#include "unit.h"

static display_model_t model(int32_t temp_cc)
{
    display_model_t m;
    memset(&m, 0, sizeof m);
    m.reading.temp_cc = temp_cc;
    m.reading.humidity_cp = 6200;
    m.reading.pressure_pa = 101320;
    m.reading.battery_mv = 4100;
    m.reading.sensor_ok = true;
    m.sf = 12;
    m.joined = true;
    m.panel_temp_cc = temp_cc;
    return m;
}

static void setup(void) { sim_reset_board(); display_init(); }

static void test_first_update_is_full_and_sleeps_panel(void)
{
    setup();
    display_model_t m = model(2140);
    CHECK_EQ(display_update(&m, 0), DISPLAY_REFRESH_FULL);
    CHECK_EQ(sim.eink_full, 1);
    CHECK(sim.eink_asleep);                      /* §8.4 */
}

static void test_skips_unchanged_and_too_soon(void)
{
    setup();
    display_model_t m = model(2140);
    display_update(&m, 0);
    m.reading.temp_cc = 2150;                    /* 0.1 degC: not meaningful */
    CHECK_EQ(display_update(&m, DISPLAY_MIN_INTERVAL_S), DISPLAY_SKIP_UNCHANGED);
    m.reading.temp_cc = 2500;                    /* meaningful, but too soon */
    CHECK_EQ(display_update(&m, 60), DISPLAY_SKIP_TOO_SOON);
    CHECK_EQ(display_update(&m, DISPLAY_MIN_INTERVAL_S), DISPLAY_REFRESH_PARTIAL);
    CHECK(sim.eink_asleep);
}

static void test_full_refresh_every_n_partials(void)
{
    setup();
    display_model_t m = model(1000);
    uint32_t t = 0;
    display_update(&m, t);
    for (unsigned i = 0; i < PARTIALS_BEFORE_FULL; i++) {
        t += DISPLAY_MIN_INTERVAL_S;
        m.reading.temp_cc += 100;
        CHECK_EQ(display_update(&m, t), DISPLAY_REFRESH_PARTIAL);
    }
    t += DISPLAY_MIN_INTERVAL_S;
    m.reading.temp_cc += 100;
    CHECK_EQ(display_update(&m, t), DISPLAY_REFRESH_FULL);   /* clears ghosting */
    CHECK_EQ(sim.eink_full, 2);
    CHECK_EQ(sim.eink_partial, PARTIALS_BEFORE_FULL);
}

static void test_cold_skips_refresh(void)
{
    setup();
    display_model_t m = model(2000);
    display_update(&m, 0);
    m = model(-500);                             /* -5 degC */
    CHECK_EQ(display_update(&m, 7200), DISPLAY_SKIP_COLD);
    CHECK_EQ(sim.eink_full + sim.eink_partial, 1);
}

static void test_max_age_forces_refresh(void)
{
    setup();
    display_model_t m = model(2000);
    display_update(&m, 0);
    CHECK_EQ(display_update(&m, DISPLAY_MAX_AGE_S - 1), DISPLAY_SKIP_UNCHANGED);
    CHECK_EQ(display_update(&m, DISPLAY_MAX_AGE_S), DISPLAY_REFRESH_PARTIAL);
}

static void test_status_changes_refresh(void)
{
    setup();
    display_model_t m = model(2000);
    display_update(&m, 0);
    m.sf = 7;                                    /* ADR moved us */
    CHECK_EQ(display_update(&m, DISPLAY_MIN_INTERVAL_S), DISPLAY_REFRESH_PARTIAL);
    m.no_network = true;
    CHECK_EQ(display_update(&m, 2 * DISPLAY_MIN_INTERVAL_S), DISPLAY_REFRESH_PARTIAL);
}

static int black_pixels(const uint8_t *fb)
{
    int n = 0;
    for (unsigned i = 0; i < EINK_FB_BYTES; i++)
        for (int b = 0; b < 8; b++) n += !((fb[i] >> b) & 1);
    return n;
}

static void test_render_draws_and_glass_matches(void)
{
    setup();
    display_model_t m = model(2140);
    for (uint32_t h = 0; h < 24; h++) display_history_add(1500 + (int32_t)h * 40, h * 3600);
    display_update(&m, 23 * 3600);
    CHECK(black_pixels(sim.glass) > 500);
    CHECK(memcmp(sim.glass, display_framebuffer(), EINK_FB_BYTES) == 0);
}

int main(void)
{
    printf("test_display\n");
    RUN(test_first_update_is_full_and_sleeps_panel);
    RUN(test_skips_unchanged_and_too_soon);
    RUN(test_full_refresh_every_n_partials);
    RUN(test_cold_skips_refresh);
    RUN(test_max_age_forces_refresh);
    RUN(test_status_changes_refresh);
    RUN(test_render_draws_and_glass_matches);
    UNIT_DONE("test_display");
}
