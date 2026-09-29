// LoRaWAN payload decoder for the IoT Environmental Monitor.
//
// Paste into TTN ("Payload formatters -> Uplink -> Custom Javascript") or
// ChirpStack ("Device profile -> Codec -> JavaScript functions").
//
// This file is the other half of firmware/include/payload.h. The two are
// versioned together; tests/roundtrip.test.js decodes vectors produced by the
// C encoder so any drift fails the build.

var PAYLOAD_SIZE_V1 = 9;

function decodeV1(b) {
  if (b.length !== PAYLOAD_SIZE_V1) {
    return { errors: ["v1 payload must be 9 bytes, got " + b.length] };
  }
  var t = (b[1] << 8) | b[2];
  if (t & 0x8000) t -= 0x10000;
  var flags = b[8];
  var data = {
    version: 1,
    temperature: t / 100,                                  // degC
    humidity: ((b[3] << 8) | b[4]) / 100,                  // %RH
    pressure: Math.round((((b[5] << 8) | b[6]) + 80000)) / 100, // hPa
    battery: Math.round(2000 + 10 * b[7]) / 1000,          // V
    sf: 7 + (flags & 0x07),
    joined: !!(flags & 0x08),
    sensor_error: !!(flags & 0x10),
    clamped: !!(flags & 0x20),
    battery_under_load: !!(flags & 0x40),
  };
  var warnings = [];
  if (data.sensor_error) {
    // Firmware sends zeros on sensor failure - don't let them look like data.
    data.temperature = null;
    data.humidity = null;
    data.pressure = null;
    warnings.push("sensor error");
  }
  if (data.clamped) warnings.push("a field was out of range and clamped");
  return warnings.length ? { data: data, warnings: warnings } : { data: data };
}

function decodeUplink(input) {
  var b = input.bytes;
  if (!b || b.length < 1) return { errors: ["empty payload"] };
  switch (b[0]) {
    case 1:
      return decodeV1(b);
    default:
      return { errors: ["unsupported payload version " + b[0]] };
  }
}

// ChirpStack v3 entry point.
function Decode(fPort, bytes) {
  var r = decodeUplink({ fPort: fPort, bytes: bytes });
  return r.data || { errors: r.errors };
}

if (typeof module !== "undefined") {
  module.exports = { decodeUplink: decodeUplink, Decode: Decode };
}
