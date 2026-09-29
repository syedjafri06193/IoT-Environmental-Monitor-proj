#include "watchdog.h"
#include "config.h"
#include "hal.h"

void watchdog_init(void)          { hal_watchdog_init(WATCHDOG_TIMEOUT_MS); }
void watchdog_refresh(void)       { hal_watchdog_refresh(); }
bool watchdog_caused_reset(void)  { return hal_reset_was_watchdog(); }
