"""
Shared ECU state container. Thread-safe via a single Lock for sensor data.
The GUI thread reads; the serial worker thread writes.
"""

import threading
from typing import List, Optional
from protocol import (
    SensorData, PIDParams, PressureConfig, AccelPumpParams,
    RPM_BINS, TPS_BINS, IAT_BINS, ET_BINS,
    RPM_BREAKPOINTS, TPS_BREAKPOINTS,
)


class ECUState:
    def __init__(self):
        # ── Sensor snapshot (written by serial worker, read by GUI) ──────────
        self._sensors_lock = threading.Lock()
        self._sensors = None  # type: Optional[SensorData]
        self.sensor_fresh = threading.Event()   # set when new sensor data arrives
        self.log_sensor_fresh = threading.Event()  # parallel event consumed by DataLogger thread
        self.map_fresh    = threading.Event()   # set when map is loaded from device
        self.config_fresh = threading.Event()   # set when pump config loaded from device
        self.device_read_complete = threading.Event()  # set when all startup reads done

        # ── Tunable parameters (GUI is source of truth, sent to ECU on demand) ─
        self.inj_map = [[0] * TPS_BINS for _ in range(RPM_BINS)]  # type: List[List[int]]
        self.pid = PIDParams()
        self.pressure = PressureConfig()
        self.accel_pump = AccelPumpParams()
        self.pump_mode_always_on: bool = False
        self.iat_corr = [1.0] * IAT_BINS   # type: List[float]
        self.et_corr  = [1.0] * ET_BINS    # type: List[float]
        self.rpm_axis = list(RPM_BREAKPOINTS)  # type: List[int]
        self.tps_axis = list(TPS_BREAKPOINTS)  # type: List[float]
        self.axis_fresh = threading.Event()

        # ── Device-read buffers (populated on connect; not applied to state automatically) ──
        self.device_map_buf = None       # type: Optional[List[List[int]]]
        self.device_rpm_axis_buf = None  # type: Optional[List[int]]
        self.device_tps_axis_buf = None  # type: Optional[List[float]]
        self.device_iat_corr_buf = None  # type: Optional[List[float]]
        self.device_et_corr_buf  = None  # type: Optional[List[float]]
        self.device_pid_buf       = None  # type: Optional[PIDParams]
        self.device_pressure_buf  = None  # type: Optional[PressureConfig]
        self.device_pump_mode_buf = None  # type: Optional[bool]
        self.device_accel_pump_buf = None  # type: Optional[AccelPumpParams]

        # ── Alarm thresholds (local to PC app, not sent to device) ───────────
        self.et_alarm_threshold: float = 70.0    # °C — alert if ET exceeds this
        self.vbat_alarm_threshold: float = 11.5   # V  — alert if VBAT drops below this

        # ── Connection state ─────────────────────────────────────────────────
        self.connected = False

    # ── Sensor access ────────────────────────────────────────────────────────

    def update_sensors(self, data: SensorData) -> None:
        with self._sensors_lock:
            self._sensors = data
        self.sensor_fresh.set()
        self.log_sensor_fresh.set()

    def update_inj_map(self, new_map: List[List[int]]) -> None:
        self.inj_map = new_map
        self.map_fresh.set()

    def update_axis(self, rpm_pts: List[int], tps_pts: List[float]) -> None:
        self.rpm_axis = rpm_pts
        self.tps_axis = tps_pts
        self.axis_fresh.set()

    def buffer_device_map(self, new_map: List[List[int]]) -> None:
        self.device_map_buf = new_map
        self.map_fresh.set()

    def buffer_device_axis(self, rpm_pts: List[int], tps_pts: List[float]) -> None:
        self.device_rpm_axis_buf = rpm_pts
        self.device_tps_axis_buf = tps_pts
        self.axis_fresh.set()

    def buffer_device_corrections(self, iat: List[float], et: List[float]) -> None:
        self.device_iat_corr_buf = iat
        self.device_et_corr_buf  = et
        self.config_fresh.set()

    def get_sensors(self):  # type: () -> Optional[SensorData]
        with self._sensors_lock:
            return self._sensors
