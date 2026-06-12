"""Root Tk window — wires all panels together."""

import copy
import os
import tkinter as tk
from tkinter import ttk
from typing import Optional

from data_model import ECUState
from serial_worker import SerialWorker
import tune_io
from protocol import (
    CMD_WRITE_MAP, CMD_WRITE_AXIS, CMD_WRITE_PID,
    CMD_WRITE_PRESSURE, CMD_WRITE_IAT_CORR, CMD_WRITE_ET_CORR,
    CMD_WRITE_ACCEL_PUMP,
    encode_map, encode_axis, encode_pid, encode_pressure, encode_corrections,
    encode_accel_pump,
)
from gui.connection_panel  import ConnectionPanel
from gui.sensor_panel      import SensorPanel
from gui.map_editor        import MapEditor
from gui.pid_panel         import PIDPanel
from gui.pressure_panel    import PressurePanel
from gui.correction_panel  import CorrectionPanel
from gui.pump_panel        import PumpPanel
from gui.tune_file_panel   import TuneFilePanel
from gui.accel_pump_panel  import AccelPumpPanel
from gui.alarm_panel       import AlarmPanel


class MainWindow(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("ASF EFI Tuner")
        self.resizable(True, True)

        self._state = ECUState()
        self._worker: Optional[SerialWorker] = None

        self._build_ui()
        self._set_panels_enabled(False)
        self._autoload_tunefile()

    # ── Layout ────────────────────────────────────────────────────────────────

    def _build_ui(self) -> None:
        # Connection bar (always visible at top)
        self._conn_panel = ConnectionPanel(
            self,
            self._state,
            on_connect=self._on_connect,
            on_disconnect=self._on_disconnect,
        )
        self._conn_panel.pack(fill="x", padx=8, pady=(8, 4))

        # Tune file bar
        self._tune_file_panel = TuneFilePanel(
            self, self._state,
            on_loaded=self._on_tunefile_loaded,
            on_save=self._flush_all,
        )
        self._tune_file_panel.pack(fill="x", padx=8, pady=(0, 4))

        # Write-all toolbar
        write_bar = ttk.Frame(self)
        write_bar.pack(fill="x", padx=8, pady=(0, 2))
        self._write_all_btn = ttk.Button(
            write_bar, text="Write all to device", command=self._write_all_to_device
        )
        self._write_all_btn.pack(side="left", padx=2)

        # Sync warning bar (packed/forgotten as a whole when warning is shown/hidden)
        self._sync_bar = tk.Frame(self, background="#b8860b")
        self._sync_label = tk.Label(
            self._sync_bar, text="", foreground="white",
            background="#b8860b", anchor="w", padx=6, pady=3,
        )
        self._sync_write_btn = ttk.Button(
            self._sync_bar, text="Write all to device", command=self._write_all_to_device
        )
        self._sync_load_btn = ttk.Button(
            self._sync_bar, text="Load from device", command=self._load_device_values
        )
        self._sync_dismiss_btn = ttk.Button(
            self._sync_bar, text="Dismiss", command=self._dismiss_sync_warning
        )

        # Body: notebook on the left, sensor panel on the right
        self._body_frame = ttk.Frame(self)
        self._body_frame.pack(fill="both", expand=True, padx=8, pady=4)

        _log_dir = os.path.join(os.path.dirname(__file__), "..", "logs")
        self._sensor_panel = SensorPanel(self._body_frame, self._state, log_dir=_log_dir)
        self._sensor_panel.pack(side="right", fill="y", padx=(4, 0))

        ttk.Separator(self._body_frame, orient="vertical").pack(
            side="right", fill="y", pady=4)

        # Notebook tabs for tuning panels
        self._notebook = ttk.Notebook(self._body_frame)
        self._notebook.pack(side="left", fill="both", expand=True)

        # ── Tab 1: Injection Map ──────────────────────────────────────────────
        map_tab = ttk.Frame(self._notebook)
        self._notebook.add(map_tab, text="  Injection Map  ")

        self._map_editor = MapEditor(map_tab, self._state, self._get_worker)
        self._map_editor.pack(padx=8, pady=8)

        # Wire sensor panel → map editor for cursor updates
        self._sensor_panel.set_map_editor(self._map_editor)

        # ── Tab 2: PID / Pressure / Pump ─────────────────────────────────────
        tune_tab = ttk.Frame(self._notebook)
        self._notebook.add(tune_tab, text="  ECU Settings  ")

        tune_inner = ttk.Frame(tune_tab)
        tune_inner.pack(padx=8, pady=8, fill="x")

        # PID + Pressure grouped in a single container panel
        engine_panel = ttk.LabelFrame(tune_inner, text="Fuel Pressure Control", padding=4)
        engine_panel.grid(row=0, column=0, padx=8, pady=4, sticky="n")

        self._pid_panel = PIDPanel(engine_panel, self._state, self._get_worker)
        self._pid_panel.grid(row=0, column=0, padx=6, pady=4, sticky="n")

        ttk.Separator(engine_panel, orient="vertical").grid(
            row=0, column=1, sticky="ns", pady=4)

        self._pressure_panel = PressurePanel(engine_panel, self._state, self._get_worker)
        self._pressure_panel.grid(row=0, column=2, padx=6, pady=4, sticky="n")

        self._pump_panel = PumpPanel(tune_inner, self._get_worker)
        self._pump_panel.grid(row=0, column=1, padx=8, pady=4, sticky="n")

        self._accel_pump_panel = AccelPumpPanel(tune_inner, self._state, self._get_worker)
        self._accel_pump_panel.grid(row=1, column=0, padx=8, pady=4, sticky="nw")

        self._alarm_panel = AlarmPanel(tune_inner, self._state)
        self._alarm_panel.grid(row=1, column=1, padx=8, pady=4, sticky="nw")

        # Wire sensor panel → pump panel for button state sync
        self._sensor_panel.set_pump_panel(self._pump_panel)

        # ── Tab 3: Temperature Corrections ───────────────────────────────────
        corr_tab = ttk.Frame(self._notebook)
        self._notebook.add(corr_tab, text="  Corrections  ")

        self._corr_panel = CorrectionPanel(corr_tab, self._state, self._get_worker)
        self._corr_panel.pack(padx=8, pady=8)

        # Collect tuning panels for enable/disable
        self._tuning_panels = [
            self._map_editor,
            self._pid_panel,
            self._pressure_panel,
            self._pump_panel,
            self._accel_pump_panel,
            self._corr_panel,
            self._alarm_panel,
            self._write_all_btn,
        ]

    # ── Tune file helpers ─────────────────────────────────────────────────────

    def _autoload_tunefile(self) -> None:
        last = tune_io.get_last_tunefile()
        if last is None:
            return
        try:
            tune_io.load_tunefile(last, self._state)
        except Exception:
            return
        self._tune_file_panel.set_path(last)
        self._on_tunefile_loaded()

    def _on_tunefile_loaded(self) -> None:
        self._refresh_all()
        if self._worker is not None and self._state.device_read_complete.is_set():
            diffs = self._diff_device_vs_state()
            if diffs:
                self._show_sync_warning(
                    "Tune file loaded — device differs. "
                    "Write all to device, or load from device. "
                    "Differs: " + ", ".join(diffs)
                )
            else:
                self._dismiss_sync_warning()
        else:
            self._dismiss_sync_warning()

    def _flush_all(self) -> None:
        self._pid_panel.flush_to_state()
        self._pressure_panel.flush_to_state()
        self._corr_panel.flush_to_state()
        self._accel_pump_panel.flush_to_state()
        self._alarm_panel.flush_to_state()

    def _refresh_all(self) -> None:
        self._map_editor.refresh_from_state()
        self._map_editor._refresh_axis_from_state()
        self._pid_panel.refresh_from_state()
        self._pressure_panel.refresh_from_state()
        self._corr_panel.refresh_from_state()
        self._accel_pump_panel.refresh_from_state()
        self._alarm_panel.refresh_from_state()

    # ── Worker accessor (passed as callable to panels) ───────────────────────

    def _get_worker(self) -> Optional[SerialWorker]:
        return self._worker

    # ── Connection callbacks ──────────────────────────────────────────────────

    def _on_connect(self, worker: SerialWorker) -> None:
        self._worker = worker
        self._set_panels_enabled(True)
        self._sensor_panel.set_logging_enabled(True)
        self._state.map_fresh.clear()
        self._state.axis_fresh.clear()
        self._state.config_fresh.clear()
        self._state.device_read_complete.clear()
        self._state.device_map_buf = None
        self._state.device_rpm_axis_buf = None
        self._state.device_tps_axis_buf = None
        self._state.device_iat_corr_buf = None
        self._state.device_et_corr_buf  = None
        self._state.device_pid_buf = None
        self._state.device_pressure_buf = None
        self._state.device_pump_mode_buf = None
        self._state.device_accel_pump_buf = None
        self._dismiss_sync_warning()
        # Poll for completion — worker takes up to ~5 s if any reads time out.
        self._sync_poll_attempts = 0
        self.after(200, self._poll_device_read)

    def _poll_device_read(self) -> None:
        if self._worker is None:
            return
        if self._state.device_read_complete.is_set():
            self._apply_device_values_and_warn()
            return
        self._sync_poll_attempts += 1
        if self._sync_poll_attempts > 40:   # ~8 s @ 200 ms
            self._apply_device_values_and_warn()
            return
        self.after(200, self._poll_device_read)

    def _apply_device_values_and_warn(self) -> None:
        """Diff device buffers against tune-file state, apply device values to
        state/GUI, then warn if any field differed."""
        diffs = self._diff_device_vs_state()
        self._load_device_values(show_warning=False)
        if diffs:
            self._show_sync_warning(
                "Tune file values differ from device — device values are shown. "
                "Differs: " + ", ".join(diffs)
            )
        else:
            self._dismiss_sync_warning()

    def _on_disconnect(self) -> None:
        self._worker = None
        self._set_panels_enabled(False)
        self._sensor_panel.set_logging_enabled(False)
        self._dismiss_sync_warning()

    # ── Sync warning helpers ──────────────────────────────────────────────────

    def _diff_device_vs_state(self) -> list:
        """Return list of human-readable field names whose device buffer differs
        from the corresponding tune-file value in state. Buffers that are None
        (read failed) are skipped."""
        s = self._state
        diffs = []
        if s.device_map_buf is not None and s.device_map_buf != s.inj_map:
            diffs.append("injection map")
        if s.device_rpm_axis_buf is not None and (
                s.device_rpm_axis_buf != s.rpm_axis or
                s.device_tps_axis_buf != s.tps_axis):
            diffs.append("axis breakpoints")
        if s.device_iat_corr_buf is not None and s.device_iat_corr_buf != s.iat_corr:
            diffs.append("IAT correction")
        if s.device_et_corr_buf is not None and s.device_et_corr_buf != s.et_corr:
            diffs.append("ET correction")
        if s.device_pid_buf is not None and (
                s.device_pid_buf.kp != s.pid.kp or
                s.device_pid_buf.ki != s.pid.ki or
                s.device_pid_buf.kd != s.pid.kd):
            diffs.append("PID")
        if s.device_pressure_buf is not None and (
                s.device_pressure_buf.low_bar != s.pressure.low_bar or
                s.device_pressure_buf.high_bar != s.pressure.high_bar or
                s.device_pressure_buf.threshold_rpm != s.pressure.threshold_rpm):
            diffs.append("pressure config")
        if s.device_pump_mode_buf is not None and s.device_pump_mode_buf != s.pump_mode_always_on:
            diffs.append("pump mode")
        if s.device_accel_pump_buf is not None and (
                s.device_accel_pump_buf.threshold_pct_per_s != s.accel_pump.threshold_pct_per_s or
                s.device_accel_pump_buf.extra_us != s.accel_pump.extra_us or
                s.device_accel_pump_buf.duration_ms != s.accel_pump.duration_ms):
            diffs.append("accel pump")
        return diffs

    def _show_sync_warning(self, message: str) -> None:
        self._sync_label.config(text=f"  \u26a0  {message}")
        self._sync_label.pack(side="left", fill="x", expand=True)
        self._sync_write_btn.pack(side="right", padx=4, pady=2)
        self._sync_load_btn.pack(side="right", padx=4, pady=2)
        self._sync_dismiss_btn.pack(side="right", padx=4, pady=2)
        self._sync_bar.pack(fill="x", padx=8, pady=(0, 2), before=self._body_frame)

    def _dismiss_sync_warning(self) -> None:
        self._sync_bar.pack_forget()

    def _load_device_values(self, show_warning: bool = True) -> None:
        s = self._state
        if s.device_map_buf is not None:
            s.inj_map = s.device_map_buf
            self._map_editor.refresh_from_state()
        if s.device_rpm_axis_buf is not None:
            s.rpm_axis = s.device_rpm_axis_buf
            s.tps_axis = s.device_tps_axis_buf
            self._map_editor._refresh_axis_from_state()
        if s.device_iat_corr_buf is not None:
            s.iat_corr = s.device_iat_corr_buf
            s.et_corr  = s.device_et_corr_buf
            self._corr_panel.refresh_from_state()
        if s.device_pid_buf is not None:
            s.pid = s.device_pid_buf
            self._pid_panel.refresh_from_state()
        if s.device_pressure_buf is not None:
            s.pressure = s.device_pressure_buf
        if s.device_pump_mode_buf is not None:
            s.pump_mode_always_on = s.device_pump_mode_buf
        if s.device_pressure_buf is not None or s.device_pump_mode_buf is not None:
            self._pressure_panel.refresh_from_state()
        if s.device_accel_pump_buf is not None:
            s.accel_pump = s.device_accel_pump_buf
            self._accel_pump_panel.refresh_from_state()
        if show_warning:
            self._dismiss_sync_warning()

    def _write_all_to_device(self) -> None:
        worker = self._worker
        if worker is None:
            return
        self._flush_all()
        s = self._state
        worker.send_command(CMD_WRITE_MAP,      encode_map(s.inj_map))
        worker.send_command(CMD_WRITE_AXIS,     encode_axis(s.rpm_axis, s.tps_axis))
        worker.send_command(CMD_WRITE_PID,      encode_pid(s.pid))
        worker.send_command(CMD_WRITE_PRESSURE, encode_pressure(s.pressure))
        worker.send_command(CMD_WRITE_IAT_CORR,   encode_corrections(s.iat_corr))
        worker.send_command(CMD_WRITE_ET_CORR,    encode_corrections(s.et_corr))
        worker.send_command(CMD_WRITE_ACCEL_PUMP, encode_accel_pump(s.accel_pump))
        # Update device buffers to reflect what was just written, so future
        # tune file loads don't falsely flag a mismatch.
        s.device_map_buf      = copy.deepcopy(s.inj_map)
        s.device_rpm_axis_buf = list(s.rpm_axis)
        s.device_tps_axis_buf = list(s.tps_axis)
        s.device_iat_corr_buf = list(s.iat_corr)
        s.device_et_corr_buf  = list(s.et_corr)
        s.device_pid_buf      = copy.deepcopy(s.pid)
        s.device_pressure_buf = copy.deepcopy(s.pressure)
        s.device_pump_mode_buf = s.pump_mode_always_on
        s.device_accel_pump_buf = copy.deepcopy(s.accel_pump)
        self._dismiss_sync_warning()

    # ── Enable/disable tuning panels ─────────────────────────────────────────

    def _set_panels_enabled(self, enabled: bool) -> None:
        state = "normal" if enabled else "disabled"
        for panel in self._tuning_panels:
            try:
                panel.configure(state=state)
            except tk.TclError:
                pass
            try:
                for child in panel.winfo_children():
                    try:
                        child.configure(state=state)
                    except tk.TclError:
                        pass
            except tk.TclError:
                pass
