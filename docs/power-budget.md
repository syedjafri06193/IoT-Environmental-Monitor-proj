# Power budget (milestones M0 → M9)

Target: 3× lithium AAA ≈ 1200 mAh over 6 months → **274 µA average**.

## 1. Calculated (design doc §4.2)

| | SF7 / 15 min | SF12 / hourly |
|---|---|---|
| Doc estimate, 182 days | 82 mAh (7%) | 121 mAh (10%) |

## 2. Simulated (firmware logic, calculated per-event energies)

`make -C firmware sim` runs the real `app.c` loop, refresh policy and join
logic against the host board model, charging each event with the §4.2
figures (5 µA sleep, 45 mA TX, 8.3/1.7 µAh full/partial refresh).

| Scenario (`./build/sim …`) | Uplinks/day | Display full/partial per week | Projected 6 months |
|---|---|---|---|
| `--sf 12` (TTN) | 18.9 | 10 / 115 | **95 mAh (8%)** |
| `--sf 7` (TTN) | 288 (5-min floor) | 41 / 491 | 122 mAh (10%) |
| `--sf 12 --limit 864000` (own gateway, 1%) | 288 | 41 / 491 | **1056 mAh (88%)** ⚠ |
| `--no-coverage` | 0 (12 joins, then ~4/day) | — | 46 mAh (4%) |
| `--cold` (−8 °C mean) | 18.7 | 0 / 0 (gated) | 88 mAh (7%) |

**Finding:** the design doc says running your own gateway "removes the
fair-use constraint entirely". True — and then the **battery** binds again.
At SF12 with only the 1% regulation limiting it, the 5-minute floor uses
88% of the pack with no margin for cold. If you self-host, raise
`MIN_INTERVAL_S` or accept that ADR must get the device to SF9 or better:

| Own gateway, SF12, `make sim DEFS=-DMIN_INTERVAL_S=…` | Uplinks/day | Projected 6 months |
|---|---|---|
| 300 s (default) | 288 | 1056 mAh (88%) ⚠ |
| 900 s | 96 | 387 mAh (32%) |
| 1800 s | 48 | 207 mAh (17%) |

## 3. Measured — fill in at M1/M3 (the numbers that count)

Capture with a PPK2 or Otii and run
`python3 tools/power_analysis.py trace.csv --uplinks-per-day 20`.

| Test (§16.1) | Target | Measured | Date / board |
|---|---|---|---|
| Bare board sleep (M1) | < 10 µA | | |
| Sleep, all peripherals attached | < 10 µA | | |
| Charge per uplink, SF7 | ~1.2 µAh ±20% | | |
| Charge per uplink, SF12 | ~19 µAh ±20% | | |
| Charge per full e-ink refresh | ~8.3 µAh ±20% | | |
| Full-cycle projection | ≥ 6 months with margin | | |

Re-run after every firmware change that touches sleep or peripherals.

## 4. Field (M9)

Battery voltage sampled mid-transmit is in every uplink and plotted on the
Grafana dashboard. A month on fresh cells in the real enclosure, plus a week
in a −15 °C freezer, produces the discharge curve that settles the claim.
