#include "sensor.h"
#include "hal.h"

#define REG_CALIB_00   0x88u   /* 0x88..0xA1, 26 bytes */
#define REG_CALIB_26   0xE1u   /* 0xE1..0xE7, 7 bytes  */
#define REG_CHIP_ID    0xD0u
#define REG_RESET      0xE0u
#define REG_CTRL_HUM   0xF2u
#define REG_STATUS     0xF3u
#define REG_CTRL_MEAS  0xF4u
#define REG_CONFIG     0xF5u
#define REG_DATA       0xF7u   /* 0xF7..0xFE, 8 bytes  */

#define OSRS_X1        0x01u
#define MODE_SLEEP     0x00u
#define MODE_FORCED    0x01u
#define STATUS_MEASURING 0x08u

static bme280_calib_t cal;
static int32_t t_fine;
static bool    have_calib;

static bool write_reg(uint8_t reg, uint8_t val)
{
    const uint8_t buf[2] = { reg, val };
    return hal_i2c_write(BME280_ADDR, buf, sizeof buf);
}

static uint16_t u16le(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static int16_t  s16le(const uint8_t *p) { return (int16_t)u16le(p); }

void bme280_set_calib(const bme280_calib_t *c) { cal = *c; have_calib = true; }

bool sensor_init(void)
{
    uint8_t id = 0;
    if (!hal_i2c_read_reg(BME280_ADDR, REG_CHIP_ID, &id, 1) || id != BME280_CHIP_ID)
        return false;

    write_reg(REG_RESET, 0xB6);
    hal_delay_ms(3);                        /* NVM copy after reset: 2 ms max */

    uint8_t a[26], h[7];
    if (!hal_i2c_read_reg(BME280_ADDR, REG_CALIB_00, a, sizeof a)) return false;
    if (!hal_i2c_read_reg(BME280_ADDR, REG_CALIB_26, h, sizeof h)) return false;

    bme280_calib_t c;
    c.T1 = u16le(&a[0]);  c.T2 = s16le(&a[2]);  c.T3 = s16le(&a[4]);
    c.P1 = u16le(&a[6]);  c.P2 = s16le(&a[8]);  c.P3 = s16le(&a[10]);
    c.P4 = s16le(&a[12]); c.P5 = s16le(&a[14]); c.P6 = s16le(&a[16]);
    c.P7 = s16le(&a[18]); c.P8 = s16le(&a[20]); c.P9 = s16le(&a[22]);
    c.H1 = a[25];
    c.H2 = s16le(&h[0]);
    c.H3 = h[2];
    c.H4 = (int16_t)((int16_t)(int8_t)h[3] * 16 | (h[4] & 0x0F));
    c.H5 = (int16_t)((int16_t)(int8_t)h[5] * 16 | (h[4] >> 4));
    c.H6 = (int8_t)h[6];
    bme280_set_calib(&c);

    /* Weather-monitoring profile from the datasheet: x1/x1/x1, filter off. */
    write_reg(REG_CONFIG, 0x00);
    write_reg(REG_CTRL_HUM, OSRS_X1);
    return write_reg(REG_CTRL_MEAS, (OSRS_X1 << 5) | (OSRS_X1 << 2) | MODE_SLEEP);
}

int32_t bme280_compensate_t(int32_t adc_t)
{
    int32_t var1 = ((((adc_t >> 3) - ((int32_t)cal.T1 << 1))) * (int32_t)cal.T2) >> 11;
    int32_t d    = (adc_t >> 4) - (int32_t)cal.T1;
    int32_t var2 = (((d * d) >> 12) * (int32_t)cal.T3) >> 14;
    t_fine = var1 + var2;
    return (t_fine * 5 + 128) >> 8;
}

uint32_t bme280_compensate_p(int32_t adc_p)
{
    int64_t var1 = (int64_t)t_fine - 128000;
    int64_t var2 = var1 * var1 * (int64_t)cal.P6;
    var2 = var2 + ((var1 * (int64_t)cal.P5) * 131072);
    var2 = var2 + ((int64_t)cal.P4 * 34359738368LL);
    var1 = ((var1 * var1 * (int64_t)cal.P3) / 256) + ((var1 * (int64_t)cal.P2) * 4096);
    var1 = ((((int64_t)1) * 140737488355328LL + var1) * (int64_t)cal.P1) / 8589934592LL;
    if (var1 == 0) return 0;                  /* avoid division by zero */
    int64_t p = 1048576 - adc_p;
    p = (((p * 2147483648LL) - var2) * 3125) / var1;
    var1 = ((int64_t)cal.P9 * (p / 8192) * (p / 8192)) / 33554432;
    var2 = ((int64_t)cal.P8 * p) / 524288;
    p = ((p + var1 + var2) / 256) + ((int64_t)cal.P7 * 16);
    return (uint32_t)p;
}

uint32_t bme280_compensate_h(int32_t adc_h)
{
    int32_t v = t_fine - 76800;
    v = (((((adc_h * 16384) - ((int32_t)cal.H4 * 1048576) - ((int32_t)cal.H5 * v)) + 16384) >> 15) *
         (((((((v * (int32_t)cal.H6) >> 10) * (((v * (int32_t)cal.H3) >> 11) + 32768)) >> 10) +
            2097152) * (int32_t)cal.H2 + 8192) >> 14));
    v = v - (((((v >> 15) * (v >> 15)) >> 7) * (int32_t)cal.H1) >> 4);
    if (v < 0) v = 0;
    if (v > 419430400) v = 419430400;
    return (uint32_t)(v >> 12);
}

bool sensor_read_forced(reading_t *r)
{
    r->sensor_ok = false;
    if (!have_calib && !sensor_init()) return false;

    /* ctrl_hum only takes effect after a ctrl_meas write, so write both. */
    if (!write_reg(REG_CTRL_HUM, OSRS_X1)) return false;
    if (!write_reg(REG_CTRL_MEAS, (OSRS_X1 << 5) | (OSRS_X1 << 2) | MODE_FORCED)) return false;

    /* Typical 8 ms, max 9.3 ms at x1/x1/x1. */
    hal_delay_ms(10);
    uint8_t status = STATUS_MEASURING;
    for (int i = 0; i < 5 && (status & STATUS_MEASURING); i++) {
        if (!hal_i2c_read_reg(BME280_ADDR, REG_STATUS, &status, 1)) return false;
        if (status & STATUS_MEASURING) hal_delay_ms(2);
    }
    if (status & STATUS_MEASURING) return false;

    uint8_t d[8];
    if (!hal_i2c_read_reg(BME280_ADDR, REG_DATA, d, sizeof d)) return false;
    const int32_t adc_p = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | (d[2] >> 4);
    const int32_t adc_t = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | (d[5] >> 4);
    const int32_t adc_h = ((int32_t)d[6] << 8) | d[7];

    /* 0x80000 / 0x8000 are the "skipped/not measured" reset values. */
    if (adc_t == 0x80000 || adc_p == 0x80000 || adc_h == 0x8000) return false;

    r->temp_cc     = bme280_compensate_t(adc_t);          /* must run first: t_fine */
    r->pressure_pa = (bme280_compensate_p(adc_p) + 128u) >> 8;
    r->humidity_cp = (bme280_compensate_h(adc_h) * 100u + 512u) >> 10;
    r->sensor_ok   = true;
    return true;
}

void sensor_sleep(void)
{
    /* Forced mode already returns to sleep; this makes it explicit after a
     * reset or an aborted read. */
    write_reg(REG_CTRL_MEAS, (OSRS_X1 << 5) | (OSRS_X1 << 2) | MODE_SLEEP);
}
