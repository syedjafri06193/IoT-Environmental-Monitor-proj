import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from airtime_calc import messages_per_day, time_on_air_ms  # noqa: E402
from power_analysis import analyse, load_csv, project  # noqa: E402


# ---------------------------------------------------------------- airtime
def test_airtime_matches_reference():
    # Published calculators (avbentem): 20 B PHY payload (7 B app) at SF12/SF7.
    assert time_on_air_ms(12, 7) == pytest.approx(1318.9, abs=0.1)
    assert time_on_air_ms(7, 7) == pytest.approx(56.6, abs=0.1)
    # Our 9-byte v1 payload
    assert time_on_air_ms(12, 9) == pytest.approx(1482.8, abs=0.1)
    assert time_on_air_ms(7, 9) == pytest.approx(56.6, abs=0.1)


def test_low_dr_opt_matters_at_sf12():
    without = time_on_air_ms(12, 9, low_dr_opt=0)
    assert time_on_air_ms(12, 9) > without


def test_fair_use_message_counts():
    assert messages_per_day(12, 9) == 20
    assert messages_per_day(12, 7) == 22   # the design doc's "22/day" figure
    assert messages_per_day(7, 9) == 530


def test_matches_firmware_implementation():
    # firmware/src/airtime.c rounds up to whole ms
    import math
    for sf in range(7, 13):
        for n in (1, 7, 9, 12, 51):
            assert math.ceil(time_on_air_ms(sf, n) - 1e-9) >= time_on_air_ms(sf, n)


# ---------------------------------------------------------------- power
def synthetic_trace(path, cycles=4, period_s=60.0, sleep_ua=3.0, tx_ma=45.0, tx_s=1.483):
    """1 kHz-ish trace: sleep floor with a wake+TX+RX burst every period."""
    rows = ["Timestamp(ms),Current(uA),D0-D7"]
    t = 0.0
    dt = 0.001
    burst_len = 0.05 + tx_s + 0.2
    while t < cycles * period_s:
        phase = t % period_s
        if phase < 0.05:
            i = 5000.0                    # wake + sensor
        elif phase < 0.05 + tx_s:
            i = tx_ma * 1000.0            # transmit
        elif phase < burst_len:
            i = 5000.0                    # RX windows
        else:
            i = sleep_ua
        rows.append("%.3f,%.3f,00000000" % (t * 1000.0, i))
        t += dt if phase < burst_len + 0.01 else 0.1
    path.write_text("\n".join(rows) + "\n")


def test_power_analysis_on_synthetic_trace(tmp_path):
    p = tmp_path / "ppk2.csv"
    synthetic_trace(p)
    times, currents = load_csv(str(p))
    a = analyse(times, currents)
    assert len(a.bursts) == 4
    assert a.sleep_ua == pytest.approx(3.0)
    expected_burst_uah = (0.05 * 5000 + 1.483 * 45000 + 0.2 * 5000) / 3600.0
    for b in a.bursts:
        assert b.charge_uah == pytest.approx(expected_burst_uah, rel=0.02)
        assert b.peak_ua == pytest.approx(45000)


def test_projection_at_20_uplinks_per_day(tmp_path):
    p = tmp_path / "ppk2.csv"
    synthetic_trace(p)
    a = analyse(*load_csv(str(p)))
    mah, pct = project(a, 1200, uplinks_per_day=20)
    # sleep 3 uA * 4368 h = 13 mAh, bursts ~19 uAh * 20 * 182 = 69 mAh
    assert 75 < mah < 90
    assert pct < 10


def test_units_in_milliamps(tmp_path):
    p = tmp_path / "otii.csv"
    p.write_text("Time (s),Current (mA)\n0,0.002\n1,0.002\n2,45\n3,0.002\n4,0.002\n")
    times, currents = load_csv(str(p))
    assert currents[0] == pytest.approx(2.0)
    assert times[2] == pytest.approx(2.0)
    a = analyse(times, currents)
    assert len(a.bursts) == 1
