"""CSV data logger for ECU sensor readings."""

import csv
import os
from datetime import datetime
from pathlib import Path

from protocol import SensorData

_HEADER_ROWS = 5
_COLUMNS = [
    "timestamp", "rpm", "tps_pct", "fps_bar", "iat_degc", "et_degc",
    "pump_active", "bat_v", "pump_duty_pct", "inj_duty_pct", "inj_open_ms", "accel_active",
]


class DataLogger:
    """Manages a single CSV log session."""

    def __init__(self) -> None:
        self._file = None
        self._writer = None

    @property
    def is_active(self) -> bool:
        """Return True if a log file is open and recording."""
        return self._file is not None

    def start(self, log_dir: str) -> str:
        """
        Open a new log file in log_dir and write the reserved header.

        Returns the path of the created file.
        """
        if self._file is not None:
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
        return path

    def log(self, data: SensorData) -> None:
        """Append one sensor reading row to the open log file."""
        if self._writer is None:
            return
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
        })
        self._file.flush()

    def stop(self) -> None:
        """Flush and close the current log file."""
        if self._file is not None:
            self._file.flush()
            self._file.close()
            self._file = None
            self._writer = None
