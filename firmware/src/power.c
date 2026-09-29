#include "power.h"
#include "eink.h"
#include "hal.h"
#include "lorawan_port.h"
#include "sensor.h"

void enter_deep_sleep(uint32_t seconds)
{
    /* Not "probably idle" - explicitly off. */
    sensor_sleep();
    if (!eink_is_asleep()) eink_deep_sleep();   /* §8.4 - should already be */
    lw_port_sleep();

    /* A floating CMOS input can oscillate and draw current indefinitely. */
    hal_gpio_config_for_sleep();

    hal_enter_stop(seconds ? seconds : 1u);    /* restores clocks on wake */

    hal_gpio_config_for_run();
}
