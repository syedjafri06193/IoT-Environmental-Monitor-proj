#ifndef DISPLAY_H
#define DISPLAY_H
/* What the e-ink shows and when it refreshes (design doc §8.2, §8.3, §8.5).
 *
 *  +----------------------------------------+
 *  | 21.4 C              62 %RH             |
 *  | 1013.2 hPa                             |
 *  | |||||||||||||||||||||||    24h         |
 *  | at 14:05 UTC                           |
 *  | SF12 | bat 4.10V | air 41%             |
 *  +----------------------------------------+
 *
 * The time line is absolute, not "N min ago": an e-ink image persists after
 * the device stops refreshing it (cold, flat battery), and a relative age
 * frozen on the glass would be a lie. Without network time it falls back to
 * time since boot ("at T+2d 03:15"). */
#include <stdbool.h>
#include <stdint.h>
#include "reading.h"

typedef enum {
    DISPLAY_SKIP_UNCHANGED,
    DISPLAY_SKIP_TOO_SOON,
    DISPLAY_SKIP_COLD,
    DISPLAY_REFRESH_PARTIAL,
    DISPLAY_REFRESH_FULL,
    DISPLAY_ERROR,
} display_result_t;

typedef struct {
    reading_t reading;
    uint8_t   sf;
    bool      joined;
    bool      no_network;       /* join gave up: show "No network" */
    uint8_t   airtime_used_pct;
    bool      time_valid;       /* unix_offset is meaningful */
    uint32_t  unix_offset;      /* unix = rtc_seconds + unix_offset */
    int32_t   panel_temp_cc;    /* for refresh gating */
} display_model_t;

void display_init(void);

/* Record a temperature sample into the 24 h sparkline history. */
void display_history_add(int32_t temp_cc, uint32_t now_s);

/* Decide and, if warranted, render + refresh. */
display_result_t display_update(const display_model_t *m, uint32_t now_s);

/* Force the next update to refresh (e.g. network state changed). */
void display_invalidate(void);

/* Render only (no panel I/O) - for tests and the host simulator. */
void display_render(const display_model_t *m, uint32_t now_s, uint8_t *fb);
const uint8_t *display_framebuffer(void);

#endif
