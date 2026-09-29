#include "payload.h"

static uint32_t clamp_u(int64_t v, int64_t lo, int64_t hi, bool *clamped)
{
    if (v < lo) { *clamped = true; return (uint32_t)lo; }
    if (v > hi) { *clamped = true; return (uint32_t)hi; }
    return (uint32_t)v;
}

bool payload_pack(const payload_fields_t *f, uint8_t out[PAYLOAD_SIZE])
{
    bool clamped = false;

    int32_t t = f->temp_cc;
    if (t < INT16_MIN) { t = INT16_MIN; clamped = true; }
    if (t > INT16_MAX) { t = INT16_MAX; clamped = true; }
    const uint16_t t_u = (uint16_t)(int16_t)t;

    const uint32_t rh = clamp_u(f->humidity_cp, 0, 10000, &clamped);
    const uint32_t p  = clamp_u((int64_t)f->pressure_pa - 80000, 0, 0xFFFF, &clamped);

    /* round to nearest 10 mV before range check */
    const int64_t bat_steps = ((int64_t)f->battery_mv - 2000 + 5) / 10;
    const uint32_t bat = clamp_u(f->battery_mv < 2000 ? -1 : bat_steps, 0, 255, &clamped);

    uint8_t sf_bits = 0;
    if (f->sf >= 7 && f->sf <= 12) sf_bits = (uint8_t)(f->sf - 7u);
    else clamped = true;

    uint8_t flags = sf_bits & FLAG_SF_MASK;
    if (f->joined)         flags |= FLAG_JOINED;
    if (f->sensor_error)   flags |= FLAG_SENSOR_ERROR;
    if (f->battery_loaded) flags |= FLAG_BATT_LOADED;
    if (clamped)           flags |= FLAG_CLAMPED;

    out[0] = PAYLOAD_VERSION;
    out[1] = (uint8_t)(t_u >> 8);  out[2] = (uint8_t)t_u;
    out[3] = (uint8_t)(rh >> 8);   out[4] = (uint8_t)rh;
    out[5] = (uint8_t)(p >> 8);    out[6] = (uint8_t)p;
    out[7] = (uint8_t)bat;
    out[8] = flags;
    return !clamped;
}

bool payload_unpack(const uint8_t *b, uint8_t len, payload_fields_t *f)
{
    if (len != PAYLOAD_SIZE || b[0] != PAYLOAD_VERSION) return false;
    f->temp_cc        = (int16_t)(((uint16_t)b[1] << 8) | b[2]);
    f->humidity_cp    = ((uint32_t)b[3] << 8) | b[4];
    f->pressure_pa    = (((uint32_t)b[5] << 8) | b[6]) + 80000u;
    f->battery_mv     = (uint16_t)(2000u + 10u * b[7]);
    f->sf             = (uint8_t)(7u + (b[8] & FLAG_SF_MASK));
    f->joined         = (b[8] & FLAG_JOINED) != 0;
    f->sensor_error   = (b[8] & FLAG_SENSOR_ERROR) != 0;
    f->clamped        = (b[8] & FLAG_CLAMPED) != 0;
    f->battery_loaded = (b[8] & FLAG_BATT_LOADED) != 0;
    return true;
}

payload_fields_t payload_from_reading(const reading_t *r, uint8_t sf, bool joined)
{
    payload_fields_t f = {
        .temp_cc        = r->temp_cc,
        .humidity_cp    = r->humidity_cp,
        .pressure_pa    = r->pressure_pa,
        .battery_mv     = r->battery_mv,
        .sf             = sf,
        .joined         = joined,
        .sensor_error   = !r->sensor_ok,
        .battery_loaded = r->battery_loaded,
    };
    if (!r->sensor_ok) {      /* send zeros rather than stale-looking values */
        f.temp_cc = 0; f.humidity_cp = 0; f.pressure_pa = 80000u;
    }
    return f;
}
