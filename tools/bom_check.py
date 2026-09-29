#!/usr/bin/env python3
"""Sum the BOM's sleep currents and flag anything unreviewed (design doc §4.3).

    python3 tools/bom_check.py hardware/bom.csv
"""
import csv
import sys

SLEEP_BUDGET_UA = 10.0


def check(path):
    total, problems = 0.0, []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            try:
                ua = float(row["sleep_current_ua"] or 0) * max(int(row["qty"] or 0), 0)
            except ValueError:
                problems.append("%s: sleep_current_ua %r is not a number" % (row["ref"], row["sleep_current_ua"]))
                continue
            total += ua
            if row["iq_reviewed"].strip().lower() not in ("yes", "n/a"):
                problems.append("%s (%s): quiescent current not reviewed" % (row["ref"], row["part"]))
    if total > SLEEP_BUDGET_UA:
        problems.append("total sleep current %.2f uA exceeds %.0f uA" % (total, SLEEP_BUDGET_UA))
    return total, problems


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "hardware/bom.csv"
    total, problems = check(path)
    print("BOM sleep current: %.2f uA (budget %.0f uA)" % (total, SLEEP_BUDGET_UA))
    for p in problems:
        print("  PROBLEM: " + p)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
