#include <stdio.h>
#include <string.h>
#include "payload.h"
#include "unit.h"

static payload_fields_t base(void)
{
    payload_fields_t f = { .temp_cc = 2345, .humidity_cp = 6120, .pressure_pa = 101320,
                           .battery_mv = 4120, .sf = 12, .joined = true };
    return f;
}

static void roundtrip(const payload_fields_t *in, payload_fields_t *out, bool expect_ok)
{
    uint8_t buf[PAYLOAD_SIZE];
    CHECK_EQ(payload_pack(in, buf), expect_ok);
    CHECK(payload_unpack(buf, PAYLOAD_SIZE, out));
}

static void test_size_under_ttn_guidance(void)
{
    CHECK_EQ(PAYLOAD_SIZE, 9);
    CHECK(PAYLOAD_SIZE < 12);
}

static void test_known_bytes(void)
{
    payload_fields_t f = base();
    uint8_t b[PAYLOAD_SIZE];
    CHECK(payload_pack(&f, b));
    const uint8_t want[PAYLOAD_SIZE] = { 0x01, 0x09, 0x29, 0x17, 0xE8, 0x53, 0x48, 0xD4, 0x0D };
    CHECK(memcmp(b, want, sizeof want) == 0);
}

static void test_roundtrip_typical(void)
{
    payload_fields_t in = base(), out;
    roundtrip(&in, &out, true);
    CHECK_EQ(out.temp_cc, 2345);
    CHECK_EQ(out.humidity_cp, 6120);
    CHECK_EQ(out.pressure_pa, 101320);
    CHECK_EQ(out.battery_mv, 4120);
    CHECK_EQ(out.sf, 12);
    CHECK(out.joined && !out.sensor_error && !out.clamped);
}

static void test_edges_survive(void)
{
    payload_fields_t in = base(), out;
    in.temp_cc = -32768; in.humidity_cp = 0; in.pressure_pa = 80000; in.battery_mv = 2000; in.sf = 7;
    roundtrip(&in, &out, true);
    CHECK_EQ(out.temp_cc, -32768); CHECK_EQ(out.humidity_cp, 0);
    CHECK_EQ(out.pressure_pa, 80000); CHECK_EQ(out.battery_mv, 2000); CHECK_EQ(out.sf, 7);

    in.temp_cc = 32767; in.humidity_cp = 10000; in.pressure_pa = 145535; in.battery_mv = 4550;
    roundtrip(&in, &out, true);
    CHECK_EQ(out.temp_cc, 32767); CHECK_EQ(out.humidity_cp, 10000);
    CHECK_EQ(out.pressure_pa, 145535);             /* 1455.35 hPa survives */
    CHECK_EQ(out.battery_mv, 4550);
    CHECK(!out.clamped);
}

static void test_out_of_range_clamps_not_wraps(void)
{
    payload_fields_t in = base(), out;
    in.pressure_pa = 145600;                        /* 1456 hPa */
    roundtrip(&in, &out, false);
    CHECK(out.clamped);
    CHECK_EQ(out.pressure_pa, 145535);             /* not 800.65 hPa */

    in = base(); in.pressure_pa = 79000;
    roundtrip(&in, &out, false);
    CHECK_EQ(out.pressure_pa, 80000);

    in = base(); in.temp_cc = -40000;
    roundtrip(&in, &out, false);
    CHECK_EQ(out.temp_cc, -32768);

    in = base(); in.battery_mv = 5000;
    roundtrip(&in, &out, false);
    CHECK_EQ(out.battery_mv, 4550);

    in = base(); in.battery_mv = 1800;
    roundtrip(&in, &out, false);
    CHECK_EQ(out.battery_mv, 2000);

    in = base(); in.humidity_cp = 10500;
    roundtrip(&in, &out, false);
    CHECK_EQ(out.humidity_cp, 10000);
}

static void test_flags(void)
{
    payload_fields_t in = base(), out;
    in.sensor_error = true; in.battery_loaded = true; in.joined = false; in.sf = 9;
    roundtrip(&in, &out, true);
    CHECK(out.sensor_error && out.battery_loaded && !out.joined);
    CHECK_EQ(out.sf, 9);
}

static void test_rejects_unknown_version_and_length(void)
{
    uint8_t b[PAYLOAD_SIZE];
    payload_fields_t f = base(), out;
    payload_pack(&f, b);
    CHECK(!payload_unpack(b, PAYLOAD_SIZE - 1, &out));
    b[0] = 2;
    CHECK(!payload_unpack(b, PAYLOAD_SIZE, &out));
}

static void test_battery_rounds_to_10mv(void)
{
    payload_fields_t in = base(), out;
    in.battery_mv = 4126;
    roundtrip(&in, &out, true);
    CHECK_EQ(out.battery_mv, 4130);
}

/* Emits vectors for decoder/tests/roundtrip.test.js: the C encoder is the
 * source of truth the JS decoder must agree with (design doc §16.3). */
static void write_vectors(const char *path)
{
    static const payload_fields_t cases[] = {
        { 2345, 6120, 101320, 4120, 12, true, false, false, false },
        { -32768, 0, 80000, 2000, 7, true, false, false, false },
        { 32767, 10000, 145535, 4550, 12, true, false, true, false },
        { -1234, 9999, 99999, 3310, 10, false, false, false, false },
        { 0, 0, 80000, 3000, 8, true, true, false, false },
        { 2000, 5000, 145600, 4000, 9, true, false, false, false },   /* clamps */
        { -5, 1, 101325, 2004, 11, true, false, true, false },
    };
    FILE *fp = fopen(path, "w");
    if (!fp) { perror(path); unit_failures++; return; }
    fprintf(fp, "[\n");
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        uint8_t b[PAYLOAD_SIZE];
        payload_fields_t out;
        payload_pack(&cases[i], b);
        payload_unpack(b, PAYLOAD_SIZE, &out);
        fprintf(fp, "  {\"bytes\": [");
        for (unsigned j = 0; j < PAYLOAD_SIZE; j++) fprintf(fp, "%s%u", j ? "," : "", b[j]);
        fprintf(fp, "], \"temp_cc\": %ld, \"humidity_cp\": %lu, \"pressure_pa\": %lu, "
                    "\"battery_mv\": %u, \"sf\": %u, \"joined\": %s, \"sensor_error\": %s, "
                    "\"battery_loaded\": %s, \"clamped\": %s}%s\n",
                (long)out.temp_cc, (unsigned long)out.humidity_cp, (unsigned long)out.pressure_pa,
                out.battery_mv, out.sf, out.joined ? "true" : "false",
                out.sensor_error ? "true" : "false", out.battery_loaded ? "true" : "false",
                out.clamped ? "true" : "false", i + 1 < sizeof cases / sizeof cases[0] ? "," : "");
    }
    fprintf(fp, "]\n");
    fclose(fp);
}

int main(int argc, char **argv)
{
    printf("test_payload\n");
    RUN(test_size_under_ttn_guidance);
    RUN(test_known_bytes);
    RUN(test_roundtrip_typical);
    RUN(test_edges_survive);
    RUN(test_out_of_range_clamps_not_wraps);
    RUN(test_flags);
    RUN(test_rejects_unknown_version_and_length);
    RUN(test_battery_rounds_to_10mv);
    if (argc > 1) write_vectors(argv[1]);
    UNIT_DONE("test_payload");
}
