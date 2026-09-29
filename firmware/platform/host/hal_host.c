/* Host (PC) implementation of hal.h: a simulated board.
 *  - RTC is sim.now_s; sleeping advances it.
 *  - I2C talks to a fake BME280 register map.
 *  - The SSD1680 command stream is decoded to capture frames and to verify
 *    that the panel is always in deep sleep before the MCU sleeps.
 *  - Energy is accumulated with the design doc §4.2 figures so the simulator
 *    can project battery life. */
#include <stdio.h>
#include <string.h>
#include "airtime.h"
#include "hal.h"
#include "sim.h"

sim_t sim;

#define SLEEP_UA            5.0     /* §4.2 sleep total */
#define SENSOR_UAH          0.1     /* forced read incl. MCU awake */
#define EINK_FULL_UAH       8.3
#define EINK_PARTIAL_UAH    1.7

static uint32_t bkp[HAL_BKP_COUNT];

/* ------------------------------------------------------------ fake BME280 */
static uint8_t bme_regs[256];
static uint8_t bme_ctrl_meas;

static void put_le16(uint8_t *p, int v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static void bme_load_calibration(void)
{
    /* Datasheet reference calibration (T and P) plus typical H values. */
    uint8_t *a = &bme_regs[0x88];
    put_le16(a + 0, 27504); put_le16(a + 2, 26435); put_le16(a + 4, -1000);
    put_le16(a + 6, 36477); put_le16(a + 8, -10685); put_le16(a + 10, 3024);
    put_le16(a + 12, 2855); put_le16(a + 14, 140);  put_le16(a + 16, -7);
    put_le16(a + 18, 15500); put_le16(a + 20, -14600); put_le16(a + 22, 6000);
    a[25] = 75;                                   /* H1 */
    uint8_t *h = &bme_regs[0xE1];
    put_le16(h, 362);                             /* H2 */
    h[2] = 0;                                     /* H3 */
    h[3] = 313 >> 4;                              /* H4 = 313 */
    h[4] = (uint8_t)((313 & 0x0F) | ((50 & 0x0F) << 4));
    h[5] = 50 >> 4;                               /* H5 = 50 */
    h[6] = 30;                                    /* H6 */
    bme_regs[0xD0] = 0x60;
}

bool hal_i2c_write(uint8_t addr, const uint8_t *data, size_t len)
{
    if (addr != 0x76 || !sim.sensor_present || len < 2) return false;
    const uint8_t reg = data[0], val = data[1];
    bme_regs[reg] = val;
    if (reg == 0xF4) {
        bme_ctrl_meas = val;
        if ((val & 0x03) == 0x01) {               /* forced: latch a sample */
            sim.uah_sensor += SENSOR_UAH;
            uint8_t *d = &bme_regs[0xF7];
            d[0] = (uint8_t)(sim.adc_p >> 12); d[1] = (uint8_t)(sim.adc_p >> 4); d[2] = (uint8_t)(sim.adc_p << 4);
            d[3] = (uint8_t)(sim.adc_t >> 12); d[4] = (uint8_t)(sim.adc_t >> 4); d[5] = (uint8_t)(sim.adc_t << 4);
            d[6] = (uint8_t)(sim.adc_h >> 8);  d[7] = (uint8_t)sim.adc_h;
        }
    }
    return true;
}

bool hal_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *out, size_t len)
{
    if (addr != 0x76 || !sim.sensor_present) return false;
    for (size_t i = 0; i < len; i++) out[i] = bme_regs[(uint8_t)(reg + i)];
    return true;
}

/* ------------------------------------------------------------ fake SSD1680 */
static uint8_t eink_cmd;
static uint8_t eink_update_mode;
static uint8_t ram_bw[EINK_FB_BYTES];
static size_t  ram_pos;

void hal_eink_power(bool on) { (void)on; }
void hal_eink_reset(void)    { sim.eink_asleep = false; }
bool hal_eink_busy(void)     { return false; }

void hal_eink_command(uint8_t cmd)
{
    eink_cmd = cmd;
    ram_pos = 0;
    if (cmd == 0x20) {                             /* master activation */
        memcpy(sim.glass, ram_bw, sizeof ram_bw);
        if (eink_update_mode == 0xF7) { sim.eink_full++;    sim.uah_display += EINK_FULL_UAH; }
        else                          { sim.eink_partial++; sim.uah_display += EINK_PARTIAL_UAH; }
    }
}

void hal_eink_data(const uint8_t *data, size_t len)
{
    switch (eink_cmd) {
    case 0x24:
        for (size_t i = 0; i < len && ram_pos < sizeof ram_bw; i++) ram_bw[ram_pos++] = data[i];
        break;
    case 0x22: eink_update_mode = data[0]; break;
    case 0x10: if (data[0] & 0x03) sim.eink_asleep = true; break;
    default: break;
    }
}

/* ------------------------------------------------------------ core */
uint32_t hal_rtc_seconds(void)    { return sim.now_s; }
void     hal_delay_ms(uint32_t ms) { (void)ms; }

void hal_enter_stop(uint32_t seconds)
{
    if (!sim.eink_asleep) sim.slept_with_panel_awake++;
    sim.sleeps++;
    sim.now_s += seconds;
    sim.uah_sleep += SLEEP_UA * seconds / 3600.0;
}

void hal_gpio_config_for_sleep(void) {}
void hal_gpio_config_for_run(void)   {}

uint32_t hal_backup_read(uint8_t idx)             { return idx < HAL_BKP_COUNT ? bkp[idx] : 0; }
void     hal_backup_write(uint8_t idx, uint32_t v) { if (idx < HAL_BKP_COUNT) bkp[idx] = v; }

uint16_t hal_battery_mv(void) { return sim.battery_mv; }

void hal_watchdog_init(uint32_t timeout_ms) { (void)timeout_ms; }
void hal_watchdog_refresh(void) {}
void hal_system_init(void) {}
bool hal_reset_was_watchdog(void) { return false; }

/* ------------------------------------------------------------ sim control */
void sim_reboot(void)
{
    /* RTC and backup registers survive; RAM does not. */
    memset(bme_regs, 0, sizeof bme_regs);
    bme_load_calibration();
    sim.eink_asleep = true;
}

void sim_reset_board(void)
{
    memset(&sim, 0, sizeof sim);
    memset(bkp, 0, sizeof bkp);
    sim.now_s = 1000;
    sim.sensor_present = true;
    sim.adc_t = 519888;     /* 25.08 degC with the reference calibration */
    sim.adc_p = 415148;     /* ~1006.5 hPa */
    sim.adc_h = 30000;
    sim.battery_mv = 4500;
    sim.sf = 7;
    sim.join_sf = 12;
    sim.send_ok = true;
    memset(sim.glass, 0xFF, sizeof sim.glass);
    sim_reboot();
}

double sim_total_uah(void)
{
    return sim.uah_sleep + sim.uah_uplink + sim.uah_join + sim.uah_sensor + sim.uah_display;
}

bool sim_write_pbm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    const int W = 296, H = 128;
    fprintf(f, "P1\n%d %d\n", W, H);
    for (int ly = 0; ly < H; ly++) {
        for (int lx = 0; lx < W; lx++) {
            const int x = (int)EINK_WIDTH - 1 - ly, y = lx;
            const int white = (sim.glass[y * (int)(EINK_WIDTH / 8u) + x / 8] >> (7 - x % 8)) & 1;
            fputc(white ? '0' : '1', f);
        }
        fputc('\n', f);
    }
    fclose(f);
    return true;
}
