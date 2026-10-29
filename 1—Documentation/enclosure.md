# Enclosure

Design doc §9, turned into requirements for milestone M8. CAD for the
radiation shield goes in `enclosure/`.

## Temperature: shield the sensor, not the box

A sealed box in sun measures the box. The BME280 goes **on a short cable
(≤ 30 cm, I²C at 100 kHz is fine) in its own stacked-plate radiation shield**
(Stevenson-screen style: 6–8 white plates, 10–15 mm gaps, louvred so air
flows and direct/reflected sun does not reach the sensor). The main
enclosure — battery, MCU, display — can be sealed.

- White or reflective plastic (ASA or UV-stabilised ABS/PETG; PLA creeps in sun)
- Mounted in shade, north-facing (northern hemisphere), 1.25–2 m above ground (WMO siting)
- **Validation (M8):** log against a reference thermometer in its own
  shield in direct summer sun for a week. Acceptance: within ±0.5 °C.

## Humidity and water

- **PTFE membrane vent** (Gore PolyVent / Amphenol LTW class) on the main
  enclosure: passes vapour and pressure, blocks liquid. It stops the daily
  thermal pumping that condenses moisture inside — the real enemy, not rain.
- Desiccant pack at assembly; it saturates, it is not a substitute for the vent.
- Mount cable glands downward and add a drip loop; a 2–3 mm drain hole at the
  lowest point.
- The sensor itself lives in the shield, so it sees ambient air.

## Display

The panel is rated 0–50 °C and the firmware skips refreshes outside that
range (showing the last good image with its absolute timestamp). For a
deployment that is often below 0 °C, consider putting the display indoors on
a second node and leaving the outdoor unit display-less.

## Antenna and battery

- Plastic enclosure; antenna **external or at least not inside metal**,
  vertical, away from the battery pack and ground pour.
- Battery holder accessible without disturbing the sensor cable; lithium
  AAA only (alkaline leaks, and fails in cold).

## Test checklist (§16.4)

- [ ] Cold chamber −15 °C, 1 week: transmits, display gating works, battery under load logged
- [ ] +50 °C in sun: sensor vs reference, enclosure interior temperature
- [ ] Condensation cycling warm-humid → cold ×10, inspect
- [ ] Coverage edge: ADR settles, budget logic adapts
