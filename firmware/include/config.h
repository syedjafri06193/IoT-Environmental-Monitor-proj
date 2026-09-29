#ifndef CONFIG_H
#define CONFIG_H

/* ---- Network operator policy (design doc §3.1) -------------------------
 * TTN public network: 30 s uplink airtime per device per rolling 24 h.
 * Self-hosted ChirpStack on EU868 sub-band M: 1% = 864 s. Override with
 * -DAIRTIME_DAILY_LIMIT_MS=864000 when running your own gateway. */
#ifndef AIRTIME_DAILY_LIMIT_MS
#define AIRTIME_DAILY_LIMIT_MS   30000u
#endif

/* Uplink pacing bounds. The actual interval is derived from the remaining
 * budget (airtime.c); these only clamp it. */
#ifndef MIN_INTERVAL_S
#define MIN_INTERVAL_S           300u      /* 5 min; raise when self-hosting */
#endif
#define MAX_INTERVAL_S           7200u     /* 2 h    */

/* LoRaWAN */
#define LORAWAN_APP_PORT         1u
#define JOIN_BACKOFF_START_S     30u
#define JOIN_BACKOFF_MAX_S       3600u
#define MAX_JOIN_ATTEMPTS        12u
#define DORMANT_RETRY_S          (6u * 3600u)  /* "retry a few times a day" */

/* E-ink refresh policy (§8.2, §8.3) */
#define PARTIALS_BEFORE_FULL     12u
#define DISPLAY_MIN_INTERVAL_S   900u      /* never refresh faster than this */
#define DISPLAY_MAX_AGE_S        (3u * 3600u) /* force refresh so the age line stays honest */
#define DISPLAY_DELTA_TEMP_CC    20        /* 0.20 degC  */
#define DISPLAY_DELTA_RH_CP      200       /* 2.00 %RH   */
#define DISPLAY_DELTA_P_DPA      10        /* 1.0 hPa    */
#define EINK_MIN_TEMP_CC         0         /* panel rated 0..50 degC */
#define EINK_MAX_TEMP_CC         5000

/* Sparkline: one slot per hour for 24 h */
#define HISTORY_SLOTS            24u
#define HISTORY_SLOT_S           3600u

/* Watchdog: guards the awake path. The STM32WL IWDG tops out near 32 s, far
 * shorter than a sleep, so the port sets the IWDG_STOP option byte to freeze
 * it in STOP2 (see platform/stm32wl/hal_stm32wl.c). A hang while awake - the
 * failure that needs a site visit - still resets the device. */
#define WATCHDOG_TIMEOUT_MS      30000u

/* Battery reporting and low-battery threshold (3x Li-FeS2 AAA) */
#define BATTERY_LOW_MV           3300u

#endif
