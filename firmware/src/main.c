/* IoT Environmental Monitor - superloop with deep sleep, no RTOS
 * (design doc §12, §15.3). */
#include "app.h"
#include "hal.h"
#include "power.h"
#include "watchdog.h"

int main(void)
{
    hal_system_init();          /* clocks, brownout level, GPIO, RTC */
    watchdog_init();
    app_init();

    for (;;) {
        const uint32_t sleep_s = app_step();
        enter_deep_sleep(sleep_s);
    }
}
