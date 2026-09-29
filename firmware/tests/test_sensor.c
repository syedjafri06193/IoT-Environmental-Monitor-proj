#include "sensor.h"
#include "sim.h"
#include "unit.h"

static const bme280_calib_t ref = {
    .T1 = 27504, .T2 = 26435, .T3 = -1000,
    .P1 = 36477, .P2 = -10685, .P3 = 3024, .P4 = 2855, .P5 = 140, .P6 = -7,
    .P7 = 15500, .P8 = -14600, .P9 = 6000,
    .H1 = 75, .H2 = 362, .H3 = 0, .H4 = 313, .H5 = 50, .H6 = 30,
};

static void test_datasheet_reference_vector(void)
{
    /* Bosch BME280 datasheet worked example: adc_T 519888, adc_P 415148
     * -> 25.08 degC, 100653 Pa. */
    bme280_set_calib(&ref);
    CHECK_EQ(bme280_compensate_t(519888), 2508);
    CHECK_NEAR(bme280_compensate_p(415148) / 256.0, 100653.0, 1.0);
}

static void test_humidity_bounded_and_monotonic(void)
{
    bme280_set_calib(&ref);
    bme280_compensate_t(519888);
    uint32_t prev = 0;
    for (int32_t adc = 20000; adc <= 45000; adc += 1000) {
        const uint32_t h = bme280_compensate_h(adc);
        CHECK(h <= 100u * 1024u);
        CHECK(h >= prev);
        prev = h;
    }
}

static void test_driver_reads_via_i2c(void)
{
    sim_reset_board();
    CHECK(sensor_init());
    reading_t r = {0};
    CHECK(sensor_read_forced(&r));
    CHECK(r.sensor_ok);
    CHECK_EQ(r.temp_cc, 2508);
    CHECK_NEAR(r.pressure_pa, 100653, 1);
    CHECK(r.humidity_cp > 0 && r.humidity_cp <= 10000);
}

static void test_missing_sensor_reports_error(void)
{
    sim_reset_board();
    sim.sensor_present = false;
    CHECK(!sensor_init());
    reading_t r = { .sensor_ok = true };
    CHECK(!sensor_read_forced(&r));
    CHECK(!r.sensor_ok);
}

static void test_skipped_measurement_rejected(void)
{
    sim_reset_board();
    CHECK(sensor_init());
    sim.adc_t = 0x80000;                  /* reset value: not measured */
    reading_t r = {0};
    CHECK(!sensor_read_forced(&r));
}

int main(void)
{
    printf("test_sensor\n");
    RUN(test_datasheet_reference_vector);
    RUN(test_humidity_bounded_and_monotonic);
    RUN(test_driver_reads_via_i2c);
    RUN(test_missing_sensor_reports_error);
    RUN(test_skipped_measurement_rejected);
    UNIT_DONE("test_sensor");
}
