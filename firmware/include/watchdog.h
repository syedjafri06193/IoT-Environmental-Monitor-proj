#ifndef WATCHDOG_H
#define WATCHDOG_H
#include <stdbool.h>
/* Independent watchdog on the LSI (design doc §6.3). A device that hangs in a
 * field needs a site visit; one that reboots does not. */
void watchdog_init(void);
void watchdog_refresh(void);
bool watchdog_caused_reset(void);
#endif
