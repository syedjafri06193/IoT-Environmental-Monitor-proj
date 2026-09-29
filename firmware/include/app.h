#ifndef APP_H
#define APP_H
/* The superloop body (design doc §15.3), split out of main() so the host
 * simulator and tests drive exactly the code the device runs. */
#include <stdint.h>
#include "airtime.h"
#include "display.h"

typedef struct {
    uint32_t uplinks_sent;
    uint32_t uplinks_blocked_by_budget;
    uint32_t uplinks_failed;
    uint32_t sensor_failures;
    uint32_t display_full, display_partial, display_skipped_cold;
    display_result_t last_display;
} app_stats_t;

void     app_init(void);
/* One wake: read, maybe transmit, maybe refresh. Returns seconds to sleep. */
uint32_t app_step(void);

const app_stats_t      *app_stats(void);
const airtime_budget_t *app_budget(void);

#endif
