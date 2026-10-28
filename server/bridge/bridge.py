#!/usr/bin/env python3
"""MQTT -> InfluxDB bridge with silence and battery alerting (design doc §10.4).

Subscribes to ChirpStack v4 (`application/+/device/+/event/up`) and/or TTN v3
(`v3/+/devices/+/up`) uplinks, which the network server has already decoded
with decoder/decoder.js, writes them to InfluxDB 2.x, and raises alerts:

  * silence   - no uplink in SILENCE_FACTOR x the device's typical interval.
                A threshold alert on temperature never fires for a dead sensor.
  * battery   - under-load voltage below BATTERY_LOW_V.
  * sensor    - the device reported a sensor error.
  * clamped   - a value was out of the payload's range.

Alerts are published to MQTT `monitor/alerts/<dev_eui>` (retained) and
optionally POSTed to ALERT_WEBHOOK_URL. Configuration is by environment
variable; see server/.env.example.
"""
import json
import math
import logging
import os
import signal
import statistics
import sys
import threading
import time
import urllib.error
import urllib.request
from collections import deque
from dataclasses import dataclass, field
from typing import Callable, Optional

log = logging.getLogger("bridge")


# ---------------------------------------------------------------- parsing
@dataclass
class Uplink:
    dev_eui: str
    device_name: str
    ts: float                      # unix seconds
    fields: dict                   # decoded measurement fields
    rssi: Optional[float] = None
    snr: Optional[float] = None
    sf: Optional[int] = None
    f_cnt: Optional[int] = None
    gateways: int = 0


MEASUREMENT_KEYS = ("temperature", "humidity", "pressure", "battery")
FLAG_KEYS = ("sf", "joined", "sensor_error", "clamped", "battery_under_load")


def _parse_time(s):
    if not s:
        return time.time()
    from datetime import datetime
    s = s.replace("Z", "+00:00")
    # trim sub-microsecond precision that fromisoformat rejects
    if "." in s:
        head, rest = s.split(".", 1)
        frac = ""
        i = 0
        while i < len(rest) and rest[i].isdigit():
            frac += rest[i]
            i += 1
        s = head + "." + frac[:6] + rest[i:]
    return datetime.fromisoformat(s).timestamp()


def _best_rx(rx):
    if not rx:
        return None, None
    best = max(rx, key=lambda r: r.get("rssi", -999))
    return best.get("rssi"), best.get("snr", best.get("loRaSNR"))


def parse_uplink(topic, payload):
    """Returns an Uplink, or None for messages without a decoded object.

    Raises ValueError for anything malformed, so one bad message cannot
    escape the MQTT callback (paho re-raises callback exceptions, which
    stops the network loop and with it the whole bridge)."""
    try:
        return _parse_uplink(topic, payload)
    except (ValueError, KeyError, TypeError, AttributeError) as e:
        raise ValueError("malformed uplink: %s" % e) from e


def _parse_uplink(topic, payload):
    msg = json.loads(payload)
    if not isinstance(msg, dict):
        raise ValueError("expected a JSON object")

    if topic.startswith("application/"):                     # ChirpStack v4
        info = msg.get("deviceInfo", {})
        obj = msg.get("object")
        if obj is not None and not isinstance(obj, dict):
            raise ValueError("decoded object is not a JSON object")
        if obj is None:
            log.warning("uplink from %s has no decoded object - is the codec "
                        "(decoder/decoder.js) set on the device profile?", info.get("devEui"))
            return None
        rssi, snr = _best_rx(msg.get("rxInfo", []))
        sf = (msg.get("txInfo", {}).get("modulation", {}).get("lora", {}) or {}).get("spreadingFactor")
        return Uplink(
            dev_eui=info.get("devEui", "unknown"),
            device_name=info.get("deviceName", ""),
            ts=_parse_time(msg.get("time")),
            fields=obj, rssi=rssi, snr=snr, sf=sf or obj.get("sf"),
            f_cnt=msg.get("fCnt"), gateways=len(msg.get("rxInfo", [])))

    if topic.startswith("v3/"):                              # TTN / The Things Stack
        ids = msg.get("end_device_ids", {})
        up = msg.get("uplink_message", {})
        obj = up.get("decoded_payload")
        if obj is not None and not isinstance(obj, dict):
            raise ValueError("decoded_payload is not a JSON object")
        if obj is None:
            log.warning("uplink from %s has no decoded_payload", ids.get("dev_eui"))
            return None
        rssi, snr = _best_rx(up.get("rx_metadata", []))
        sf = up.get("settings", {}).get("data_rate", {}).get("lora", {}).get("spreading_factor")
        return Uplink(
            dev_eui=ids.get("dev_eui", "unknown"),
            device_name=ids.get("device_id", ""),
            ts=_parse_time(up.get("received_at") or msg.get("received_at")),
            fields=obj, rssi=rssi, snr=snr, sf=sf or obj.get("sf"),
            f_cnt=up.get("f_cnt"), gateways=len(up.get("rx_metadata", [])))

    return None


# ---------------------------------------------------------------- influx
def _esc_tag(v):
    # Line protocol is newline-delimited: a newline in a tag (a device name is
    # user-editable in ChirpStack/TTN) would start a new, attacker-shaped line.
    v = str(v).replace("\r", " ").replace("\n", " ")
    return v.replace("\\", "\\\\").replace(",", "\\,").replace("=", "\\=").replace(" ", "\\ ")


def to_line_protocol(u: Uplink, measurement="environment"):
    fields = {}
    for k in MEASUREMENT_KEYS:
        v = u.fields.get(k)
        # NaN/inf are not representable in line protocol; one would make
        # InfluxDB reject the whole write, losing the good fields with it.
        if isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v):
            fields[k] = float(v)
    for k in FLAG_KEYS:
        v = u.fields.get(k)
        if isinstance(v, bool):
            fields[k] = v
        elif isinstance(v, (int, float)):
            fields[k] = int(v)
    if u.sf is not None:
        fields["sf"] = int(u.sf)
    if u.rssi is not None and math.isfinite(float(u.rssi)):
        fields["rssi"] = float(u.rssi)
    if u.snr is not None and math.isfinite(float(u.snr)):
        fields["snr"] = float(u.snr)
    if u.f_cnt is not None:
        fields["f_cnt"] = int(u.f_cnt)
    fields["gateways"] = int(u.gateways)

    def fmt(v):
        if isinstance(v, bool):
            return "true" if v else "false"
        if isinstance(v, int):
            return "%di" % v
        return repr(float(v))

    tags = "dev_eui=%s" % _esc_tag(u.dev_eui)
    if u.device_name:
        tags += ",device=%s" % _esc_tag(u.device_name)
    body = ",".join("%s=%s" % (k, fmt(v)) for k, v in sorted(fields.items()))
    return "%s,%s %s %d" % (measurement, tags, body, int(u.ts * 1e9))


class InfluxWriter:
    def __init__(self, url, org, bucket, token, timeout=5.0):
        self.endpoint = "%s/api/v2/write?org=%s&bucket=%s&precision=ns" % (
            url.rstrip("/"), urllib.request.quote(org), urllib.request.quote(bucket))
        self.token = token
        self.timeout = timeout

    def write(self, lines):
        req = urllib.request.Request(self.endpoint, data="\n".join(lines).encode(), method="POST")
        req.add_header("Authorization", "Token %s" % self.token)
        req.add_header("Content-Type", "text/plain; charset=utf-8")
        with urllib.request.urlopen(req, timeout=self.timeout) as r:
            if r.status >= 300:
                raise RuntimeError("influx write failed: HTTP %d" % r.status)


# ---------------------------------------------------------------- alerting
@dataclass
class DeviceState:
    last_seen: float
    intervals: deque = field(default_factory=lambda: deque(maxlen=20))
    active: set = field(default_factory=set)
    last_fields: dict = field(default_factory=dict)


class Alerter:
    """Silence and value alerts. `emit(dev_eui, kind, active, message)`."""

    def __init__(self, emit: Callable, silence_factor=3.0, default_interval_s=4320.0,
                 min_interval_s=300.0, battery_low_v=3.3):
        self.emit = emit
        self.silence_factor = silence_factor
        self.default_interval_s = default_interval_s   # SF12 on TTN: 86400 / 20
        self.min_interval_s = min_interval_s
        self.battery_low_v = battery_low_v
        self.devices = {}
        self.lock = threading.Lock()

    def expected_interval(self, st: DeviceState):
        if len(st.intervals) >= 3:
            return max(statistics.median(st.intervals), self.min_interval_s)
        return self.default_interval_s

    def _set(self, dev, st, kind, active, message):
        if active and kind not in st.active:
            st.active.add(kind)
            self.emit(dev, kind, True, message)
        elif not active and kind in st.active:
            st.active.discard(kind)
            self.emit(dev, kind, False, message)

    def on_uplink(self, u: Uplink):
        with self.lock:
            st = self.devices.get(u.dev_eui)
            if st is None:
                st = self.devices[u.dev_eui] = DeviceState(last_seen=u.ts)
            else:
                gap = u.ts - st.last_seen
                if gap > 0:
                    st.intervals.append(gap)
                st.last_seen = max(st.last_seen, u.ts)
            st.last_fields = dict(u.fields)
            self._set(u.dev_eui, st, "silence", False, "reporting again")

            bat = u.fields.get("battery")
            if isinstance(bat, (int, float)):
                self._set(u.dev_eui, st, "battery", bat < self.battery_low_v,
                          "battery %.2f V (under load: %s), threshold %.2f V" % (
                              bat, u.fields.get("battery_under_load"), self.battery_low_v))
            self._set(u.dev_eui, st, "sensor", bool(u.fields.get("sensor_error")),
                      "sensor error reported" if u.fields.get("sensor_error") else "sensor ok")
            self._set(u.dev_eui, st, "clamped", bool(u.fields.get("clamped")),
                      "value out of payload range" if u.fields.get("clamped") else "values in range")

    def check_silence(self, now=None):
        now = time.time() if now is None else now
        with self.lock:
            for dev, st in self.devices.items():
                limit = self.silence_factor * self.expected_interval(st)
                silent = now - st.last_seen > limit
                if silent:
                    self._set(dev, st, "silence", True,
                              "no uplink for %.0f min (expected every %.0f min)" % (
                                  (now - st.last_seen) / 60, self.expected_interval(st) / 60))


# ---------------------------------------------------------------- runtime
def env(name, default=None):
    v = os.environ.get(name, default)
    if v is None:
        sys.exit("missing required environment variable %s" % name)
    return v


def main():
    import paho.mqtt.client as mqtt

    logging.basicConfig(level=os.environ.get("LOG_LEVEL", "INFO"),
                        format="%(asctime)s %(levelname)s %(message)s")
    topics = [t.strip() for t in env("MQTT_TOPICS", "application/+/device/+/event/up").split(",") if t.strip()]
    writer = InfluxWriter(env("INFLUX_URL", "http://influxdb:8086"), env("INFLUX_ORG", "monitor"),
                          env("INFLUX_BUCKET", "environment"), env("INFLUX_TOKEN"))
    webhook = os.environ.get("ALERT_WEBHOOK_URL")

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="env-monitor-bridge")
    if os.environ.get("MQTT_USERNAME"):
        client.username_pw_set(os.environ["MQTT_USERNAME"], os.environ.get("MQTT_PASSWORD"))

    def emit(dev, kind, active, message):
        state = "FIRING" if active else "resolved"
        log.warning("ALERT %s %s %s: %s", dev, kind, state, message)
        body = {"dev_eui": dev, "kind": kind, "active": active, "message": message, "ts": time.time()}
        client.publish("monitor/alerts/%s/%s" % (dev, kind), json.dumps(body), qos=1, retain=True)
        if webhook:
            try:
                req = urllib.request.Request(webhook, data=json.dumps(body).encode(), method="POST",
                                             headers={"Content-Type": "application/json"})
                urllib.request.urlopen(req, timeout=5).close()
            except (urllib.error.URLError, OSError) as e:
                log.error("webhook failed: %s", e)

    alerter = Alerter(emit,
                      silence_factor=float(env("SILENCE_FACTOR", "3")),
                      default_interval_s=float(env("EXPECTED_INTERVAL_S", "4320")),
                      battery_low_v=float(env("BATTERY_LOW_V", "3.3")))

    def on_connect(c, userdata, flags, reason, props=None):
        log.info("connected to MQTT (%s); subscribing %s", reason, topics)
        for t in topics:
            c.subscribe(t, qos=1)

    def on_message(c, userdata, m):
        try:
            u = parse_uplink(m.topic, m.payload)
        except (ValueError, KeyError) as e:
            log.error("bad message on %s: %s", m.topic, e)
            return
        if u is None:
            return
        alerter.on_uplink(u)
        try:
            writer.write([to_line_protocol(u)])
        except (urllib.error.URLError, OSError, RuntimeError) as e:
            log.error("influx write failed: %s", e)
        log.info("%s %s", u.dev_eui, {k: u.fields.get(k) for k in MEASUREMENT_KEYS + ("sf",)})

    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(env("MQTT_HOST", "mosquitto"), int(env("MQTT_PORT", "1883")))
    client.loop_start()

    stop = threading.Event()
    signal.signal(signal.SIGTERM, lambda *a: stop.set())
    signal.signal(signal.SIGINT, lambda *a: stop.set())
    while not stop.wait(60):
        alerter.check_silence()
    client.loop_stop()


if __name__ == "__main__":
    main()
