#ifndef HAL_H
#define HAL_H
/* Hardware abstraction. Everything board-specific sits behind these calls so
 * the application logic builds and is tested on a host (platform/host) and
 * runs on the STM32WL (platform/stm32wl) unchanged. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- Clock / RTC. The RTC lives in the backup domain and keeps counting
 * across resets, which is what lets the airtime window survive a reboot. */
uint32_t hal_rtc_seconds(void);
void     hal_delay_ms(uint32_t ms);

/* --- Sleep. Enter STOP2 for `seconds`, woken by the RTC wake-up timer.
 * Implementations must restore clocks before returning. */
void hal_enter_stop(uint32_t seconds);
void hal_gpio_config_for_sleep(void);
void hal_gpio_config_for_run(void);

/* --- Backup registers (32-bit, retained across reset while VBAT present) */
#define HAL_BKP_COUNT 8u
uint32_t hal_backup_read(uint8_t idx);
void     hal_backup_write(uint8_t idx, uint32_t value);

/* --- I2C (sensor). 7-bit address. Return false on NACK/timeout. */
bool hal_i2c_write(uint8_t addr, const uint8_t *data, size_t len);
bool hal_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *out, size_t len);

/* --- E-ink panel (SPI, 4-wire: DC selects command/data) */
void hal_eink_power(bool on);            /* panel supply via load switch */
void hal_eink_reset(void);               /* pulse RST low */
void hal_eink_command(uint8_t cmd);
void hal_eink_data(const uint8_t *data, size_t len);
bool hal_eink_busy(void);

/* --- Battery ADC. Returns millivolts at the pack. */
uint16_t hal_battery_mv(void);

/* --- Independent watchdog */
void hal_watchdog_init(uint32_t timeout_ms);
void hal_watchdog_refresh(void);

/* --- System */
void hal_system_init(void);
bool hal_reset_was_watchdog(void);

#endif
