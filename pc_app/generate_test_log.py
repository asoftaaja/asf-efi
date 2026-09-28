"""
Generate a synthetic ASF EFI data log for testing the log viewer.

Scenario (60 s total at 200 ms intervals):
  0–5 s    warm idle    ~850 RPM, ~2 % TPS
  5–15 s   ramp up      RPM 850 → 3200, TPS 2 → 48 %
  15–17 s  blip / accel TPS spikes to ~55 %, accel pump active
  17–35 s  sustained    RPM 3800–4100, TPS 68–73 %
  35–50 s  deceleration RPM 4000 → 1000, TPS 70 → 15 %
  50–60 s  return idle  RPM ~850, TPS ~2 %

inj_open_ms is derived from inj_duty_pct and RPM using the same formula as
the firmware (sync mode below 3000 RPM, fixed 16667 µs period above).

Usage:
    python generate_test_log.py [output_path]

If output_path is omitted the file is written to
logs/asf_efi_datalog_<timestamp>.csv next to this script.
"""

import csv
import math
import os
import random
import sys
from datetime import datetime, timedelta
from pathlib import Path

# ── constants matching firmware ───────────────────────────────────────────────
RPM_SYNC_THRESHOLD = 3000
PERIOD_60HZ_US = 16_667

_HEADER_ROWS = 5
_COLUMNS = [
    "timestamp", "rpm", "tps_pct", "fps_bar", "iat_degc", "et_degc",
    "pump_active", "bat_v", "pump_duty_pct", "inj_duty_pct", "inj_open_ms", "accel_active",
    "powerband_active", "powerband_mult",
]

# Powerband settings for the simulated run. The thresholds are chosen to sit
# inside this scenario's RPM/TPS range so the ramp is visible in the output.
PB_MULTIPLIER    = 0.5
PB_THRESHOLD_RPM = 3500
PB_THRESHOLD_TPS = 50.0
PB_DELAY_REV     = 50

SEED = 42
INTERVAL_MS = 200
DURATION_S = 60.0


def _lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * max(0.0, min(1.0, t))


def _calc_inj_open_ms(rpm: int, inj_duty_pct: float) -> float:
    """Reproduce the firmware duty→pulse-width back-calculation."""
    if rpm == 0 or inj_duty_pct == 0:
        return 0.0
    period_us = PERIOD_60HZ_US if rpm >= RPM_SYNC_THRESHOLD else 60_000_000 / rpm
    return inj_duty_pct * period_us / 100_000


def _scenario(t: float, rng: random.Random) -> dict:
    """Return noiseless target values for elapsed time t (seconds)."""
    # ── RPM ──────────────────────────────────────────────────────────────────
    if t < 5:
        rpm_base = 850.0
    elif t < 15:
        rpm_base = _lerp(850, 3200, (t - 5) / 10)
    elif t < 17:
        rpm_base = _lerp(3200, 3900, (t - 15) / 2)
    elif t < 35:
        rpm_base = _lerp(3900, 4050, (t - 17) / 18)
    elif t < 50:
        rpm_base = _lerp(4050, 1000, (t - 35) / 15)
    else:
        rpm_base = _lerp(1000, 850, (t - 50) / 10)

    # ── TPS ──────────────────────────────────────────────────────────────────
    if t < 5:
        tps_base = 2.0
    elif t < 15:
        tps_base = _lerp(2, 48, (t - 5) / 10)
    elif t < 15.8:
        tps_base = _lerp(48, 56, (t - 15) / 0.8)   # fast blip up
    elif t < 17:
        tps_base = _lerp(56, 70, (t - 15.8) / 1.2)
    elif t < 35:
        tps_base = _lerp(70, 71, (t - 17) / 18)
    elif t < 50:
        tps_base = _lerp(71, 15, (t - 35) / 15)
    else:
        tps_base = _lerp(15, 2, (t - 50) / 10)

    # ── inj duty % (rough proportional to TPS/RPM load) ──────────────────────
    rpm_norm = min(rpm_base / 4000, 1.0)
    tps_norm = tps_base / 100.0
    inj_duty_base = 1.0 + 21.5 * tps_norm * (0.6 + 0.4 * rpm_norm)

    # ── fuel pressure — rises slightly with RPM then stabilises ───────────────
    fps_base = 2.95 + 0.6 * min(rpm_base / 4000, 1.0)

    # ── temperatures — slow climb ─────────────────────────────────────────────
    iat_base = 22.0 + t * 0.025
    et_base  = 55.0 + t * 0.17

    # ── pump duty — inversely proportional to pressure headroom ───────────────
    pump_duty_base = max(20.0, 65.0 - rpm_norm * 25.0)

    # ── battery — slight sag under load ──────────────────────────────────────
    bat_base = 13.8 - 0.3 * rpm_norm

    # ── accel pump active 15–17 s ─────────────────────────────────────────────
    accel = 1 if 15.0 <= t < 17.0 else 0

    return dict(
        rpm_base=rpm_base, tps_base=tps_base, inj_duty_base=inj_duty_base,
        fps_base=fps_base, iat_base=iat_base, et_base=et_base,
        pump_duty_base=pump_duty_base, bat_base=bat_base, accel=accel,
    )


def _advance_powerband(progress: int, rpm: int, tps_pct: float, revs: int) -> int:
    """Reproduce the firmware ramp: progress walks toward PB_DELAY_REV while
    both thresholds are met, and back toward 0 while either is not."""
    in_band = rpm >= PB_THRESHOLD_RPM and tps_pct >= PB_THRESHOLD_TPS
    if in_band:
        return min(progress + revs, PB_DELAY_REV)
    return max(progress - revs, 0)


def generate(output_path: str, seed: int = SEED) -> str:
    """Write the log to output_path and return the path."""
    rng = random.Random(seed)
    Path(os.path.dirname(output_path) or ".").mkdir(parents=True, exist_ok=True)

    t0 = datetime(2026, 5, 21, 12, 0, 0)
    steps = int(DURATION_S * 1000 / INTERVAL_MS)

    with open(output_path, "w", newline="", encoding="utf-8") as fh:
        for _ in range(_HEADER_ROWS):
            fh.write("#\n")
        writer = csv.DictWriter(fh, fieldnames=_COLUMNS)
        writer.writeheader()

        pb_progress = 0

        for i in range(steps):
            t = i * INTERVAL_MS / 1000.0
            s = _scenario(t, rng)

            rpm      = max(0, round(s["rpm_base"]      + rng.gauss(0, 18)))
            tps_pct  = max(0.0, s["tps_base"]          + rng.gauss(0, 0.35))
            fps_bar  = max(0.0, s["fps_base"]           + rng.gauss(0, 0.030))
            iat_degc = s["iat_base"]                    + rng.gauss(0, 0.15)
            et_degc  = s["et_base"]                     + rng.gauss(0, 0.25)
            bat_v    = s["bat_base"]                    + rng.gauss(0, 0.04)
            pump_pct = max(0.0, min(100.0,
                           s["pump_duty_base"]          + rng.gauss(0, 1.5)))
            inj_duty = max(0.0, s["inj_duty_base"]      + rng.gauss(0, 0.15))
            inj_ms   = _calc_inj_open_ms(rpm, inj_duty)

            # Crank revolutions elapsed since the previous sample drive the ramp
            revs = int(rpm / 60.0 * (INTERVAL_MS / 1000.0))
            pb_progress = _advance_powerband(pb_progress, rpm, tps_pct, revs)
            pb_mult = PB_MULTIPLIER + (1.0 - PB_MULTIPLIER) * pb_progress / PB_DELAY_REV

            ts = t0 + timedelta(milliseconds=i * INTERVAL_MS)
            writer.writerow({
                "timestamp":     ts.isoformat(timespec="milliseconds"),
                "rpm":           rpm,
                "tps_pct":       f"{tps_pct:.2f}",
                "fps_bar":       f"{fps_bar:.3f}",
                "iat_degc":      f"{iat_degc:.1f}",
                "et_degc":       f"{et_degc:.1f}",
                "pump_active":   1,
                "bat_v":         f"{bat_v:.2f}",
                "pump_duty_pct": f"{pump_pct:.2f}",
                "inj_duty_pct":  f"{inj_duty:.2f}",
                "inj_open_ms":   f"{inj_ms:.1f}",
                "accel_active":  s["accel"],
                "powerband_active": int(pb_progress >= PB_DELAY_REV),
                "powerband_mult":   f"{pb_mult:.3f}",
            })

    return output_path


def main() -> None:
    if len(sys.argv) > 1:
        path = sys.argv[1]
    else:
        log_dir = os.path.join(os.path.dirname(__file__), "logs")
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        path = os.path.join(log_dir, f"asf_efi_datalog_{ts}.csv")

    out = generate(path)
    print(f"Written: {out}")


if __name__ == "__main__":
    main()
