#ifndef SIM_H
#define SIM_H
/* Host-side simulation controls for platform/host. */
#include <stdbool.h>
#include <stdint.h>
#include "eink.h"

typedef struct {
    /* time */
    uint32_t now_s;
    /* environment -> fake BME280 raw ADC */
    int32_t  adc_t, adc_p, adc_h;
    bool     sensor_present;
    uint16_t battery_mv;
    /* LoRaWAN simulation */
    uint8_t  sf, join_sf;
    uint32_t join_failures_remaining;   /* fail this many joins, then succeed; UINT32_MAX = never */
    bool     send_ok;
    bool     time_valid;
    uint32_t unix_offset;
    /* observations */
    uint32_t joins_attempted, uplinks, sleeps;
    uint8_t  last_payload[32];
    uint8_t  last_payload_len;
    uint32_t eink_full, eink_partial;
    bool     eink_asleep;
    uint32_t slept_with_panel_awake;    /* must stay 0 (design doc §8.4) */
    uint8_t  glass[EINK_FB_BYTES];      /* what the panel is showing */
    /* energy accounting, micro-amp-hours (design doc §4.2 figures) */
    double   uah_sleep, uah_uplink, uah_join, uah_sensor, uah_display;
} sim_t;

extern sim_t sim;

void sim_reset_board(void);      /* power-on: clears everything incl. backup regs */
void sim_reboot(void);           /* MCU reset: RTC + backup registers survive */
double sim_total_uah(void);
bool sim_write_pbm(const char *path);   /* landscape 296x128 image of the glass */

#endif
