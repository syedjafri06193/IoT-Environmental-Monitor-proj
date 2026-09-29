#ifndef SENSOR_H
#define SENSOR_H
/* BME280 temperature / humidity / pressure, forced (one-shot) mode only
 * (design doc §9.1). Normal mode draws hundreds of uA forever; forced mode
 * takes one sample (~10 ms at ~350 uA) and returns the part to sleep. */
#include <stdbool.h>
#include "reading.h"

#define BME280_ADDR        0x76u
#define BME280_CHIP_ID     0x60u

bool sensor_init(void);
/* Fills temp/humidity/pressure and sets r->sensor_ok. Leaves battery and
 * timestamp fields alone. */
bool sensor_read_forced(reading_t *r);
void sensor_sleep(void);

/* Exposed for unit tests: raw ADC values -> compensated fixed point. */
typedef struct {
    uint16_t T1; int16_t T2, T3;
    uint16_t P1; int16_t P2, P3, P4, P5, P6, P7, P8, P9;
    uint8_t  H1; int16_t H2; uint8_t H3; int16_t H4, H5; int8_t H6;
} bme280_calib_t;

void    bme280_set_calib(const bme280_calib_t *c);
int32_t bme280_compensate_t(int32_t adc_t);          /* degC x 100, sets t_fine */
uint32_t bme280_compensate_p(int32_t adc_p);         /* Pa x 256 (Q24.8) */
uint32_t bme280_compensate_h(int32_t adc_h);         /* %RH x 1024 (Q22.10) */

#endif
