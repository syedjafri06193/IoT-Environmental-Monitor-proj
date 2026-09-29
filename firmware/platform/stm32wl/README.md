# STM32WL device port

The application (`firmware/src`) is hardware-independent. This directory holds
the two files that bind it to an STM32WL and STM32CubeWL's LoRaMAC-node stack:

| File | Implements |
|---|---|
| `board.h` | Pin map — **edit for your wiring** |
| `hal_stm32wl.c` | `hal.h`: RTC seconds, STOP2 sleep, I2C, SPI e-ink, switched battery divider, IWDG, backup registers |
| `lorawan_port_stm32wl.c` | `lorawan_port.h`: blocking join/send over `LmHandler`, ADR SF readback, mid-TX battery sample, DeviceTime |

## Integration (STM32CubeIDE)

1. In STM32CubeMX create a project from the **`LoRaWAN_End_Node`** example for
   NUCLEO-WL55JC1 (or your STM32WLE5 board). This generates `SystemClock_Config`,
   `MX_RTC_Init`, `rtc_if.c` (binary-mode RTC with MSB-tick extension),
   `stm32_timer.c`, `sys_app.c`, the SubGHz radio and the `LmHandler` middleware.
2. Delete the template's `lora_app.c` and its call from `app_lorawan.c`
   (`MX_LoRaWAN_Init` should still initialise the radio board interface, but
   not start the template's periodic uplink timer), and remove the template's
   `main()` loop.
3. Add `firmware/src/*.c` and this directory's two `.c` files to the build;
   add `firmware/include` and this directory to the include path.
4. Set credentials in `se-identity.h` (DevEUI, JoinEUI, AppKey) as CubeWL
   documents. For TTN keep `AIRTIME_DAILY_LIMIT_MS=30000`; for your own
   gateway add `-DAIRTIME_DAILY_LIMIT_MS=864000` **and** raise
   `MIN_INTERVAL_S` — see `docs/power-budget.md`, the battery becomes the limit.
5. In `sys_conf.h` set `LOW_POWER_DISABLE 0`, `DEBUGGER_ENABLED 0` and
   `APP_LOG_ENABLED 0` for deployment (debug output at 115200 baud is real
   energy — design doc §6.2).

On first boot `hal_watchdog_init()` programs the option bytes
(`IWDG_STOP`/`IWDG_STDBY` freeze, BOR level 1) and the MCU resets once. This
is expected.

## What has and hasn't been verified

The application logic, drivers' command sequences and the policies are
exercised by the host tests and simulator. These two port files follow the
STM32CubeWL v1.3 HAL and LmHandler APIs but have **not** been compiled or run
on hardware in this repository — `LmHandler` signatures differ slightly
between CubeWL releases. Milestones M1 and M3 in the design doc are where they
get proven: measure sleep current on the bare board with a PPK2 before
anything else.
