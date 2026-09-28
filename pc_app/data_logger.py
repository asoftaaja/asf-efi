"""CSV data logger for ECU sensor readings.

Runs on its own worker thread so GUI activity (modal dialogs, heavy
redraws) and slow disk I/O cannot pause logging or freeze the UI.
"""

import csv
import os
import threading
from datetime import datetime
from pathlib import Path
from typing import Optional

from data_model import ECUState
from protocol import SensorData

_HEADER_ROWS = 5
_COLUMNS = [
    "timestamp", "rpm", "tps_pct", "fps_bar", "iat_degc", "et_degc",
    "pump_active", "bat_v", "pump_duty_pct", "inj_duty_pct", "inj_open_ms", "accel_active",
    "powerband_active", "powerband_mult",
]
_SAMPLE_PERIOD_S = 0.2     # 5 Hz cadence; matches sensor packet rate
_FLUSH_INTERVAL_ROWS = 5   # flush every ~1 s


class DataLogger:
    """Manages a single CSV log session on a background thread."""

    def __init__(self) -> None:
        self._file = None
        self._writer = None
        self._thread: Optional[threading.Thread] = None
        self._stop_evt = threading.Event()
        self._state: Optional[ECUState] = None
        self._rows_since_flush = 0
        self.error_msg: Optional[str] = None

    @property
    def is_active(self) -> bool:
        """Return True if a log session is currently running."""
        return self._thread is not None and self._thread.is_alive()

    def start(self, log_dir: str, ecu_state: ECUState) -> str:
        """
        Open a new log file in log_dir and spawn the worker thread.

        Returns the path of the created file.
        """
        if self.is_active:
            self.stop()

        Path(log_dir).mkdir(parents=True, exist_ok=True)
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        path = os.path.join(log_dir, f"asf_efi_datalog_{ts}.csv")

        self._file = open(path, "w", newline="", encoding="utf-8")
        for _ in range(_HEADER_ROWS):
            self._file.write("#\n")
        self._writer = csv.DictWriter(self._file, fieldnames=_COLUMNS)
        self._writer.writeheader()
        self._file.flush()

        self._state = ecu_state
        self._rows_since_flush = 0
        self.error_msg = None
        self._stop_evt.clear()
        self._state.log_sensor_fresh.clear()

        self._thread = threading.Thread(target=self._run, name="DataLoggerWorker",
                                        daemon=True)
        self._thread.start()
        return path

    def stop(self) -> None:
        """Signal the worker to exit, then close the file."""
        self._stop_evt.set()
        if self._state is not None:
            self._state.log_sensor_fresh.set()   # wake the worker if blocked on wait()
        if self._thread is not None:
            self._thread.join(timeout=1.0)
            self._thread = None
        self._close_file()
        self._state = None

    def _close_file(self) -> None:
        if self._file is not None:
            try:
                self._file.flush()
                self._file.close()
            except OSError:
                pass
            self._file = None
            self._writer = None

    def _run(self) -> None:
        """
        Worker loop: wait for fresh sensor data, write one row, repeat.

        Stale frames (no new packet within _SAMPLE_PERIOD_S) are skipped —
        the gap is visible from the timestamp column in the log.
        """
        assert self._state is not None
        while not self._stop_evt.is_set():
            got = self._state.log_sensor_fresh.wait(timeout=_SAMPLE_PERIOD_S)
            if self._stop_evt.is_set():
                break
            if not got:
                continue
            self._state.log_sensor_fresh.clear()
            data = self._state.get_sensors()
            if data is None:
                continue
            try:
                self._write_row(data)
            except OSError as exc:
                self.error_msg = f"log write failed: {exc}"
                break

    def _write_row(self, data: SensorData) -> None:
        """Append one sensor reading row and flush periodically."""
        self._writer.writerow({
            "timestamp":    datetime.now().isoformat(timespec="milliseconds"),
            "rpm":          data.rpm,
            "tps_pct":      f"{data.tps * 100:.2f}",
            "fps_bar":      f"{data.fps_bar:.3f}",
            "iat_degc":     f"{data.iat_degc:.1f}",
            "et_degc":      f"{data.et_degc:.1f}",
            "pump_active":  int(data.pump_active),
            "bat_v":        f"{data.bat_v:.2f}",
            "pump_duty_pct": f"{data.pump_duty / 255 * 100:.2f}",
            "inj_duty_pct": f"{data.inj_duty:.2f}",
            "inj_open_ms":  f"{data.inj_open_us / 1000:.1f}",
            "accel_active": int(data.accel_active),
            "powerband_active": int(data.powerband_active),
            "powerband_mult": f"{data.powerband_mult:.3f}",
        })
        self._rows_since_flush += 1
        if self._rows_since_flush >= _FLUSH_INTERVAL_ROWS:
            self._file.flush()
            self._rows_since_flush = 0
