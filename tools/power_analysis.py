#!/usr/bin/env python3
"""PPK2 / Otii current trace -> charge per cycle and projected battery life.

Design doc §11: calculated budgets are always optimistic; this turns a
measured trace into the numbers that prove (or refute) the six-month claim.

Input: CSV with a time column in ms (or s) and a current column in uA (or mA,
A). Nordic Power Profiler exports "Timestamp(ms),Current(uA),..." which is
detected automatically; for other tools pass --time-col/--current-col/--units.

    python3 tools/power_analysis.py trace.csv
    python3 tools/power_analysis.py trace.csv --uplinks-per-day 20 --battery-mah 1200
"""
import argparse
import csv
import statistics
import sys
from dataclasses import dataclass, field

UNIT_TO_UA = {"ua": 1.0, "ma": 1000.0, "a": 1_000_000.0, "na": 0.001}


@dataclass
class Burst:
    start_s: float
    end_s: float
    charge_uah: float
    peak_ua: float

    @property
    def duration_s(self):
        return self.end_s - self.start_s


@dataclass
class Analysis:
    duration_s: float
    total_uah: float
    sleep_ua: float
    bursts: list = field(default_factory=list)

    @property
    def average_ua(self):
        return self.total_uah * 3600.0 / self.duration_s if self.duration_s else 0.0


def load_csv(path, time_col=None, current_col=None, time_unit=None, current_unit=None):
    with open(path, newline="") as f:
        reader = csv.reader(f)
        header = next(reader)
        lower = [h.strip().lower() for h in header]

        def find(pred):
            for i, h in enumerate(lower):
                if pred(h):
                    return i
            return None

        ti = header.index(time_col) if time_col else find(lambda h: h.startswith("timestamp") or h.startswith("time"))
        ci = header.index(current_col) if current_col else find(lambda h: h.startswith("current"))
        if ti is None or ci is None:
            raise ValueError("could not find time/current columns in %r; use --time-col/--current-col" % header)

        t_unit = time_unit or ("s" if "(s)" in lower[ti] else "ms")
        c_unit = current_unit or next((u for u in ("ua", "ma", "na") if "(%s)" % u in lower[ci]),
                                      "a" if "(a)" in lower[ci] else "ua")
        t_scale = 1.0 if t_unit == "s" else 0.001
        c_scale = UNIT_TO_UA[c_unit]

        times, currents = [], []
        for row in reader:
            if len(row) <= max(ti, ci) or not row[ti].strip():
                continue
            times.append(float(row[ti]) * t_scale)
            currents.append(float(row[ci]) * c_scale)
    return times, currents


def analyse(times, currents, threshold_ua=100.0, merge_gap_s=0.05):
    """Integrates the trace; splits it into sleep and active bursts."""
    if len(times) < 2:
        raise ValueError("trace too short")
    total = 0.0
    bursts = []
    sleep_samples = []
    cur = None
    last_active_t = None

    for i in range(1, len(times)):
        dt = times[i] - times[i - 1]
        if dt <= 0:
            continue
        i_ua = currents[i - 1]
        q = i_ua * dt / 3600.0  # uAh
        total += q
        t = times[i - 1]
        if i_ua >= threshold_ua:
            if cur is None or (last_active_t is not None and t - last_active_t > merge_gap_s):
                cur = Burst(t, t + dt, 0.0, 0.0)
                bursts.append(cur)
            cur.end_s = t + dt
            cur.charge_uah += q
            cur.peak_ua = max(cur.peak_ua, i_ua)
            last_active_t = t
        else:
            sleep_samples.append(i_ua)
            if cur is not None and last_active_t is not None and t - last_active_t <= merge_gap_s:
                cur.charge_uah += q  # dips inside a burst (e.g. between RX windows)
                cur.end_s = t + dt

    sleep_ua = statistics.median(sleep_samples) if sleep_samples else 0.0
    return Analysis(times[-1] - times[0], total, sleep_ua, bursts)


def project(a, battery_mah=1200.0, days=182.0, uplinks_per_day=None):
    """Projected consumption over `days`.

    With `uplinks_per_day`, scales the measured mean burst (one burst = one
    wake cycle) to that rate on top of the sleep floor; otherwise
    extrapolates the trace's own average current.
    """
    hours = days * 24.0
    if uplinks_per_day and a.bursts:
        per_cycle = statistics.mean(b.charge_uah for b in a.bursts)
        mah = (a.sleep_ua * hours + per_cycle * uplinks_per_day * days) / 1000.0
    else:
        mah = a.average_ua * hours / 1000.0
    return mah, 100.0 * mah / battery_mah


def report(a, battery_mah, uplinks_per_day):
    lines = [
        "trace          %.1f s, %d active bursts" % (a.duration_s, len(a.bursts)),
        "charge         %.3f uAh total, average %.2f uA" % (a.total_uah, a.average_ua),
        "sleep floor    %.2f uA (median below threshold)%s" % (
            a.sleep_ua, "   ** over 10 uA - check regulator I_q, LEDs, pull-ups (§4.3)" if a.sleep_ua > 10 else ""),
    ]
    if a.bursts:
        charges = [b.charge_uah for b in a.bursts]
        lines.append("per burst      mean %.2f uAh, min %.2f, max %.2f, peak %.1f mA, mean %.0f ms" % (
            statistics.mean(charges), min(charges), max(charges),
            max(b.peak_ua for b in a.bursts) / 1000.0,
            1000.0 * statistics.mean(b.duration_s for b in a.bursts)))
    mah, pct = project(a, battery_mah, uplinks_per_day=uplinks_per_day)
    basis = "%g cycles/day" % uplinks_per_day if uplinks_per_day else "trace average"
    lines.append("6 months       %.0f mAh = %.0f%% of %.0f mAh (%s)%s" % (
        mah, pct, battery_mah, basis, "   ** exceeds battery" if pct > 100 else ""))
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("csv")
    ap.add_argument("--threshold-ua", type=float, default=100.0, help="active/sleep split (default 100 uA)")
    ap.add_argument("--battery-mah", type=float, default=1200.0)
    ap.add_argument("--uplinks-per-day", type=float, help="project at this cycle rate")
    ap.add_argument("--time-col")
    ap.add_argument("--current-col")
    ap.add_argument("--units", choices=sorted(UNIT_TO_UA), help="current units if not in header")
    args = ap.parse_args(argv)

    times, currents = load_csv(args.csv, args.time_col, args.current_col, current_unit=args.units)
    print(report(analyse(times, currents, args.threshold_ua), args.battery_mah, args.uplinks_per_day))
    return 0


if __name__ == "__main__":
    sys.exit(main())
