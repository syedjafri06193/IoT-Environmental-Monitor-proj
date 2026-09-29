#include "eink.h"
#include "hal.h"

/* SSD1680 commands */
#define CMD_DRIVER_OUTPUT     0x01
#define CMD_DEEP_SLEEP        0x10
#define CMD_DATA_ENTRY        0x11
#define CMD_SW_RESET          0x12
#define CMD_TEMP_SENSOR       0x18
#define CMD_MASTER_ACTIVATE   0x20
#define CMD_UPDATE_CTRL1      0x21
#define CMD_UPDATE_CTRL2      0x22
#define CMD_WRITE_RAM_BW      0x24
#define CMD_WRITE_RAM_PREV    0x26
#define CMD_BORDER            0x3C
#define CMD_RAM_X_RANGE       0x44
#define CMD_RAM_Y_RANGE       0x45
#define CMD_RAM_X_COUNTER     0x4E
#define CMD_RAM_Y_COUNTER     0x4F

/* 0x22 sequences (from the GDEY029T94 / GxEPD2 reference):
 * full    - clock, analog, load temperature + OTP LUT, display, power down
 * partial - clock, analog, display mode 2 (partial LUT), power down       */
#define UPDATE_FULL           0xF7
#define UPDATE_PARTIAL        0xFC

#define BUSY_TIMEOUT_MS       5000u

static bool asleep = true;

static void cmd1(uint8_t c, uint8_t d) { hal_eink_command(c); hal_eink_data(&d, 1); }

static bool wait_idle(void)
{
    for (uint32_t t = 0; t < BUSY_TIMEOUT_MS; t += 5) {
        if (!hal_eink_busy()) return true;
        hal_delay_ms(5);
    }
    return false;
}

static void set_window(void)
{
    static const uint8_t x_range[] = { 0x00, (EINK_WIDTH / 8u) - 1u };
    static const uint8_t y_range[] = { 0x00, 0x00, (EINK_HEIGHT - 1u) & 0xFF, (EINK_HEIGHT - 1u) >> 8 };
    static const uint8_t y_zero[]  = { 0x00, 0x00 };
    hal_eink_command(CMD_RAM_X_RANGE);   hal_eink_data(x_range, sizeof x_range);
    hal_eink_command(CMD_RAM_Y_RANGE);   hal_eink_data(y_range, sizeof y_range);
    cmd1(CMD_RAM_X_COUNTER, 0x00);
    hal_eink_command(CMD_RAM_Y_COUNTER); hal_eink_data(y_zero, sizeof y_zero);
}

/* Deep sleep requires a hardware reset to wake, so this runs on every update. */
static bool init_panel(void)
{
    hal_eink_power(true);
    hal_eink_reset();
    if (!wait_idle()) return false;
    hal_eink_command(CMD_SW_RESET);
    hal_delay_ms(10);
    if (!wait_idle()) return false;

    static const uint8_t drv[] = { (EINK_HEIGHT - 1u) & 0xFF, (EINK_HEIGHT - 1u) >> 8, 0x00 };
    hal_eink_command(CMD_DRIVER_OUTPUT); hal_eink_data(drv, sizeof drv);
    cmd1(CMD_DATA_ENTRY, 0x03);          /* X+, Y+ */
    cmd1(CMD_BORDER, 0x05);
    static const uint8_t upd1[] = { 0x00, 0x80 };
    hal_eink_command(CMD_UPDATE_CTRL1);  hal_eink_data(upd1, sizeof upd1);
    cmd1(CMD_TEMP_SENSOR, 0x80);         /* internal sensor: selects waveform by temperature */
    set_window();
    asleep = false;
    return true;
}

static void write_ram(uint8_t which, const uint8_t *frame)
{
    set_window();
    hal_eink_command(which);
    hal_eink_data(frame, EINK_FB_BYTES);
}

void eink_deep_sleep(void)
{
    cmd1(CMD_DEEP_SLEEP, 0x01);          /* mode 1: RAM retained, ~1 uA */
    hal_delay_ms(1);
    asleep = true;
}

bool eink_is_asleep(void) { return asleep; }

bool eink_show(const uint8_t *frame, const uint8_t *previous, eink_mode_t mode)
{
    if (mode == EINK_PARTIAL && previous == 0) mode = EINK_FULL;

    bool ok = init_panel();
    if (ok) {
        if (mode == EINK_FULL) {
            write_ram(CMD_WRITE_RAM_PREV, frame);    /* keep both RAMs in step */
            write_ram(CMD_WRITE_RAM_BW, frame);
            cmd1(CMD_UPDATE_CTRL2, UPDATE_FULL);
        } else {
            write_ram(CMD_WRITE_RAM_PREV, previous);
            write_ram(CMD_WRITE_RAM_BW, frame);
            cmd1(CMD_UPDATE_CTRL2, UPDATE_PARTIAL);
        }
        hal_eink_command(CMD_MASTER_ACTIVATE);
        ok = wait_idle();
    }
    /* Non-negotiable, including on the error path. */
    eink_deep_sleep();
    return ok;
}
