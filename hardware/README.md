# Hardware

This folder holds the BOM now and the schematic/PCB at milestone M7.

- `bom.csv` — every part has a **`sleep_current_ua`** column and an
  **`iq_reviewed`** flag. Review this column before ordering: the regulator,
  a stray power LED, an always-on divider or a leaky bulk cap each exceed the
  entire ~5 µA sleep budget on their own (design doc §4.3).
  `tools/bom_check.py` sums it and fails if the total exceeds 10 µA.
- `schematic/`, `pcb/` — to be added at M7. Keep antenna clearance from the
  battery and ground pour (§9.4), and re-measure sleep current on the real
  board (leakage paths appear that a breadboard doesn't have).

Firmware pin assignments live in `firmware/platform/stm32wl/board.h`.
