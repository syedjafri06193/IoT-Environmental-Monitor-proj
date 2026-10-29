# IoT Environmental Monitor

[![CI](https://github.com/syedjafri06193/IoT-Environmental-Monitor-proj/actions/workflows/ci.yml/badge.svg)](https://github.com/syedjafri06193/IoT-Environmental-Monitor-proj/actions/workflows/ci.yml)

Battery-powered sensor node with LoRaWAN uplink, e-ink display, and MQTT
bridge — designed for 6 months on 3× lithium AAA cells.

The design rationale is in [`1—Documentation/design-guide.md`](1—Documentation/design-guide.md).
Its central finding drives the whole codebase: **airtime, not battery, is the
binding constraint.** On TTN's public network a device gets 30 s of uplink
airtime a day — at SF12 that's **20 messages/day** with this 9-byte payload —
and the firmware has to enforce that itself.

```
BME280 ─I²C─┐                                              ┌─> InfluxDB ─> Grafana
            STM32WL ──LoRa──> gateway ─> ChirpStack ─MQTT─> bridge
e-ink ──SPI─┘  (superloop + STOP2, ~5 µA)                  └─> alerts (silence, battery, sensor)
```

## What's here

| Path | Contents |
|---|---|
| `firmware/src` | Hardware-independent application: superloop, fair-use airtime accounting, join backoff, packed payload, BME280 forced-mode driver, SSD1680 e-ink driver + refresh policy, sleep discipline |
| `firmware/platform/stm32wl` | STM32WL + STM32CubeWL/LoRaMAC-node port and pin map ([integration guide](firmware/platform/stm32wl/README.md)) |
| `firmware/platform/host` | Simulated board: fake BME280, e-ink frame capture, energy model — runs the real firmware on a PC |
| `firmware/tests` | C unit + end-to-end tests |
| `decoder/decoder.js` | TTN/ChirpStack payload formatter, tested against vectors from the C encoder |
| `server/` | Docker Compose stack: Mosquitto, ChirpStack v4, gateway bridge, InfluxDB, Grafana, and the Python MQTT→InfluxDB bridge with silence alerting |
| `tools/` | `airtime_calc.py`, `power_analysis.py` (PPK2/Otii trace → mAh), `bom_check.py`, `gen_font.py` |
| `hardware/bom.csv` | BOM with sleep current as a reviewed column |
| `1—Documentation/` | Design guide, airtime and power budgets (M0), enclosure requirements |

## Quick start

```sh
make test        # C tests, payload vectors, JS decoder round-trip, Python tests, BOM check
make sim         # build the simulator
firmware/build/sim --days 14 --sf 12 --pbm display.pbm
```

```
Simulated 14.0 days at SF12, fair-use limit 30000 ms/24h
  uplinks sent         265  (18.9/day, 0 blocked by budget)
  airtime per uplink   1483 ms   peak window usage 28177 ms of 30000
  display              20 full, 230 partial, 0 skipped (cold)
  panel awake at sleep 0   (must be 0)
  projected 6 months   95 mAh = 8% of 1200 mAh
```

Server stack:

```sh
cd server && cp .env.example .env   # set secrets
docker compose up -d
```

Then in ChirpStack (http://localhost:8080) add the gateway, a device profile
with **`decoder/decoder.js` as its codec**, and the device's OTAA keys.

## Payload v1 (9 bytes, big-endian)

| Byte | Field | Encoding |
|---|---|---|
| 0 | version | `0x01` |
| 1–2 | temperature | int16, °C × 100 |
| 3–4 | humidity | uint16, %RH × 100 |
| 5–6 | pressure | uint16, (hPa − 800) × 100 → 800.00–1455.35 hPa |
| 7 | battery | uint8, (V − 2.0) × 100, sampled mid-transmit |
| 8 | flags | bits 0–2 SF−7 · 3 joined · 4 sensor error · 5 clamped · 6 battery under load |

Out-of-range values are clamped (never wrapped) and flagged.

## How the design doc's rules map to code

| Rule | Where |
|---|---|
| Track fair-use airtime on-device, persist across resets | `firmware/src/airtime.c` (backup registers, checksum) |
| Derive the interval from remaining budget | `airtime_next_interval_s()` |
| Joins are uplinks too; exponential backoff, dormant fallback | `firmware/src/lorawan.c` |
| Unconfirmed uplinks only | `lorawan_port_*.c` |
| Battery voltage under transmit load | `lw_port_last_tx_battery_mv()` |
| E-ink: refresh on meaningful change, full every 12 partials, gate on temperature, **always deep sleep** | `firmware/src/display.c`, `eink.c` |
| Every peripheral asleep, every GPIO defined before STOP2 | `firmware/src/power.c`, `hal_gpio_config_for_sleep()` |
| Alert on silence, not just values | `server/bridge/bridge.py` |

## Status against the milestone ladder

| Milestone | State |
|---|---|
| M0 budgets | Done — [`1—Documentation/airtime-budget.md`](1—Documentation/airtime-budget.md), [`1—Documentation/power-budget.md`](1—Documentation/power-budget.md) |
| M2 sensor + payload, M4 airtime accounting, M5 e-ink logic, M6 network + dashboard | Code complete and tested on host |
| M1 sleep current, M3 LoRaWAN on hardware | STM32WL port written; **needs a board and a PPK2** |
| M7 PCB, M8 enclosure, M9 battery validation | Hardware work; requirements in `hardware/` and [`1—Documentation/enclosure.md`](1—Documentation/enclosure.md) |

Two deliberate departures from the doc, both explained in
[`1—Documentation/airtime-budget.md`](1—Documentation/airtime-budget.md) and
[`firmware/include/display.h`](firmware/include/display.h): the SF12 reference
airtime is 1482.8 ms for this payload (the doc's 1318.9 ms is for 7 bytes), and
pressure uses 0.01 hPa steps so the field actually spans the doc's stated
800–1455 hPa. The display shows an absolute reading time rather than
"N min ago", because an e-ink image outlives the refreshes that would update it.

<!-- project-metrics:start -->
## Project Metrics

[![Stars](https://img.shields.io/github/stars/syedjafri06193/IoT-Environmental-Monitor-proj?style=flat-square)](https://github.com/syedjafri06193/IoT-Environmental-Monitor-proj/stargazers) [![Forks](https://img.shields.io/github/forks/syedjafri06193/IoT-Environmental-Monitor-proj?style=flat-square)](https://github.com/syedjafri06193/IoT-Environmental-Monitor-proj/forks) [![Open issues](https://img.shields.io/github/issues/syedjafri06193/IoT-Environmental-Monitor-proj?style=flat-square)](https://github.com/syedjafri06193/IoT-Environmental-Monitor-proj/issues) [![Last commit](https://img.shields.io/github/last-commit/syedjafri06193/IoT-Environmental-Monitor-proj/improvements?style=flat-square)](https://github.com/syedjafri06193/IoT-Environmental-Monitor-proj/commits/improvements) [![Commits](https://img.shields.io/github/commit-activity/t/syedjafri06193/IoT-Environmental-Monitor-proj?style=flat-square)](https://github.com/syedjafri06193/IoT-Environmental-Monitor-proj/commits/improvements) [![Top language](https://img.shields.io/github/languages/top/syedjafri06193/IoT-Environmental-Monitor-proj?style=flat-square)](https://github.com/syedjafri06193/IoT-Environmental-Monitor-proj) [![Repo size](https://img.shields.io/github/repo-size/syedjafri06193/IoT-Environmental-Monitor-proj?style=flat-square)](https://github.com/syedjafri06193/IoT-Environmental-Monitor-proj)

### Code

| Metric | Value |
|---|---|
| Lines of code | 2,934 |
| Source files | 55 |
| Tracked files | 74 |
| Documentation | 9 Markdown files (11,105 words), 1 PDF |
| Commits | 7 |
| Contributors | 1 |
| Active | 2026-09-11 → 2026-10-28 |

| Language | Lines | Share |
|---|---:|---:|
| C | 1,988 | 67.8% |
| Python | 638 | 21.7% |
| YAML | 109 | 3.7% |
| JavaScript | 83 | 2.8% |
| TOML | 71 | 2.4% |
| Makefile | 38 | 1.3% |

### Performance

| Metric | Result |
|---|---|
| 14-day simulation, LoRaWAN SF12 | 18.9 uplinks/day, 0 blocked by duty budget |
| Projected 6-month battery use | **95 mAh = 8%** of 1200 mAh |
| Airtime per uplink | 1483 ms |

<sub>Simulated figures (from the project's own simulator), recorded in [`README.md`](README.md).</sub>

<sub>Code stats as of 2026-10-28, counted with pygount (comments and blank lines excluded). Badges update live.</sub>
<!-- project-metrics:end -->
