// Round-trip: vectors are produced by the C encoder (firmware/tests/test_payload.c)
// and decoded here. A decoder that drifts from the encoder fails this test.
//
//   make -C firmware test && node --test decoder/tests/
const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { decodeUplink, Decode } = require("../decoder.js");

const vectorsPath = process.env.PAYLOAD_VECTORS ||
  path.join(__dirname, "..", "..", "firmware", "build", "payload_vectors.json");

test("C encoder vectors decode identically", () => {
  const vectors = JSON.parse(fs.readFileSync(vectorsPath, "utf8"));
  assert.ok(vectors.length >= 5);
  for (const v of vectors) {
    const r = decodeUplink({ fPort: 1, bytes: v.bytes });
    assert.equal(r.errors, undefined, JSON.stringify(r.errors));
    const d = r.data;
    if (v.sensor_error) {
      assert.equal(d.temperature, null);
      assert.equal(d.humidity, null);
      assert.equal(d.pressure, null);
    } else {
      assert.equal(Math.round(d.temperature * 100), v.temp_cc);
      assert.equal(Math.round(d.humidity * 100), v.humidity_cp);
      assert.equal(Math.round(d.pressure * 100), v.pressure_pa);
    }
    assert.equal(Math.round(d.battery * 1000), v.battery_mv);
    assert.equal(d.sf, v.sf);
    assert.equal(d.joined, v.joined);
    assert.equal(d.sensor_error, v.sensor_error);
    assert.equal(d.battery_under_load, v.battery_loaded);
    assert.equal(d.clamped, v.clamped);
    if (v.clamped) assert.ok(r.warnings.some((w) => w.includes("clamped")));
  }
});

test("known bytes from the spec", () => {
  const r = decodeUplink({ bytes: [0x01, 0x09, 0x29, 0x17, 0xe8, 0x53, 0x48, 0xd4, 0x0d] });
  assert.deepEqual(r.data, {
    version: 1, temperature: 23.45, humidity: 61.2, pressure: 1013.2, battery: 4.12,
    sf: 12, joined: true, sensor_error: false, clamped: false, battery_under_load: false,
  });
});

test("negative temperature sign-extends", () => {
  const r = decodeUplink({ bytes: [1, 0xff, 0x38, 0, 0, 0, 0, 0, 0] });
  assert.equal(r.data.temperature, -2);
});

test("rejects unknown version, empty and wrong length", () => {
  assert.match(decodeUplink({ bytes: [2, 0, 0, 0, 0, 0, 0, 0, 0] }).errors[0], /unsupported payload version 2/);
  assert.match(decodeUplink({ bytes: [] }).errors[0], /empty/);
  assert.match(decodeUplink({ bytes: [1, 2, 3] }).errors[0], /9 bytes/);
});

test("ChirpStack v3 Decode() entry point", () => {
  const d = Decode(1, [0x01, 0x09, 0x29, 0x17, 0xe8, 0x53, 0x48, 0xd4, 0x0d]);
  assert.equal(d.temperature, 23.45);
});
