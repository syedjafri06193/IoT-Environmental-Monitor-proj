import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from bridge import Alerter, Uplink, parse_uplink, to_line_protocol  # noqa: E402

DECODED = {"version": 1, "temperature": 21.4, "humidity": 62.0, "pressure": 1013.2,
           "battery": 4.1, "sf": 12, "joined": True, "sensor_error": False,
           "clamped": False, "battery_under_load": True}

CHIRPSTACK = {
    "deduplicationId": "x", "time": "2026-09-29T12:00:00.123456789Z",
    "deviceInfo": {"devEui": "0080e1150000abcd", "deviceName": "garden north"},
    "fCnt": 42, "fPort": 1, "data": "AQkpF+hTSNQN", "object": DECODED,
    "rxInfo": [{"rssi": -118, "snr": -12.5}, {"rssi": -101, "snr": 3.0}],
    "txInfo": {"frequency": 868100000, "modulation": {"lora": {"bandwidth": 125000, "spreadingFactor": 12}}},
}

TTN = {
    "end_device_ids": {"device_id": "env-1", "dev_eui": "0080E1150000ABCD"},
    "received_at": "2026-09-29T12:00:00Z",
    "uplink_message": {"f_cnt": 7, "decoded_payload": DECODED,
                       "rx_metadata": [{"rssi": -110, "snr": -5}],
                       "settings": {"data_rate": {"lora": {"spreading_factor": 12}}}},
}


def test_parse_chirpstack():
    u = parse_uplink("application/abc/device/0080e1150000abcd/event/up", json.dumps(CHIRPSTACK))
    assert u.dev_eui == "0080e1150000abcd"
    assert u.fields["temperature"] == 21.4
    assert u.rssi == -101 and u.snr == 3.0          # best gateway
    assert u.sf == 12 and u.f_cnt == 42 and u.gateways == 2
    assert abs(u.ts - 1790683200.123) < 0.01


def test_parse_ttn():
    u = parse_uplink("v3/app@ttn/devices/env-1/up", json.dumps(TTN))
    assert u.device_name == "env-1" and u.sf == 12 and u.rssi == -110


def test_missing_codec_is_skipped():
    m = dict(CHIRPSTACK)
    del m["object"]
    assert parse_uplink("application/a/device/b/event/up", json.dumps(m)) is None


def test_line_protocol():
    u = parse_uplink("application/abc/device/x/event/up", json.dumps(CHIRPSTACK))
    line = to_line_protocol(u)
    assert line.startswith("environment,dev_eui=0080e1150000abcd,device=garden\\ north ")
    assert "temperature=21.4" in line and "sf=12i" in line and "joined=true" in line
    assert "rssi=-101.0" in line and line.endswith(" 1790683200123456000")


def test_sensor_error_nulls_not_written():
    u = parse_uplink("application/a/device/b/event/up", json.dumps(
        dict(CHIRPSTACK, object=dict(DECODED, temperature=None, humidity=None, pressure=None, sensor_error=True))))
    line = to_line_protocol(u)
    assert "temperature" not in line and "sensor_error=true" in line


def make_alerter():
    events = []
    a = Alerter(lambda d, k, active, m: events.append((d, k, active)), default_interval_s=4320)
    return a, events


def up(ts, **fields):
    return Uplink(dev_eui="d", device_name="", ts=ts, fields=dict(DECODED, **fields))


def test_silence_alert_fires_once_and_resolves():
    a, ev = make_alerter()
    for i in range(5):
        a.on_uplink(up(1000 + i * 3600))           # hourly
    last = 1000 + 4 * 3600
    a.check_silence(now=last + 3 * 3600 - 1)
    assert ev == []
    a.check_silence(now=last + 3 * 3600 + 1)
    a.check_silence(now=last + 5 * 3600)
    assert ev == [("d", "silence", True)]
    a.on_uplink(up(last + 6 * 3600))
    assert ev[-1] == ("d", "silence", False)


def test_silence_uses_default_before_history():
    a, ev = make_alerter()
    a.on_uplink(up(0))
    a.check_silence(now=3 * 4320 - 10)
    assert ev == []
    a.check_silence(now=3 * 4320 + 10)
    assert ev == [("d", "silence", True)]


def test_battery_and_sensor_alerts():
    a, ev = make_alerter()
    a.on_uplink(up(0, battery=3.2))
    a.on_uplink(up(3600, battery=3.25, sensor_error=True))
    a.on_uplink(up(7200, battery=4.0, sensor_error=False))
    assert ev == [("d", "battery", True), ("d", "sensor", True),
                  ("d", "battery", False), ("d", "sensor", False)]
