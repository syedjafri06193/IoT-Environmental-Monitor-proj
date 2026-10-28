# IoT Environmental Monitor — Design & Build Guide

**Project:** Battery-powered sensor node with LoRaWAN uplink, e-ink display, and MQTT bridge, 6-month battery life on AAA cells
**Status of this document:** planning + reference

---

## Table of contents

1. [Executive summary and scope](#1-executive-summary-and-scope)
2. [Reality check](#2-reality-check)
3. [The airtime budget](#3-the-airtime-budget)
4. [The power budget](#4-the-power-budget)
5. [Battery chemistry](#5-battery-chemistry)
6. [MCU and radio selection](#6-mcu-and-radio-selection)
7. [Payload design](#7-payload-design)
8. [The e-ink display](#8-the-e-ink-display)
9. [Sensors and enclosure](#9-sensors-and-enclosure)
10. [The network side](#10-the-network-side)
11. [Measure, don't calculate](#11-measure-dont-calculate)
12. [Tech stack and setup](#12-tech-stack-and-setup)
13. [Repository layout](#13-repository-layout)
14. [Milestone ladder](#14-milestone-ladder)
15. [Reference implementations](#15-reference-implementations)
16. [Testing](#16-testing)
17. [Stretch goals](#17-stretch-goals)
18. [References](#18-references)

---

## 1. Executive summary and scope

### The original statement

> Battery-powered sensor node with LoRaWAN uplink, e-ink display, and MQTT bridge, 6-month battery life on AAA cells.

Five findings reshape this, and the first one inverts what you'd expect:

1. **Airtime, not battery, is the binding constraint — and The Things Network's fair use policy is far stricter than the law.** EU regulation allows 864 seconds of transmit time per day on a 1% sub-band. **TTN allows 30 seconds.** That's 29× stricter. At SF12 with a 12-byte payload (~1.3 s airtime), 30 s/day is roughly **22 messages per day** — one every 65 minutes. See section 3.
2. **You don't control your spreading factor, so you don't control your message rate.** Adaptive Data Rate sets SF from link quality, which depends on where the device ends up relative to a gateway. The same firmware gives you ~500 messages/day at SF7 near a gateway and ~22 at SF12 at the edge of coverage — a **23× swing** you find out about after installation. See section 3.3.
3. **The 6-month battery claim is comfortable with the right parts and impossible with one wrong one.** A well-chosen regulator has a quiescent current around 1 µA; the LDO on most cheap dev boards draws 5 mA. That single part choice is the difference between six months of headroom and **ten days**. See section 4.3.
4. **Alkaline AAA will not deliver 6 months outdoors in a cold climate.** Alkaline capacity collapses below freezing and its internal resistance rises steeply as it discharges, which causes voltage droop during transmit exactly when the cells are already weak. Lithium AAA (Li-FeS₂) works to −40 °C with a flat discharge curve and low impedance. See section 5.
5. **E-ink is free when static and not free when refreshing.** A full refresh is a couple of seconds of active current. Refreshing hourly is affordable; refreshing every ten minutes is a meaningful fraction of your budget and probably isn't what anyone needs from a display nobody is looking at. See section 8.

### Revised project statement

> A LoRaWAN Class A environmental node built around an SoC with an integrated sub-GHz radio, running on 3× lithium AAA cells, with an adaptive uplink schedule that respects both regulatory duty cycle and the network operator's fair-use budget, a packed binary payload under 12 bytes, an e-ink display refreshed on a slower cadence than the uplink, and a self-hosted network server bridging to MQTT — with the power budget **measured** on hardware rather than calculated.

### Explicit non-goals

- **Not Class B or C.** Both require the radio to listen on a schedule, which is fundamentally incompatible with microamp-average power. Class A only.
- **Not a gateway.** Use an existing one, or build one as a separate mains-powered project.
- **Not real-time.** LoRaWAN Class A means downlinks are only delivered in the two receive windows after an uplink. Latency is bounded by your uplink interval, not by the network.
- **Not rechargeable/solar in v1.** Solar changes the whole power architecture and removes the interesting constraint. It's a good v2.
- **Not certified.** Radio module pre-certification covers the module; a finished product still needs its own compliance work if you sell it.

---

## 2. Reality check

### 2.1 The three budgets

This project has three independent budgets, and they bind in an order most people don't expect:

| Budget | Limit | Typically binds |
|---|---|---|
| **Network fair use** | TTN: 30 s uplink airtime/day | **First, usually** |
| **Regulatory duty cycle** | EU868 1% sub-band: 864 s/day | Second |
| **Battery** | ~274 µA average for 6 months | Third, with good parts |

Most people design for the battery and discover the airtime limits after deployment.

### 2.2 The failure modes

| Symptom | Cause |
|---|---|
| **Device transmits far less often than programmed** | The stack is enforcing duty cycle or fair use (§3) |
| Message rate differs between two identical units | ADR chose different spreading factors (§3.3) |
| **Battery dead in two weeks** | Regulator quiescent current (§4.3) |
| Battery dies in winter but not summer | Alkaline cold performance (§5.2) |
| Device resets during transmit | Voltage droop from cell internal resistance (§5.3) |
| Never joins the network | Coverage, or join backoff draining the battery first (§10.2) |
| Display ghosts badly outdoors | E-ink refresh below its rated temperature (§8.3) |
| Temperature reads 8 °C too high at midday | Solar gain on the enclosure (§9.2) |
| `no_free_ch` errors from the radio | Per-channel duty cycle enforcement (§3.2) |
| Downlinks never arrive | Class A — they only land in RX windows after an uplink (§10.3) |

### 2.3 What makes this a good project

The constraints are all *measurable*. Power is microamps you can put a meter on, airtime is seconds you can calculate exactly, payload is bytes you can count. There's very little guesswork, and the engineering is in making the numbers work rather than in taste.

That also means the project is honest: either the device runs six months or it doesn't, and you'll know.

---

## 3. The airtime budget ★

### 3.1 Three limits, stacked

**Regulatory (EU868), by sub-band:**

| Sub-band | Frequency | Duty cycle | Airtime/day | Max power |
|---|---|---|---|---|
| K | 863–865 MHz | 0.1% | 86 s | |
| L | 865–868 MHz | 1% | 864 s | |
| **M** | **868–868.6 MHz** | **1%** | **864 s** | 25 mW ERP |
| N | 868.7–869.2 MHz | 0.1% | 86 s | 25 mW ERP |
| P | 869.4–869.65 MHz | 10% | 8640 s | 500 mW ERP |
| Q | 869.7–870 MHz | 1% | 864 s | 25 mW ERP |

The three mandatory LoRaWAN channels (868.1, 868.3, 868.5 MHz) all sit in sub-band M at 1%.

**Network operator (TTN Fair Use Policy):**

- **30 seconds of uplink airtime per device per 24 hours**
- **10 downlink messages per device per 24 hours**, including ACKs for confirmed uplinks

**That's 29× stricter than the regulation.** If you're on TTN's public network, this is your real limit and nothing else matters.

TTN's own guidance: keep the application payload under 12 bytes and the interval at least several minutes.

**US915 differs**: no duty cycle, but a 400 ms maximum dwell time per transmission on 125 kHz channels — which means **SF12 and SF11 are not usable** there, since their airtime exceeds it.

### 3.2 Airtime by spreading factor

For a 12-byte application payload (13 bytes of MAC payload plus headers), 125 kHz bandwidth, CR 4/5:

| SF | Airtime | Messages in 30 s (TTN) | Messages in 864 s (1% reg) | Relative energy |
|---|---|---|---|---|
| **SF7** | ~56 ms | **~535/day** | ~15,400/day | 1× |
| SF8 | ~103 ms | ~290/day | ~8,400/day | 1.8× |
| SF9 | ~185 ms | ~162/day | ~4,670/day | 3.3× |
| SF10 | ~330 ms | ~90/day | ~2,600/day | 5.9× |
| SF11 | ~740 ms | ~40/day | ~1,160/day | 13× |
| **SF12** | **~1320 ms** | **~22/day** | ~650/day | **24×** |

Read the two right-hand columns together. **At SF12 on TTN you get 22 messages a day.** On a private network respecting only the regulation, you'd get 650.

Note also that the radio module may enforce duty cycle **per channel**. With only one channel configured, some modules start refusing transmissions after the very first message with a `no_free_ch` error — which looks like a hardware fault and isn't.

### 3.3 You don't control your spreading factor ★

Adaptive Data Rate lets the network server raise or lower your SF based on link margin. That's the right behavior — it saves energy where coverage is good — but it means:

**Your message rate is a function of where the device is installed, which you learn after installing it.**

```
Installed 200 m from a gateway, line of sight  → SF7  → ~500 msg/day possible
Installed 6 km away, through buildings          → SF12 → ~22 msg/day possible
```

A 23× difference in capability from the same firmware.

**Design for the worst case and adapt upward:**

```c
// Budget-aware scheduling: the interval is derived from the airtime
// actually consumed, not hardcoded.
uint32_t next_interval_seconds(const airtime_budget_t *b, uint8_t sf) {
    uint32_t toa_ms      = airtime_ms(sf, PAYLOAD_BYTES);
    uint32_t remaining_ms = b->daily_limit_ms - b->used_today_ms;
    uint32_t seconds_left = seconds_until_window_reset(b);

    if (remaining_ms < toa_ms) {
        return seconds_left;            // budget spent — wait for reset
    }
    uint32_t msgs_affordable = remaining_ms / toa_ms;
    uint32_t interval = seconds_left / msgs_affordable;

    return CLAMP(interval, MIN_INTERVAL_S, MAX_INTERVAL_S);
}
```

**Track your own airtime consumption on the device.** The stack enforces the regulatory duty cycle; almost none of them enforce the operator's fair-use policy, so that's your job.

And surface the current SF — on the display, and in the payload. "Why is this one updating so much less often?" is answered instantly by a number on the screen.

### 3.4 Consequences for the display

At SF12 with 22 messages a day, the freshest data anyone can see is over an hour old. That's fine for temperature and humidity — they don't change fast — but it needs to be a design decision rather than a surprise.

**Show the reading age on the display.** A number with no timestamp implies it's current, and at SF12 it isn't.

---

## 4. The power budget ★

### 4.1 The target

3× lithium AAA in series, roughly 1200 mAh usable.

```
6 months = 4380 hours
1200 mAh / 4380 h = 274 µA average
```

That's the number everything must fit inside.

### 4.2 Where it goes

Assuming an SoC with integrated radio (§6), a BME280-class sensor, and a 2.9″ e-ink panel:

**Sleep (continuous):**

| Item | Current |
|---|---|
| MCU stop mode + RTC | 2.0 µA |
| **Regulator quiescent** | **1.0 µA** (or 5000 µA — see §4.3) |
| Sensor sleep | 0.1 µA |
| E-ink deep sleep | 1.0 µA |
| Leakage, pull-ups | 1.0 µA |
| **Total** | **~5 µA** |

Over 4380 hours: **22 mAh**.

**Per-event energy:**

| Event | Energy |
|---|---|
| Sensor read (forced mode + MCU awake 50 ms) | ~0.0001 mAh |
| Uplink at SF7 (TX + RX1 + RX2 + MCU) | ~0.0013 mAh |
| Uplink at SF12 | ~0.0203 mAh |
| E-ink full refresh (~2 s) | ~0.0083 mAh |
| E-ink partial refresh (~0.3 s) | ~0.0017 mAh |

**Two scenarios over 182 days:**

| | SF7, uplink/15 min, full refresh/hour | SF12, uplink/hour, full refresh/2 h |
|---|---|---|
| Sleep | 22 mAh | 22 mAh |
| Uplinks | 96/day → 23 mAh | 22/day → 81 mAh |
| Sensor reads | 1 mAh | 0.3 mAh |
| Display | 24/day → 36 mAh | 12/day → 18 mAh |
| **Total** | **82 mAh** | **121 mAh** |
| **Of 1200 mAh** | **7%** | **10%** |

**The battery has enormous headroom.** Which is the point — the airtime budget (§3) bound first, and once you respect it the energy follows easily.

That headroom is also your safety margin for cold weather (§5.2), cell-to-cell variation, and the things you didn't think of.

### 4.3 Regulator quiescent current is the whole game ★

Look again at that sleep table. The regulator line says 1 µA, and it's the single most consequential component choice in the project.

| Regulator | Typical I_q | Sleep contribution over 6 months |
|---|---|---|
| Nano-power LDO | 1 µA | 4 mAh |
| Typical low-power LDO | 20 µA | 88 mAh |
| Generic 3.3 V LDO | 500 µA | 2190 mAh — **over capacity** |
| **AMS1117** (on most cheap dev boards) | **~5 mA** | **21,900 mAh — 18× the battery** |

An AMS1117 alone drains 1200 mAh in **ten days**, with the MCU asleep and the radio off the entire time.

This is the classic way a low-power project fails, and it fails silently — everything works on the bench, plugged in, and the battery is flat in a fortnight.

**Also check:**

- **Any LED with a pull-up.** A power LED at 2 mA is 8760 mAh over six months.
- **Pull-up and pull-down resistors** on idle lines. A 10 kΩ pull-up held low is 330 µA — larger than your entire budget.
- **Unused pins floating.** Configure every GPIO explicitly; a floating CMOS input can oscillate and draw current.
- **Sensor and peripheral sleep modes** actually entered, not just configured.
- **Level shifters and translators**, which often have surprisingly high quiescent draw.

**Consider running the MCU directly from the battery.** Three lithium AAA give 4.5 V nominal falling to about 2.7 V, and many low-power MCUs run happily from 1.8–3.6 V — which suggests two cells (3.0 V → 1.8 V) with no regulator at all. That eliminates the regulator's quiescent current entirely, at the cost of a supply that sags as the cells deplete. Worth evaluating against your parts' voltage ranges.

### 4.4 The e-ink refresh cadence

At ~0.0083 mAh per full refresh:

| Cadence | Over 6 months |
|---|---|
| Every 2 hours | 18 mAh |
| Every hour | 36 mAh |
| Every 15 minutes | 145 mAh |
| Every 5 minutes | 436 mAh — **36% of the battery** |

**Refresh no faster than the data arrives.** There is nothing to show that changed, and nobody is watching a display on a wall in a field.

A reasonable policy: refresh when a new reading differs meaningfully from the displayed one, with a minimum interval and a periodic full refresh to clear ghosting (§8.2).

---

## 5. Battery chemistry

### 5.1 The three options

| | **Alkaline AAA** | **Lithium AAA (Li-FeS₂)** | **NiMH AAA (LSD)** |
|---|---|---|---|
| Capacity | ~1000–1200 mAh | ~1200–1250 mAh | ~800 mAh |
| Nominal voltage | 1.5 V | 1.5 V | 1.2 V |
| Discharge curve | Sloping | **Flat** | Flat |
| Internal resistance | Rises steeply with discharge | **Low and stable** | Low |
| Cold performance | **Poor below 0 °C** | **Good to −40 °C** | Moderate |
| Self-discharge | ~2–3%/yr | ~1%/yr | ~15%/yr (LSD) |
| Leakage risk | **Real** | Low | Low |
| Cost | Low | ~3× | Moderate, rechargeable |

### 5.2 Cold is the problem ★

An outdoor environmental monitor experiences the environment it's monitoring.

**Alkaline capacity falls sharply below freezing** — a cell rated 1200 mAh at 20 °C may deliver half that at −10 °C. A "6-month" figure computed at room temperature becomes a three-month figure that expires in January, which is exactly when nobody wants to visit the installation.

**Lithium AAA works to −40 °C with a flat discharge curve.** For any outdoor deployment this is the correct choice and the price difference is trivial against the cost of a site visit.

Also: **alkaline cells leak.** A leaked cell in a sealed enclosure destroys the board. Lithium is far less prone to it.

### 5.3 Internal resistance and transmit droop

Alkaline internal resistance rises substantially over discharge — by mid-life a cell can be an ohm or more. Three in series during a 45 mA transmit pulse is a meaningful voltage drop, arriving precisely when the cells are already near their low end.

The symptom is a device that resets or browns out during transmit, only after a few months, only sometimes.

**Defenses:**

- **Lithium chemistry** — low, stable impedance. The primary fix.
- **Bulk capacitance across the supply** — 100–470 µF close to the radio. This handles sharp edges and the e-ink inrush well; note it does **not** meaningfully help a 1.3 s SF12 transmission, where the charge involved far exceeds anything a reasonable capacitor holds.
- **Measure battery voltage under load**, not at rest, and report it (§7.2). A resting voltage looks fine right up until it doesn't.
- **A brownout detector** set above the MCU's minimum, so the device fails cleanly rather than corrupting flash.

### 5.4 Reporting battery state

Include battery voltage in every uplink. It costs one byte and it's the difference between planned maintenance and a dead sensor nobody noticed.

Better: report the voltage **sampled during a transmit**, which reveals rising internal resistance long before the resting voltage moves.

---

## 6. MCU and radio selection

### 6.1 An SoC with integrated radio

| Option | Sleep | Notes |
|---|---|---|
| **STM32WL** (integrated sub-GHz radio) | ~2 µA stop | **One chip, one antenna, one BOM line.** Purpose-built for this. |
| STM32L0/L4 + SX1262 | ~1–2 µA | More flexible, two chips, SPI in between |
| nRF52840 + SX1262 | ~1.5 µA | Adds BLE for local configuration |
| ESP32 + radio | ~10 µA, quirky | **Avoid** — sleep current and wake energy are poor for this |
| RP2040 + radio | ~180 µA dormant | **Avoid** — not a low-power part |

**The integrated-radio SoC is the right default.** It removes the SPI link, the second crystal, and a class of layout problems, and the LoRaWAN stacks for it are mature.

ESP32 and RP2040 are excellent parts that are wrong for this project, and reaching for the familiar one is a common way to lose the power budget before starting.

### 6.2 Sleep discipline

```c
void enter_deep_sleep(uint32_t seconds) {
    // Every peripheral explicitly off — not "probably idle".
    sensor_sleep();
    eink_deep_sleep();                 // §8.4 — critical for panel health
    radio_sleep();

    // Every GPIO in a defined, non-floating state.
    gpio_config_for_sleep();

    rtc_set_wakeup(seconds);
    HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);

    // On wake: reconfigure clocks — STOP mode drops the PLL.
    SystemClock_Config();
}
```

Two points that catch people: **wake energy is not free** (clock restart, PLL lock, peripheral reinit can be milliseconds of active current), so waking every second to check something is expensive even if you do nothing. And **the wake path should be short** — read the sensor, decide, act, sleep. Anything that logs to a UART at 115200 is spending real energy on debug output.

### 6.3 Watchdog

An unattended device needs an independent watchdog. But the watchdog must survive sleep, which means an independent low-power one (IWDG on STM32) clocked from the LSI, with a period longer than your sleep interval — or refreshed from the RTC wake.

A device that hangs in a field and needs a site visit is a worse failure than a device that reboots.

---

## 7. Payload design

### 7.1 Every byte costs three times

Payload size increases airtime, which costs **energy**, **duty-cycle budget**, and **fair-use budget** simultaneously. JSON over LoRaWAN is a genuine anti-pattern.

```
{"temperature":23.45,"humidity":61.2,"pressure":1013.2,"battery":4.12}
= 70 bytes → ~2.5 s at SF12
```

versus a packed binary encoding:

```
[0x01][0x09 0x29][0x17 0xE4][0x27 0x8C][0x9C]
= 8 bytes → ~1.2 s at SF12
```

TTN's own guidance is to stay under 12 bytes.

### 7.2 A concrete format

```c
typedef struct __attribute__((packed)) {
    uint8_t  version;          // format version — makes decoders upgradable
    int16_t  temp_cc;          // °C × 100,  −327.68 .. 327.67
    uint16_t humidity_pm;      // %RH × 100, 0 .. 100.00
    uint16_t pressure_dPa;     // (hPa − 800) × 10, covers 800 .. 1455 hPa
    uint8_t  battery_cv;       // (V − 2.0) × 100, covers 2.00 .. 4.55 V
    uint8_t  flags;            // sf | join_state | error bits
} uplink_t;                    // 9 bytes
```

Three design notes:

**Range-shifted fixed point.** Pressure never goes below 800 hPa at any habitable altitude, so subtracting 800 lets 16 bits carry 0.1 hPa resolution across the whole useful range. The same trick on battery voltage fits it in one byte at 10 mV resolution.

**A version byte in every payload.** Decoders live on the application server and devices in the field can't all be updated at once. One byte buys you the ability to change the format later.

**Flags carry diagnostics.** Current SF, join state, and sensor error bits cost nothing and answer most support questions without a downlink.

### 7.3 Decode on the server

The network server runs a payload formatter — JavaScript in both TTN and ChirpStack:

```javascript
function decodeUplink(input) {
  const b = input.bytes;
  if (b[0] !== 1) return { errors: ["unsupported payload version " + b[0]] };

  const view = new DataView(new Uint8Array(b).buffer);
  return {
    data: {
      temperature: view.getInt16(1) / 100,
      humidity:    view.getUint16(3) / 100,
      pressure:    view.getUint16(5) / 10 + 800,
      battery:     b[7] / 100 + 2.0,
      sf:          7 + (b[8] & 0x07),
      joined:      !!(b[8] & 0x08),
      sensor_error:!!(b[8] & 0x10),
    },
  };
}
```

**Keep the decoder in the firmware repository**, versioned alongside the struct. A decoder that drifts from the encoder produces plausible, wrong numbers — the worst kind of bug in a monitoring system.

---

## 8. The e-ink display

### 8.1 Free when static, not when refreshing

E-ink holds its image with zero power. That's the whole appeal, and it makes the display feasible at all.

But a refresh draws real current for a real duration:

| | Duration | Notes |
|---|---|---|
| Full refresh | 1–3 s | Visible black/white flashing; clears ghosting |
| Partial refresh | ~0.3 s | No flash; ghosting accumulates |
| Fast/partial waveform | ~0.5 s | Panel-dependent |

### 8.2 Ghosting and the refresh policy

Partial refreshes leave residue. The standard policy is a periodic full refresh:

```c
#define PARTIALS_BEFORE_FULL 12

void update_display(const reading_t *r) {
    if (!reading_differs_meaningfully(r, &displayed)) return;   // nothing to show

    if (partial_count >= PARTIALS_BEFORE_FULL) {
        eink_full_refresh(render(r));
        partial_count = 0;
    } else {
        eink_partial_refresh(render(r));
        partial_count++;
    }
    displayed = *r;
    eink_deep_sleep();                      // §8.4
}
```

The `reading_differs_meaningfully` check is worth having — a temperature that moved 0.01 °C doesn't justify a refresh, and skipping those is free energy.

### 8.3 Temperature limits ★

E-ink refresh is an electrophoretic process and it's **temperature dependent**. Most panels are specified for roughly 0 °C to 50 °C operation. Below freezing, refreshes become slow, incomplete, or leave severe ghosting; some panels won't refresh at all.

For an outdoor monitor, that's a real limitation:

- **Read the panel's own temperature sensor** if it has one, and select the appropriate waveform
- **Skip refreshes below the rated minimum** rather than producing a corrupted image
- **Display the last good reading with an age indicator** when you can't refresh
- Consider whether the display belongs indoors while the sensor is outdoors

That last option is worth taking seriously. The display is for a human, and humans are usually inside.

### 8.4 Deep-sleep the panel ★

**Leaving an e-ink panel biased and powered after a refresh damages it** and draws current continuously.

Always issue the panel's deep-sleep command when the refresh completes, and re-initialize on the next update. It's an extra initialization sequence each time and it's non-negotiable — a panel left powered can develop permanent artifacts within days.

### 8.5 What to show

At 22 messages a day (§3.4), the display is a status board, not a live readout:

```
┌──────────────────────────┐
│  21.4 °C      62 %RH     │
│  1013.2 hPa              │
│                          │
│  ▂▃▅▆▅▃▂▁▂▃▄  24 h       │
│                          │
│  updated 47 min ago      │
│  SF12  ·  bat 4.1 V      │
└──────────────────────────┘
```

**The age line is the important one.** A number without it implies currency the device can't provide. The SF and battery lines answer most field questions without a laptop.

---

## 9. Sensors and enclosure

### 9.1 Sensor selection

| Sensor | Measures | Power | Notes |
|---|---|---|---|
| **BME280 / BME680** | T, RH, P (+VOC) | ~350 µA for 10 ms forced | **Excellent fit.** Sleep current negligible. |
| SHT4x | T, RH | Very low | Better humidity accuracy |
| **SCD4x** | CO₂ | **~18 mA for ~1 s** | A CO₂ reading costs ~15× an SF7 uplink |
| Light (photodiode + ADC) | Lux | Microamps | Cheap |

**CO₂ sensors are power hogs** — NDIR measurement means running an IR source. One SCD4x reading costs roughly what fifteen SF7 uplinks do. If CO₂ is required, budget it explicitly and read it far less often than temperature.

Use **forced/one-shot mode**, never continuous. A BME280 left in normal mode draws hundreds of microamps forever.

### 9.2 Solar gain will ruin your temperature data ★

A temperature sensor inside a sealed enclosure in direct sun measures the enclosure, not the air. The error is not subtle — a dark plastic box in summer sun can read many degrees above ambient.

This is the single most common way an amateur weather station produces wrong data, and it produces *plausible* wrong data, which is worse.

**Fixes, in order of effectiveness:**

- **A radiation shield** — the stacked-plate Stevenson-screen style, which allows airflow while blocking direct radiation. This is the real answer and it's the reason official weather stations look like they do.
- **White or reflective enclosure**, mounted in shade, north-facing (northern hemisphere)
- **Sensor on a short cable outside the main enclosure**, in its own vented shield
- **Never** a sealed dark box in the open

### 9.3 Humidity needs airflow, and water doesn't

Humidity sensors need air exchange to read ambient, and electronics need to stay dry. The reconciliation is a **vent with a hydrophobic membrane** — a PTFE/Gore-style vent that passes water vapour and pressure while blocking liquid.

Also:

- **Condensation** is the real enemy, not rain. A sealed enclosure that heats and cools daily pumps moist air in and condenses it. A membrane vent equalizes pressure and prevents this.
- **A desiccant pack** helps initially and saturates. Not a substitute for a vent.
- **Drainage** — mount so any water that gets in can get out.

### 9.4 Antenna placement

- **Never inside a metal enclosure.** Use plastic, or mount the antenna externally.
- **Keep the antenna away from the battery pack and ground planes.** Both detune it.
- **Vertical polarization** matches most gateways.
- A ground plane or counterpoise matters for a quarter-wave whip; check the antenna's requirements.

A poorly-placed antenna costs link margin, which costs spreading factor, which costs **message rate and battery** (§3.3). Antenna placement is a power decision.

---

## 10. The network side

### 10.1 LoRaWAN is not MQTT

```
Device → RF → Gateway → (IP) → Network Server → Application Server → MQTT broker → your app
```

The network server handles join, session keys, MAC commands, ADR, deduplication across gateways, and downlink scheduling. The application server handles payload decoding and integrations.

**You do not write this.** Options:

| | Notes |
|---|---|
| **TTN (public)** | Free, community gateways, **30 s/day fair use** (§3.1) |
| **ChirpStack (self-hosted)** | Open source, full control, no fair-use limit, MQTT built in. **Recommended** if you run your own gateway. |
| The Things Stack (private) | Commercial, the same stack as TTN |
| Helium / commercial operators | Coverage without running gateways |

**Running your own gateway plus ChirpStack removes the fair-use constraint entirely**, leaving only the regulatory duty cycle — which is 29× more generous. If update rate matters to your application, that's the single highest-leverage decision available.

ChirpStack publishes to MQTT natively:

```
application/{id}/device/{devEUI}/event/up
application/{id}/device/{devEUI}/event/join
application/{id}/device/{devEUI}/command/down
```

So the "MQTT bridge" is largely configuration rather than code.

### 10.2 Joining, and the battery trap ★

OTAA join is a two-message exchange, and a device that can't reach a gateway will retry.

**Unbounded join retries will flatten the battery.** A device installed in a coverage hole, retrying every 30 seconds, is transmitting constantly — the highest-power thing it can do — until the cells are dead. This is a real and unforgiving field failure.

```c
// Exponential backoff with a hard ceiling. Never retry tightly.
static uint32_t join_backoff_s = 30;

void on_join_failed(void) {
    join_backoff_s = MIN(join_backoff_s * 2, 3600);   // cap at 1 hour
    join_attempts++;

    if (join_attempts > MAX_JOIN_ATTEMPTS) {
        display_message("No network\ncheck coverage");
        enter_dormant_mode();      // sleep long; retry a few times a day
    }
    schedule_wake(join_backoff_s);
}
```

Show the join state on the display. "No network" on the screen is what turns a dead-looking device into a five-minute diagnosis.

Consider ABP for development — it skips the join entirely — while keeping OTAA for deployment, where session key rotation and network mobility matter.

### 10.3 Downlinks are constrained

Class A devices open two receive windows, typically 1 s and 2 s after an uplink. **That is the only time a downlink can arrive.**

Consequences:

- **You cannot push to the device.** A configuration change waits for the next uplink — up to an hour at SF12.
- **TTN allows 10 downlinks per day**, including ACKs for confirmed uplinks.
- **Avoid confirmed uplinks.** Each one consumes a downlink from that budget and adds a retransmission path that costs energy. Use them sparingly, if at all.

Design the device to need almost no downlinks: sensible defaults, local configuration via a button or BLE, and downlink reserved for interval changes and firmware flags.

### 10.4 The application side

```
ChirpStack → MQTT → your consumer → time-series DB (InfluxDB/TimescaleDB) → Grafana
                                  → alerting (battery low, device silent)
```

**Alert on silence, not just on values.** A sensor that stops reporting is the failure you most need to know about, and a threshold alert on temperature will never fire for a dead device. A "no uplink in 3× the expected interval" rule catches it.

---

## 11. Measure, don't calculate ★

### 11.1 Calculated budgets are always optimistic

Section 4's table is a design tool. The real number comes from a meter.

**Use a current measurement tool with microamp resolution and a wide dynamic range** — the challenge is capturing 2 µA sleep and 45 mA transmit in the same trace. A Nordic Power Profiler Kit II or an Otii Arc handles this; a bench DMM generally does not, because it can't span both ranges or sample fast enough to integrate a transmit pulse.

### 11.2 Measure a complete cycle

```
        45 mA ┤                    ╭──╮ TX
              │                    │  │
         5 mA ┤        ╭─╮    ╭────╯  ╰─╮ ╭╮ RX1  ╭╮ RX2
              │        │ │    │         ╰─╯╰──────╯╰──╮
         2 µA ┤────────╯ ╰────╯                       ╰──────────
              └──┬─────┬──┬────┬────────┬──────┬──────┬─────────
                sleep  wake   sensor    TX    RX1    RX2   sleep
```

Integrate the whole thing to get charge per cycle in microamp-hours, multiply by cycles per six months, add the sleep baseline, and compare to §4.

**Do this early**, on the first working hardware. It's the milestone that tells you whether the project's headline claim is true, and discovering otherwise in month five is expensive.

### 11.3 Validate with a real run

Arithmetic misses things. Run a unit on a fresh set of cells with telemetry logging battery voltage, in the actual enclosure, in the actual location, for at least a month. Extrapolate from the measured discharge curve.

**Run one in the cold** if the deployment is outdoors. A freezer at −15 °C for a week tells you more about winter behavior than any datasheet.

### 11.4 Track it in the field

Battery voltage in every uplink (§5.4), plotted over time, gives you the real discharge curve from real deployments. It's the ground truth that validates or refutes everything above, and it costs one byte.

---

## 12. Tech stack and setup

| Layer | Choice | Why |
|---|---|---|
| **MCU** | STM32WL-class SoC with integrated sub-GHz radio | One chip, ~2 µA stop mode (§6.1) |
| **LoRaWAN stack** | LoRaMAC-node, or the vendor's | Mature; handles regulatory duty cycle |
| **RTOS** | **None**, or a minimal scheduler | A superloop with sleep is simpler and lower power here |
| **Sensor** | BME280 or SHT4x, forced mode | |
| **Display** | 2.9″ e-ink, SPI | |
| **Battery** | **3× lithium AAA (Li-FeS₂)** | §5.2 |
| **Regulator** | Nano-power LDO, I_q ≤ 2 µA, or none | §4.3 — the most consequential part choice |
| **Network server** | **ChirpStack**, self-hosted | Removes the fair-use limit (§10.1) |
| **Storage** | InfluxDB or TimescaleDB | |
| **Dashboard** | Grafana | |
| **Power measurement** | **Nordic PPK2 or Otii Arc** | §11.1 — buy this before the PCB |

Three setup notes:

**Buy the power profiler first.** Before the PCB, before the enclosure. Every design decision in §4 is unverifiable without it, and it's the cheapest way to avoid a respin.

**Breadboard the power path before the radio.** Get a board into deep sleep at single-digit microamps with nothing else running. If you can't hit that, nothing downstream matters, and it's much easier to diagnose in isolation.

**Run your own gateway if you can.** An indoor gateway plus ChirpStack costs modestly, removes the 30 s/day limit, gives you SF7 link quality nearby, and makes development enormously faster because you can see every packet.

---

## 13. Repository layout

```
IoT-Environmental-Monitor/
├── README.md
├── docs/
│   ├── design.md               ← this document
│   ├── power-budget.md         ← ★ measured, not calculated (§11)
│   ├── airtime-budget.md       ← ★ SF table and the fair-use math (§3)
│   └── enclosure.md            ← radiation shield, venting (§9)
├── firmware/
│   ├── src/
│   │   ├── main.c              ← the superloop
│   │   ├── power.c             ← ★ sleep discipline, GPIO config (§6.2)
│   │   ├── airtime.c           ← ★ own fair-use accounting (§3.3)
│   │   ├── lorawan.c           ← join backoff (§10.2)
│   │   ├── payload.c           ← ★ packed struct (§7.2)
│   │   ├── sensor.c            ← forced mode only
│   │   ├── eink.c              ← ★ deep sleep after refresh (§8.4)
│   │   └── watchdog.c
│   └── tests/
├── decoder/
│   └── decoder.js              ← ★ versioned WITH the payload struct (§7.3)
├── hardware/
│   ├── schematic/
│   ├── pcb/
│   └── bom.csv                 ← ★ regulator I_q as a reviewed column
├── server/
│   ├── chirpstack/             ← config
│   ├── bridge/                 ← MQTT → time-series
│   └── grafana/
├── enclosure/                  ← radiation shield model
└── tools/
    ├── airtime_calc.py         ← SF/payload → time on air
    └── power_analysis.py       ← PPK2 trace → mAh per cycle
```

---

## 14. Milestone ladder

### M0 — The two budgets ★ **before ordering parts**
**Est. 2–3 days**

Write `docs/airtime-budget.md` and `docs/power-budget.md`. Decide: network operator (TTN vs self-hosted), target uplink interval at both SF7 and SF12, display cadence, battery chemistry.

**Done when:** you can state your message rate at SF12 and it's a number you're happy with.

---

### M1 — Sleep current on a breadboard ★
**Est. 1 week**

MCU, regulator, RTC wake. Nothing else. Measure sleep current with the profiler.

**Do not proceed until sleep is in single-digit microamps.** Every subsequent problem is easier to find in isolation, and a regulator mistake found here costs a part swap rather than a PCB respin.

---

### M2 — Sensor and payload
**Est. 1 week**

Forced-mode reads, the packed struct, the decoder, round-trip tests.

---

### M3 — LoRaWAN uplink
**Est. 1.5 weeks**

Stack integration, OTAA join with backoff, uplink, RX windows, ADR. Measure the full cycle (§11.2).

**Done when:** you have a measured mAh-per-uplink figure at SF7 and SF12.

---

### M4 — Airtime accounting ★
**Est. 4–5 days**

Own fair-use tracking, budget-derived scheduling, SF reporting.

**The stack enforces the regulation; it won't enforce your operator's policy.** That's this milestone.

---

### M5 — E-ink
**Est. 1 week**

Refresh, partial/full policy, deep sleep after every refresh, temperature gating, layout with the age indicator.

---

### M6 — Network and dashboard
**Est. 1 week**

ChirpStack, MQTT bridge, time-series storage, Grafana, **silence alerting**.

---

### M7 — PCB
**Est. 3 weeks including fabrication**

Schematic, review the BOM specifically for quiescent current, layout with antenna clearance, fabricate, bring up.

**Re-measure sleep current on the real board.** Leakage paths that don't exist on a breadboard appear on a PCB.

---

### M8 — Enclosure ★
**Est. 2 weeks**

Radiation shield, membrane vent, antenna clearance, drainage.

**Validate against a reference thermometer in direct sun.** §9.2 is the difference between data and plausible-looking noise.

---

### M9 — Battery validation ★
**Est. 4+ weeks, mostly waiting**

A full unit on fresh cells, logging, in situ. Plus a cold-chamber run.

**This is the milestone that proves the headline claim.** Start it as early as the hardware allows, because it's wall-clock time you can't compress.

---

## 15. Reference implementations

### 15.1 Airtime calculation

```python
def time_on_air_ms(sf, payload_bytes, bw_khz=125, cr=1, preamble=8,
                   header=True, crc=True, low_dr_opt=None):
    """LoRa airtime per the SX127x/SX126x datasheet formula."""
    t_sym = (2 ** sf) / (bw_khz * 1000) * 1000          # ms

    if low_dr_opt is None:
        low_dr_opt = 1 if (sf >= 11 and bw_khz == 125) else 0

    t_preamble = (preamble + 4.25) * t_sym

    pl = payload_bytes + 13          # MAC header + FPort + MIC
    num = 8 * pl - 4 * sf + 28 + 16 * crc - 20 * (not header)
    den = 4 * (sf - 2 * low_dr_opt)
    n_payload = max(math.ceil(num / den) * (cr + 4), 0) + 8

    return t_preamble + n_payload * t_sym
```

The `low_dr_opt` flag is mandatory at SF11 and SF12 on 125 kHz and is easy to omit — leaving it out understates SF12 airtime by roughly 20%, which is exactly the case where the budget is tightest.

### 15.2 Fair-use budget tracking

```c
typedef struct {
    uint32_t window_start_s;        // RTC seconds
    uint32_t used_ms;               // airtime consumed in this window
    uint32_t limit_ms;              // 30000 for TTN
} airtime_budget_t;

bool airtime_may_transmit(airtime_budget_t *b, uint8_t sf, uint8_t bytes) {
    uint32_t now = rtc_seconds();
    if (now - b->window_start_s >= 86400) {      // rolling 24 h
        b->window_start_s = now;
        b->used_ms = 0;
    }
    return (b->used_ms + airtime_ms(sf, bytes)) <= b->limit_ms;
}

void airtime_record(airtime_budget_t *b, uint8_t sf, uint8_t bytes) {
    b->used_ms += airtime_ms(sf, bytes);
}
```

**Persist the window across resets**, in RTC backup registers or flash. A device that reboots and forgets its budget will blow through the daily limit in an afternoon.

### 15.3 The main loop

```c
int main(void) {
    system_init();
    if (!lorawan_join_with_backoff()) enter_dormant_mode();   // §10.2

    for (;;) {
        watchdog_refresh();

        reading_t r;
        if (sensor_read_forced(&r)) {
            r.battery_mv = adc_read_battery_under_load();    // §5.4

            if (airtime_may_transmit(&budget, current_sf(), sizeof(uplink_t))) {
                uplink_t p = pack_uplink(&r);
                if (lorawan_send_unconfirmed(&p, sizeof p)) { // §10.3
                    airtime_record(&budget, current_sf(), sizeof p);
                }
            }
            maybe_update_display(&r);                         // §8.2
        }

        uint32_t sleep_s = next_interval_seconds(&budget, current_sf());
        enter_deep_sleep(sleep_s);
    }
}
```

Unconfirmed uplinks by default, budget checked before transmitting, and the sleep interval derived from what's left rather than hardcoded.

---

## 16. Testing

### 16.1 Power, at every stage

| Test | Assertion |
|---|---|
| Bare board sleep | < 10 µA |
| Sleep with all peripherals attached | < 10 µA |
| Charge per uplink at SF7 | matches the budget within 20% |
| Charge per uplink at SF12 | matches within 20% |
| Charge per full e-ink refresh | matches within 20% |
| Full cycle integration | projected life ≥ 6 months with margin |

**Re-run after every firmware change that touches sleep or peripherals.** A forgotten `gpio_config_for_sleep()` in one code path costs milliamps continuously, and nothing else will tell you.

### 16.2 Airtime

```python
def test_airtime_matches_reference():
    # Cross-check against a published LoRaWAN airtime calculator.
    assert abs(time_on_air_ms(12, 12) - 1318.9) < 1.0
    assert abs(time_on_air_ms(7, 12) - 56.6) < 1.0

def test_budget_blocks_at_limit():
    b = budget(limit_ms=30000)
    for _ in range(22):
        assert may_transmit(b, sf=12, bytes=12)
        record(b, sf=12, bytes=12)
    assert not may_transmit(b, sf=12, bytes=12)   # 23rd exceeds 30 s
```

### 16.3 Payload round-trip

Encode in C, decode in the JavaScript decoder, assert equality — including edge values at the extremes of every field's range. A pressure reading at 1455 hPa should survive; one at 1456 should be caught by a range assertion rather than silently wrapping.

### 16.4 Field conditions

- **Cold chamber** at −15 °C: does it transmit, does the display refresh, what's the battery voltage under load
- **Heat** at +50 °C in sun: sensor accuracy against a reference, enclosure interior temperature
- **Condensation cycling**: warm humid to cold, repeatedly, and inspect for moisture
- **Coverage edge**: install deliberately far from a gateway and confirm ADR settles and the budget logic adapts

### 16.5 The long run

One unit, fresh cells, real location, telemetry to the dashboard, for a month minimum. Plot the discharge curve and extrapolate.

---

## 17. Stretch goals

| Feature | Effort | Value |
|---|---|---|
| **Solar + supercapacitor or LiFePO₄** | Medium | Removes the battery constraint entirely; changes the power architecture |
| **Multiple sensor variants from one board** | Small | Soil moisture, light, CO₂ — the payload version byte already supports it |
| Local BLE configuration | Medium | Configure without downlinks (§10.3) |
| **Firmware update over LoRaWAN** | Large | Very slow at 30 s/day; multicast helps but it's a real project |
| Onboard data logging | Small | Buffer readings between uplinks; send aggregates. Good fit for SF12. |
| **Aggregate statistics in the payload** | Small | Min/max/mean since last uplink in the same 12 bytes — much better than one instantaneous sample at SF12 |
| Mesh or repeater | Large | Outside LoRaWAN's model |
| Enclosure design for manufacture | Medium | Injection-moldable radiation shield |
| Certification | Large | Required to sell; use a pre-certified module to reduce it |

The aggregate-statistics item is the best value on that list. At 22 messages a day, sending one instantaneous reading wastes the intervening hour of data. Sending min, max, and mean in the same payload size turns a sparse sampler into a proper logger, and it costs a handful of bytes and some firmware.

---

## 18. References

### LoRaWAN

| Source | For |
|---|---|
| **TTN: Duty Cycle** documentation | The EU868 sub-band table and the Fair Use Policy in §3.1 |
| **TTN: Fair Use Policy** | 30 s uplink airtime and 10 downlinks per device per day |
| LoRaWAN Regional Parameters (LoRa Alliance) | EU868, US915, dwell time, data rates |
| ETSI EN 300 220 | The underlying European SRD regulation |
| **Airtime calculators** (e.g. avbentem's) | Cross-check §15.1 |
| ChirpStack documentation | Self-hosted network server and MQTT integration |
| LoRaMAC-node | The reference device stack |

### Low power

- **Nordic Power Profiler Kit II** and **Otii Arc** documentation — measurement technique
- STM32 low-power application notes — stop modes, wake sources, GPIO configuration for sleep
- Battery manufacturer datasheets — **read the temperature and load-dependent capacity curves**, not just the headline mAh

### Sensors and environment

- BME280 / BME680 datasheets — forced mode, power figures
- WMO Guide to Meteorological Instruments — radiation shield requirements and siting; the authoritative source for §9.2
- Gore and equivalent protective-vent application notes — membrane venting

### E-ink

- Panel datasheets — refresh waveforms, temperature range, **deep-sleep requirement**
- GxEPD2 and similar driver libraries — practical refresh handling

---

## Appendix A — Decision record

| Decision | Rationale |
|---|---|
| **Design to the airtime budget first, not the battery** | TTN's 30 s/day is 29× stricter than the 1% regulation's 864 s/day, and it binds before the cells do |
| **Assume SF12 in planning** | ADR sets SF from link quality, which depends on installation location — a 23× swing in message rate you discover after mounting the device |
| Track fair-use airtime on the device | Stacks enforce the regulatory duty cycle; almost none enforce the operator's policy |
| Persist the airtime window across resets | A device that reboots and forgets its budget blows the daily limit in an afternoon |
| Derive the uplink interval from remaining budget | A hardcoded interval is either wasteful at SF7 or illegal at SF12 |
| Report SF and reading age on the display | "Why is this one slower?" is answered by a number on the screen |
| **Nano-power regulator, I_q ≤ 2 µA — or no regulator** | An AMS1117 at ~5 mA drains the pack in ten days with everything else asleep. The single most consequential part choice. |
| Audit the BOM for quiescent current as a review step | Power LEDs, pull-ups on idle lines, and level shifters each exceed the entire budget |
| **Lithium AAA (Li-FeS₂), not alkaline** | Alkaline capacity collapses below freezing and its internal resistance rises with discharge, causing transmit droop when cells are already weak |
| Report battery voltage sampled under load | Resting voltage looks fine until it suddenly doesn't; under-load reveals rising impedance early |
| Bulk capacitance for edges, not for SF12 | No reasonable capacitor supplies a 1.3 s transmit; chemistry is the fix |
| **SoC with integrated radio** | One chip, one antenna, ~2 µA stop. ESP32 and RP2040 are good parts and wrong for this. |
| Superloop with deep sleep, no RTOS | Simpler and lower power for a device that's asleep 99.9% of the time |
| Explicit GPIO configuration before sleep | A floating CMOS input can oscillate and draw current indefinitely |
| **Packed binary payload under 12 bytes** | Every byte costs energy, duty cycle, and fair-use budget simultaneously |
| Range-shifted fixed point | Pressure minus 800 fits 0.1 hPa across the useful range in 16 bits |
| Version byte in every payload | Field devices can't all be updated at once; decoders must handle both |
| Decoder versioned in the firmware repo | A drifted decoder produces plausible wrong numbers — the worst bug in a monitoring system |
| **E-ink refresh no faster than the data arrives** | Nothing changed, nobody is watching, and a 5-minute cadence costs a third of the battery |
| **Deep-sleep the panel after every refresh** | Leaving it biased draws current and damages it |
| Gate refreshes on panel temperature | Most panels are rated to 0 °C; below that, refreshes ghost or fail |
| **Radiation shield for the temperature sensor** | A sealed box in the sun measures the box; the error is large and the data looks plausible |
| Membrane vent, not a sealed enclosure | Condensation from daily thermal cycling is the real enemy, not rain |
| Antenna outside metal, away from the battery | Link margin determines SF, which determines message rate and battery |
| **Exponential join backoff with a dormant fallback** | Unbounded join retries in a coverage hole flatten the battery — transmitting is the most expensive thing the device does |
| Unconfirmed uplinks by default | Each confirmation consumes one of ten daily downlinks and adds a retransmission path |
| **Self-hosted ChirpStack + own gateway, if update rate matters** | Removes the fair-use limit entirely, leaving only the 29×-more-generous regulation |
| Alert on silence, not just on values | A threshold alert never fires for a dead sensor |
| **Buy the power profiler before the PCB** | Every §4 decision is unverifiable without microamp-to-milliamp measurement in one trace |
| Reach single-digit µA on a breadboard before adding anything | A regulator mistake found there is a part swap, not a respin |
| **Measure a full duty cycle and integrate** | Calculated budgets are always optimistic; this is the milestone that proves the headline claim |
| Cold-chamber and month-long field validation | Arithmetic misses things, and winter is when site visits are worst |

---

## Appendix B — Quick reference card

```
THE BINDING CONSTRAINT IS AIRTIME, NOT BATTERY
  EU868 regulation, 1% sub-band:   864 s/day
  ★ TTN Fair Use Policy:            30 s/day   ← 29× stricter
    plus 10 downlinks/day (ACKs count)
  US915: no duty cycle, but 400 ms dwell ⇒ SF11/SF12 UNUSABLE

AIRTIME BY SF (12-byte payload, 125 kHz, CR 4/5)
  SF7   ~56 ms   →  535 msg/day on TTN    1× energy
  SF9  ~185 ms   →  162 msg/day           3.3×
  SF10 ~330 ms   →   90 msg/day           5.9×
  SF12 ~1320 ms  →   22 msg/day           24×
  ★ ADR picks your SF from link quality — a 23× swing you learn
    AFTER installing. Plan for SF12.
  ★ low_dr_opt flag is mandatory at SF11/12 — omitting it
    understates SF12 airtime by ~20%

EU868 SUB-BANDS
  K 863–865      0.1%      N 868.7–869.2   0.1%
  L 865–868      1%        P 869.4–869.65  10%, 500 mW
  M 868–868.6    1%  ← the 3 default channels
  Q 869.7–870    1%

POWER — 3× lithium AAA ≈ 1200 mAh / 4380 h = 274 µA average
  sleep budget      MCU 2 µA · REGULATOR 1 µA · sensor 0.1 · eink 1
  per uplink SF7    0.0013 mAh      per uplink SF12  0.0203 mAh
  per eink full     0.0083 mAh      per eink partial 0.0017 mAh
  realistic total   80–120 mAh of 1200 ⇒ big headroom IF parts are right

  ★ REGULATOR QUIESCENT IS THE WHOLE GAME
      1 µA nano-LDO   →     4 mAh over 6 months
     20 µA LDO        →    88 mAh
    500 µA LDO        →  2190 mAh  (over capacity)
      5 mA AMS1117    → 21900 mAh  ← DEAD IN 10 DAYS
    also audit: power LEDs · pull-ups on idle lines · level shifters
    consider NO regulator: 2 cells 3.0→1.8 V direct

BATTERY CHEMISTRY
  ★ LITHIUM AAA (Li-FeS₂), not alkaline
    alkaline: capacity collapses below 0 °C, IR rises with discharge
              ⇒ transmit droop exactly when cells are weak, plus leakage
    lithium:  −40 °C, flat curve, low stable impedance
  report battery voltage sampled UNDER LOAD, not at rest
  caps help sharp edges; nothing helps a 1.3 s SF12 pulse

E-INK
  free when static, NOT when refreshing
  full ~2 s · partial ~0.3 s · full refresh every ~12 partials
  ★ DEEP SLEEP THE PANEL after every refresh or it degrades
  ★ rated ~0–50 °C — below freezing it ghosts or won't refresh
  refresh no faster than data arrives:
    hourly 36 mAh · every 5 min 436 mAh (36% of the pack)
  ALWAYS show reading age — at SF12 data is an hour old

ENCLOSURE
  ★ radiation shield or your temperature reads the BOX, not the air
  membrane vent (PTFE) — condensation is the enemy, not rain
  antenna outside metal, away from the battery pack

NETWORK
  device → gateway → network server → app server → MQTT
  ★ self-hosted ChirpStack + own gateway removes the 30 s/day limit
  ★ join retries MUST back off exponentially — a coverage hole
    otherwise flattens the battery by transmitting constantly
  Class A: downlinks ONLY in RX1/RX2 after an uplink
  avoid confirmed uplinks (each ACK is one of your 10 downlinks)
  alert on SILENCE, not just on values

MEASURE, DON'T CALCULATE
  buy the power profiler BEFORE the PCB
  single-digit µA on a breadboard before adding anything
  integrate a full cycle: sleep→wake→sense→TX→RX1→RX2→display→sleep
  then run one for a month, and one in a freezer
```
