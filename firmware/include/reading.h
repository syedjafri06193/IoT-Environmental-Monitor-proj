#ifndef READING_H
#define READING_H
#include <stdbool.h>
#include <stdint.h>

/* One sensor sample, in integer fixed point throughout. */
typedef struct {
    int32_t  temp_cc;        /* degC x 100 */
    uint32_t humidity_cp;    /* %RH x 100 */
    uint32_t pressure_pa;    /* Pa (= hPa x 100) */
    uint16_t battery_mv;
    bool     battery_loaded; /* battery_mv was sampled during a transmit */
    bool     sensor_ok;
    uint32_t taken_at_s;     /* RTC seconds */
} reading_t;

#endif
