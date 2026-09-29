#ifndef POWER_H
#define POWER_H
#include <stdint.h>
/* Every peripheral explicitly asleep, every GPIO defined, then STOP2 on the
 * RTC wake-up timer (design doc §6.2). */
void enter_deep_sleep(uint32_t seconds);
#endif
