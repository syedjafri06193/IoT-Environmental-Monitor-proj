#ifndef EINK_H
#define EINK_H
/* SSD1680 driver for 2.9" 128x296 black/white panels (GDEY029T94, WeAct and
 * Waveshare 2.9" V2 class). Every refresh ends in deep sleep (design doc
 * §8.4): a panel left biased draws current and develops permanent artifacts. */
#include <stdbool.h>
#include <stdint.h>

#define EINK_WIDTH     128u                       /* panel native, portrait */
#define EINK_HEIGHT    296u
#define EINK_FB_BYTES  (EINK_WIDTH / 8u * EINK_HEIGHT)   /* 4736 */

typedef enum { EINK_FULL, EINK_PARTIAL } eink_mode_t;

/* Writes `frame` (1 = white, 0 = black, row-major, MSB first) and refreshes.
 * `previous` is what is on the glass now; required for a partial refresh so
 * the controller can compute the difference after deep sleep. Always leaves
 * the panel in deep sleep. Returns false on a BUSY timeout. */
bool eink_show(const uint8_t *frame, const uint8_t *previous, eink_mode_t mode);

void eink_deep_sleep(void);
bool eink_is_asleep(void);

#endif
