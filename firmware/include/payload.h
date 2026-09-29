#ifndef PAYLOAD_H
#define PAYLOAD_H
/* Uplink wire format, version 1 (design doc §7.2). 9 bytes, big-endian.
 *
 *  byte  field          encoding                        range
 *  0     version        0x01
 *  1-2   temperature    int16,  degC x 100              -327.68 .. 327.67 degC
 *  3-4   humidity       uint16, %RH x 100               0 .. 100.00 %RH
 *  5-6   pressure       uint16, (hPa - 800) x 100       800.00 .. 1455.35 hPa
 *  7     battery        uint8,  (V - 2.0) x 100         2.00 .. 4.55 V
 *  8     flags          bits 0-2  SF - 7
 *                       bit 3     joined
 *                       bit 4     sensor error
 *                       bit 5     a field was out of range and clamped
 *                       bit 6     battery sampled under transmit load
 *                       bit 7     reserved (0)
 *
 * decoder/decoder.js is the other half of this file. Change them together
 * and bump PAYLOAD_VERSION. */
#include <stdbool.h>
#include <stdint.h>
#include "reading.h"

#define PAYLOAD_VERSION 1u
#define PAYLOAD_SIZE    9u

#define FLAG_SF_MASK        0x07u
#define FLAG_JOINED         0x08u
#define FLAG_SENSOR_ERROR   0x10u
#define FLAG_CLAMPED        0x20u
#define FLAG_BATT_LOADED    0x40u

typedef struct {
    int32_t  temp_cc;
    uint32_t humidity_cp;
    uint32_t pressure_pa;
    uint16_t battery_mv;
    uint8_t  sf;
    bool     joined;
    bool     sensor_error;
    bool     battery_loaded;
    bool     clamped;       /* decode only */
} payload_fields_t;

/* Encodes into out[PAYLOAD_SIZE]. Values outside a field's range are clamped
 * (never wrapped) and FLAG_CLAMPED is set; returns false in that case. */
bool payload_pack(const payload_fields_t *f, uint8_t out[PAYLOAD_SIZE]);

/* Decodes; returns false for an unknown version or wrong length. Values come
 * back at wire resolution (pressure to 1 Pa, battery to 10 mV). */
bool payload_unpack(const uint8_t *buf, uint8_t len, payload_fields_t *f);

payload_fields_t payload_from_reading(const reading_t *r, uint8_t sf, bool joined);

#endif
