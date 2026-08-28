"""Tune file save/load — JSON format, stored in tunefiles/ folder."""

import json
from pathlib import Path
from typing import Optional

from protocol import (PIDParams, PressureConfig, AccelPumpParams, ShiftCutParams,
                      RPM_BREAKPOINTS, TPS_BREAKPOINTS)

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


def load_tunefile(path: "Path | str", state) -> None:
    path = Path(path)
    data = json.loads(path.read_text())
    state.inj_map = data["inj_map"]
    pid = data["pid"]
    state.pid = PIDParams(kp=float(pid["kp"]), ki=float(pid["ki"]), kd=float(pid["kd"]))
    p = data["pressure"]
    state.pressure = PressureConfig(
        low_bar=float(p["low_bar"]),
        high_bar=float(p["high_bar"]),
        threshold_rpm=int(p["threshold_rpm"]),
    )
    state.iat_corr = [float(v) for v in data["iat_corr"]]
    state.et_corr = [float(v) for v in data["et_corr"]]
    state.rpm_axis = [int(v) for v in data.get("rpm_axis", RPM_BREAKPOINTS)]
    state.tps_axis = [float(v) for v in data.get("tps_axis", TPS_BREAKPOINTS)]
    state.pump_mode_always_on = bool(data.get("pump_mode_always_on", False))
    ap = data.get("accel_pump", {})
    state.accel_pump = AccelPumpParams(
        threshold_pct_per_s=int(ap.get("threshold_pct_per_s", 50)),
        extra_us=int(ap.get("extra_us", 500)),
        duration_ms=int(ap.get("duration_ms", 300)),
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
