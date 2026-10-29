# Airtime budget (milestone M0)

Generated with `python3 tools/airtime_calc.py` (payload v1 = **9 bytes**,
125 kHz, CR 4/5, EU868). Re-run it whenever the payload changes.

| SF | Airtime | msgs/day on TTN (30 s) | min interval on TTN | msgs/day at 1% (864 s) |
|---|---|---|---|---|
| SF7 | 56.6 ms | 530 | 3 min | 15,271 |
| SF8 | 102.9 ms | 291 | 5 min | 8,395 |
| SF9 | 205.8 ms | 145 | 10 min | 4,197 |
| SF10 | 370.7 ms | 80 | 18 min | 2,330 |
| SF11 | 741.4 ms | 40 | 36 min | 1,165 |
| **SF12** | **1482.8 ms** | **20** | **72 min** | 582 |

## Decisions

- **Plan for SF12.** ADR picks the SF from link margin after installation;
  the same firmware can see a 26× swing in message rate (§3.3).
- **At SF12 on TTN the device sends 20 messages a day**, one every ~72 min.
  That is the number to be happy with before ordering parts.
- **Joins count.** A JoinRequest is 23 bytes on air (1.48 s at SF12) and is
  charged to the same budget by `lorawan.c`.
- **Firmware enforces this itself.** `airtime.c` tracks a rolling 24 h
  window, persists it in RTC backup registers across resets, rounds airtime
  *up* to whole ms, and derives the uplink interval from what is left.
  `test_app` proves that five resets inside a window don't reset the budget.

## A correction to the design doc

§3.2 and §16.2 quote **1318.9 ms / 22 msgs per day** at SF12 for a
"12-byte payload". Those figures are right for a **7-byte** application
payload (20 bytes on air). With the 13 bytes of LoRaWAN overhead:

| App payload | On air | SF12 airtime | TTN msgs/day |
|---|---|---|---|
| 7 B | 20 B | 1318.9 ms | 22 |
| 9 B (this design) | 22 B | 1482.8 ms | 20 |
| 12 B | 25 B | 1482.8 ms | 20 |

At SF12 the symbol boundaries mean 8–12 bytes all cost the same, so the
spare 3 bytes are free — a good place for the min/max aggregate stretch goal
(§17). The tests in this repo use the corrected values.

## US915

No duty cycle, but a 400 ms dwell limit makes SF11/SF12 unusable at 125 kHz.
`lorawan_port_stm32wl.c`'s DR→SF mapping is EU868-specific; change it with
the region.
