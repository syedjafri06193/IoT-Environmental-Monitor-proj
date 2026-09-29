#include "display.h"
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "eink.h"
#include "font6x11.h"

#define LW 296   /* logical landscape width  */
#define LH 128   /* logical landscape height */

static uint8_t fb_shown[EINK_FB_BYTES];   /* what is on the glass */
static uint8_t fb_next[EINK_FB_BYTES];

static struct {
    bool            valid;
    bool            invalidated;
    display_model_t shown;
    uint32_t        last_refresh_s;
    uint32_t        partial_count;
} st;

static struct { int32_t temp_cc; uint32_t hour; bool used; } hist[HISTORY_SLOTS];

void display_init(void)
{
    memset(&st, 0, sizeof st);
    memset(hist, 0, sizeof hist);
    memset(fb_shown, 0xFF, sizeof fb_shown);
}

void display_invalidate(void) { st.invalidated = true; }
const uint8_t *display_framebuffer(void) { return fb_shown; }

void display_history_add(int32_t temp_cc, uint32_t now_s)
{
    const uint32_t hour = now_s / HISTORY_SLOT_S;
    const uint32_t i = hour % HISTORY_SLOTS;
    hist[i].temp_cc = temp_cc;   /* latest sample in the hour wins */
    hist[i].hour = hour;
    hist[i].used = true;
}

/* ---------------------------------------------------------------- drawing */

static void px(uint8_t *fb, int lx, int ly)
{
    if (lx < 0 || lx >= LW || ly < 0 || ly >= LH) return;
    /* landscape -> portrait panel: rotate 90 degrees */
    const int x = (int)EINK_WIDTH - 1 - ly;
    const int y = lx;
    fb[y * (int)(EINK_WIDTH / 8u) + x / 8] &= (uint8_t)~(0x80u >> (x % 8));
}

static void fill(uint8_t *fb, int x, int y, int w, int h)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) px(fb, x + i, y + j);
}

static int text(uint8_t *fb, int x, int y, const char *s, int scale)
{
    for (; *s; s++) {
        unsigned c = (unsigned char)*s;
        if (c < FONT_FIRST || c >= FONT_FIRST + FONT_GLYPHS) c = '?';
        const uint8_t *g = font6x11[c - FONT_FIRST];
        for (int row = 0; row < FONT_H; row++)
            for (int col = 0; col < FONT_W; col++)
                if (g[row] & (0x80u >> col)) fill(fb, x + col * scale, y + row * scale, scale, scale);
        x += FONT_W * scale;
    }
    return x;
}

static void fmt_fixed(char *buf, size_t n, int32_t v_x100, int decimals)
{
    /* integer-only formatting of a x100 value to 1 or 2 decimals */
    const char *sign = v_x100 < 0 ? "-" : "";
    int32_t a = v_x100 < 0 ? -v_x100 : v_x100;
    if (decimals == 1) {
        a = (a + 5) / 10;
        snprintf(buf, n, "%s%ld.%ld", sign, (long)(a / 10), (long)(a % 10));
    } else {
        snprintf(buf, n, "%s%ld.%02ld", sign, (long)(a / 100), (long)(a % 100));
    }
}

static void sparkline(uint8_t *fb, int x0, int y0, int w, int h, uint32_t now_s)
{
    const uint32_t cur = now_s / HISTORY_SLOT_S;
    int32_t lo = INT32_MAX, hi = INT32_MIN;
    for (unsigned k = 0; k < HISTORY_SLOTS; k++) {
        const uint32_t hour = cur + 1u + k - HISTORY_SLOTS;
        const unsigned i = hour % HISTORY_SLOTS;
        if (cur + 1u < HISTORY_SLOTS - k || !hist[i].used || hist[i].hour != hour) continue;
        if (hist[i].temp_cc < lo) lo = hist[i].temp_cc;
        if (hist[i].temp_cc > hi) hi = hist[i].temp_cc;
    }
    if (lo > hi) return;                       /* no history yet */
    const int32_t span = (hi - lo) < 100 ? 100 : (hi - lo);  /* at least 1 degC tall */
    const int bw = w / (int)HISTORY_SLOTS;

    for (unsigned k = 0; k < HISTORY_SLOTS; k++) {
        const uint32_t hour = cur + 1u + k - HISTORY_SLOTS;
        const unsigned i = hour % HISTORY_SLOTS;
        if (cur + 1u < HISTORY_SLOTS - k || !hist[i].used || hist[i].hour != hour) continue;
        const int bh = 2 + (int)((int64_t)(hist[i].temp_cc - lo) * (h - 2) / span);
        fill(fb, x0 + (int)k * bw, y0 + h - bh, bw - 2, bh);
    }
    fill(fb, x0, y0 + h + 1, w - 2, 1);        /* baseline */
}

void display_render(const display_model_t *m, uint32_t now_s, uint8_t *fb)
{
    char a[32], b[32], line[64];
    memset(fb, 0xFF, EINK_FB_BYTES);
    const reading_t *r = &m->reading;

    if (r->sensor_ok) {
        fmt_fixed(a, sizeof a, r->temp_cc, 1);
        snprintf(line, sizeof line, "%s" FONT_DEGREE "C", a);
        text(fb, 6, 6, line, 3);
        snprintf(line, sizeof line, "%ld%%RH", (long)((r->humidity_cp + 50) / 100));
        text(fb, LW - 6 - (int)strlen(line) * FONT_W * 2, 12, line, 2);
        fmt_fixed(a, sizeof a, (int32_t)r->pressure_pa, 1);   /* Pa == hPa x 100 */
        snprintf(line, sizeof line, "%s hPa", a);
        text(fb, 6, 44, line, 2);
    } else {
        text(fb, 6, 10, "Sensor error", 3);
    }

    sparkline(fb, 6, 70, 240, 22, now_s);
    text(fb, 254, 78, "24h", 1);

    /* Time of reading: absolute, so a frozen image stays truthful. */
    if (m->time_valid) {
        const uint32_t t = r->taken_at_s + m->unix_offset;
        snprintf(line, sizeof line, "at %02lu:%02lu UTC",
                 (unsigned long)(t / 3600u % 24u), (unsigned long)(t / 60u % 60u));
    } else {
        const uint32_t t = r->taken_at_s;
        snprintf(line, sizeof line, "at T+%lud %02lu:%02lu",
                 (unsigned long)(t / 86400u), (unsigned long)(t / 3600u % 24u),
                 (unsigned long)(t / 60u % 60u));
    }
    text(fb, 6, 100, line, 1);

    if (m->no_network) {
        text(fb, 150, 100, "No network", 1);
        text(fb, 150, 113, "check coverage", 1);
        fmt_fixed(b, sizeof b, (int32_t)r->battery_mv / 10, 2);
        snprintf(line, sizeof line, "bat %sV", b);
        text(fb, 6, 113, line, 1);
    } else {
        fmt_fixed(b, sizeof b, (int32_t)r->battery_mv / 10, 2);
        if (m->joined)
            snprintf(line, sizeof line, "SF%u | bat %sV | air %u%%",
                     (unsigned)m->sf, b, (unsigned)m->airtime_used_pct);
        else
            snprintf(line, sizeof line, "joining... | bat %sV", b);
        text(fb, 6, 113, line, 1);
    }
    if (r->battery_mv && r->battery_mv < BATTERY_LOW_MV) text(fb, 250, 6, "LOW", 1);
}

/* ---------------------------------------------------------------- policy */

static int32_t iabs32(int32_t v) { return v < 0 ? -v : v; }

static bool differs_meaningfully(const display_model_t *a, const display_model_t *b)
{
    const reading_t *x = &a->reading, *y = &b->reading;
    if (x->sensor_ok != y->sensor_ok) return true;
    if (a->sf != b->sf || a->joined != b->joined || a->no_network != b->no_network) return true;
    if (iabs32((int32_t)x->battery_mv - (int32_t)y->battery_mv) >= 100) return true;
    if (!x->sensor_ok) return false;
    return iabs32(x->temp_cc - y->temp_cc) >= DISPLAY_DELTA_TEMP_CC ||
           iabs32((int32_t)x->humidity_cp - (int32_t)y->humidity_cp) >= DISPLAY_DELTA_RH_CP ||
           iabs32((int32_t)x->pressure_pa - (int32_t)y->pressure_pa) >= DISPLAY_DELTA_P_DPA * 10;
}

display_result_t display_update(const display_model_t *m, uint32_t now_s)
{
    /* §8.3: below the rated range refreshes ghost or fail - leave the last
     * good image (it carries its own timestamp) rather than corrupt it. */
    if (m->panel_temp_cc < EINK_MIN_TEMP_CC || m->panel_temp_cc > EINK_MAX_TEMP_CC)
        return DISPLAY_SKIP_COLD;

    const bool forced = !st.valid || st.invalidated;
    if (!forced) {
        const uint32_t since = now_s - st.last_refresh_s;
        if (since < DISPLAY_MIN_INTERVAL_S) return DISPLAY_SKIP_TOO_SOON;
        if (since < DISPLAY_MAX_AGE_S && !differs_meaningfully(m, &st.shown))
            return DISPLAY_SKIP_UNCHANGED;
    }

    display_render(m, now_s, fb_next);

    const bool full = !st.valid || st.partial_count >= PARTIALS_BEFORE_FULL;
    const bool ok = eink_show(fb_next, st.valid ? fb_shown : 0, full ? EINK_FULL : EINK_PARTIAL);
    /* eink_show always ends in deep sleep (§8.4). */
    if (!ok) { st.valid = false; return DISPLAY_ERROR; }

    memcpy(fb_shown, fb_next, sizeof fb_shown);
    st.shown = *m;
    st.valid = true;
    st.invalidated = false;
    st.last_refresh_s = now_s;
    st.partial_count = full ? 0 : st.partial_count + 1;
    return full ? DISPLAY_REFRESH_FULL : DISPLAY_REFRESH_PARTIAL;
}
