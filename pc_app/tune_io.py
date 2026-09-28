"""Tune file save/load — JSON format, stored in tunefiles/ folder."""

import json
from pathlib import Path
from typing import Optional

from protocol import (PIDParams, PressureConfig, AccelPumpParams,
                      ShiftCutParams, PowerbandParams,
                      RPM_BREAKPOINTS, TPS_BREAKPOINTS,
                      quantize_q8_8, quantize_f32, quantize_tps)

TUNEFILES_DIR = Path("tunefiles")
_LAST_FILE = TUNEFILES_DIR / ".last"


def _ensure_dir() -> None:
    TUNEFILES_DIR.mkdir(exist_ok=True)


def save_tunefile(path: "Path | str", state) -> None:
    _ensure_dir()
    path = Path(path)
    data = {
        "inj_map": state.inj_map,
        "pid": {"kp": state.pid.kp, "ki": state.pid.ki, "kd": state.pid.kd},
        "pressure": {
            "low_bar": state.pressure.low_bar,
            "high_bar": state.pressure.high_bar,
            "threshold_rpm": state.pressure.threshold_rpm,
        },
        "iat_corr": state.iat_corr,
        "et_corr": state.et_corr,
        "rpm_axis": state.rpm_axis,
        "tps_axis": state.tps_axis,
        "pump_mode_always_on": state.pump_mode_always_on,
        "accel_pump": {
            "threshold_pct_per_s": state.accel_pump.threshold_pct_per_s,
            "extra_us": state.accel_pump.extra_us,
            "duration_ms": state.accel_pump.duration_ms,
        },
        "powerband": {
            "multiplier": state.powerband.multiplier,
            "threshold_rpm": state.powerband.threshold_rpm,
            "threshold_tps_pct": state.powerband.threshold_tps_pct,
            "delay_rev": state.powerband.delay_rev,
        },
        "shift_cut": {
            "enabled": state.shift_cut.enabled,
            "duration_ms": state.shift_cut.duration_ms,
            "min_rpm": state.shift_cut.min_rpm,
            "lockout_ms": state.shift_cut.lockout_ms,
        },
        "alarms": {
            "et_threshold": state.et_alarm_threshold,
            "vbat_threshold": state.vbat_alarm_threshold,
        },
    }
    path.write_text(json.dumps(data, indent=2))
    _set_last(path)


def _load_tps_axis(raw) -> list:
    """Normalise a tune file's TPS axis to 0.0–1.0 fractions, quantised to the wire.

    Tune files written before the TPS units fix stored percent here, because the
    TPS_BREAKPOINTS default they fell back to was in percent. This is the one place
    that shim lives: everything downstream may assume a fraction.
    """
    vals = [float(v) for v in raw]
    if vals and max(vals) > 1.0:
        vals = [v / 100.0 for v in vals]
    return [quantize_tps(v) for v in vals]


def load_tunefile(path: "Path | str", state) -> None:
    """Load a tune file into `state`.

    Every value the device narrows on the wire is quantised on the way in, so state
    holds exactly what the device will hold. Without this, a tune-file 1.05 never
    equals the device's 1.05078125 and the GUI reports a mismatch that cannot be
    cleared. See the quantize_* helpers in protocol.py.
    """
    path = Path(path)
    data = json.loads(path.read_text())
    state.inj_map = data["inj_map"]
    pid = data["pid"]
    state.pid = PIDParams(kp=quantize_f32(pid["kp"]),
                          ki=quantize_f32(pid["ki"]),
                          kd=quantize_f32(pid["kd"]))
    p = data["pressure"]
    state.pressure = PressureConfig(
        low_bar=quantize_f32(p["low_bar"]),
        high_bar=quantize_f32(p["high_bar"]),
        threshold_rpm=int(p["threshold_rpm"]),
    )
    state.iat_corr = [quantize_q8_8(float(v)) for v in data["iat_corr"]]
    state.et_corr = [quantize_q8_8(float(v)) for v in data["et_corr"]]
    state.rpm_axis = [int(v) for v in data.get("rpm_axis", RPM_BREAKPOINTS)]
    state.tps_axis = _load_tps_axis(data.get("tps_axis", TPS_BREAKPOINTS))
    state.pump_mode_always_on = bool(data.get("pump_mode_always_on", False))
    ap = data.get("accel_pump", {})
    state.accel_pump = AccelPumpParams(
        threshold_pct_per_s=int(ap.get("threshold_pct_per_s", 50)),
        extra_us=int(ap.get("extra_us", 500)),
        duration_ms=int(ap.get("duration_ms", 300)),
    )
    pb = data.get("powerband", {})
    state.powerband = PowerbandParams(
        multiplier=quantize_q8_8(float(pb.get("multiplier", 0.5))),
        threshold_rpm=int(pb.get("threshold_rpm", 9000)),
        threshold_tps_pct=int(pb.get("threshold_tps_pct", 30)),
        delay_rev=int(pb.get("delay_rev", 50)),
    )
    sc = data.get("shift_cut", {})
    state.shift_cut = ShiftCutParams(
        enabled=bool(sc.get("enabled", True)),
        duration_ms=int(sc.get("duration_ms", 50)),
        min_rpm=int(sc.get("min_rpm", 3000)),
        lockout_ms=int(sc.get("lockout_ms", 500)),
    )
    alarms = data.get("alarms", {})
    state.et_alarm_threshold   = float(alarms.get("et_threshold",   110.0))
    state.vbat_alarm_threshold = float(alarms.get("vbat_threshold",  11.5))
    _set_last(path)


def get_last_tunefile() -> Optional[Path]:
    if _LAST_FILE.exists():
        p = Path(_LAST_FILE.read_text().strip())
        if p.exists():
            return p
    return None


def _set_last(path: Path) -> None:
    _ensure_dir()
    _LAST_FILE.write_text(str(path))
