#!/usr/bin/env python3
"""LoRa time-on-air and fair-use budget calculator (design doc §3, §15.1).

Usage:
    python3 tools/airtime_calc.py                 # table for the 9-byte payload
    python3 tools/airtime_calc.py --payload 12    # table for another size
    python3 tools/airtime_calc.py --sf 12 --payload 9
"""
import argparse
import math

LORAWAN_OVERHEAD = 13  # MHDR(1) + FHDR(7) + FPort(1) + MIC(4)
TTN_FAIR_USE_MS = 30_000
EU868_1PCT_MS = 864_000
SECONDS_PER_DAY = 86_400


def time_on_air_ms(sf, payload_bytes, bw_khz=125, cr=1, preamble=8,
                   header=True, crc=True, low_dr_opt=None):
    """LoRa airtime per the SX127x/SX126x datasheet formula.

    `payload_bytes` is the LoRaWAN *application* payload; the 13 bytes of
    MAC overhead are added here.
    """
    if not 6 <= sf <= 12:
        raise ValueError("sf must be 6..12")
    t_sym = (2 ** sf) / (bw_khz * 1000) * 1000  # ms

    if low_dr_opt is None:
        low_dr_opt = 1 if (sf >= 11 and bw_khz == 125) else 0

    t_preamble = (preamble + 4.25) * t_sym

    pl = payload_bytes + LORAWAN_OVERHEAD
    num = 8 * pl - 4 * sf + 28 + 16 * crc - 20 * (not header)
    den = 4 * (sf - 2 * low_dr_opt)
    n_payload = max(math.ceil(num / den) * (cr + 4), 0) + 8

    return t_preamble + n_payload * t_sym


def messages_per_day(sf, payload_bytes, limit_ms=TTN_FAIR_USE_MS):
    return int(limit_ms // time_on_air_ms(sf, payload_bytes))


def min_interval_s(sf, payload_bytes, limit_ms=TTN_FAIR_USE_MS):
    n = messages_per_day(sf, payload_bytes, limit_ms)
    return math.ceil(SECONDS_PER_DAY / n) if n else SECONDS_PER_DAY


def table(payload_bytes):
    rows = ["| SF | Airtime (ms) | msgs/day TTN 30 s | min interval TTN | msgs/day 1% reg |",
            "|---|---|---|---|---|"]
    for sf in range(7, 13):
        rows.append("| SF{} | {:.1f} | {} | {} min | {} |".format(
            sf, time_on_air_ms(sf, payload_bytes),
            messages_per_day(sf, payload_bytes),
            math.ceil(min_interval_s(sf, payload_bytes) / 60),
            messages_per_day(sf, payload_bytes, EU868_1PCT_MS)))
    return "\n".join(rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--payload", type=int, default=9, help="application payload bytes (default 9)")
    ap.add_argument("--sf", type=int, help="single spreading factor")
    args = ap.parse_args()
    if args.sf:
        print("SF{} / {} B: {:.1f} ms, {} msgs/day on TTN".format(
            args.sf, args.payload, time_on_air_ms(args.sf, args.payload),
            messages_per_day(args.sf, args.payload)))
    else:
        print(table(args.payload))


if __name__ == "__main__":
    main()
