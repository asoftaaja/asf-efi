"""Root Tk window — wires all panels together."""

import copy
import tkinter as tk
from tkinter import ttk
from typing import Optional

from data_model import ECUState
from serial_worker import SerialWorker
import tune_io
from protocol import (
    CMD_WRITE_MAP, CMD_WRITE_AXIS, CMD_WRITE_PID,
    CMD_WRITE_PRESSURE, CMD_WRITE_IAT_CORR, CMD_WRITE_ET_CORR,
    encode_map, encode_axis, encode_pid, encode_pressure, encode_corrections,
)
from gui.connection_panel  import ConnectionPanel
from gui.sensor_panel      import SensorPanel
from gui.map_editor        import MapEditor
from gui.pid_panel         import PIDPanel
from gui.pressure_panel    import PressurePanel
from gui.correction_panel  import CorrectionPanel
from gui.pump_panel        import PumpPanel
from gui.tune_file_panel   import TuneFilePanel


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

        self._sensor_panel = SensorPanel(self._body_frame, self._state)
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
        self._notebook.add(tune_tab, text="  PID & Pressure  ")

        tune_inner = ttk.Frame(tune_tab)
        tune_inner.pack(padx=8, pady=8, fill="x")

        self._pid_panel = PIDPanel(tune_inner, self._state, self._get_worker)
        self._pid_panel.grid(row=0, column=0, padx=8, pady=4, sticky="n")

        self._pressure_panel = PressurePanel(tune_inner, self._state, self._get_worker)
        self._pressure_panel.grid(row=0, column=1, padx=8, pady=4, sticky="n")

        self._pump_panel = PumpPanel(tune_inner, self._get_worker)
        self._pump_panel.grid(row=0, column=2, padx=8, pady=4, sticky="n")

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
            self._corr_panel,
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
        s = self._state
        if self._worker is not None and s.device_map_buf is not None:
            if not self._buffers_match_state():
                self._show_sync_warning(
                    "Tune file loaded — device has different values. "
                    "Write all to device, or load from device."
                )
            else:
                self._dismiss_sync_warning()
        else:
            self._dismiss_sync_warning()

    def _flush_all(self) -> None:
        self._pid_panel.flush_to_state()
        self._pressure_panel.flush_to_state()
        self._corr_panel.flush_to_state()

    def _refresh_all(self) -> None:
        self._map_editor.refresh_from_state()
        self._map_editor._refresh_axis_from_state()
        self._pid_panel.refresh_from_state()
        self._pressure_panel.refresh_from_state()
        self._corr_panel.refresh_from_state()

    # ── Worker accessor (passed as callable to panels) ───────────────────────

    def _get_worker(self) -> Optional[SerialWorker]:
        return self._worker

    # ── Connection callbacks ──────────────────────────────────────────────────

    def _on_connect(self, worker: SerialWorker) -> None:
        self._worker = worker
        self._set_panels_enabled(True)
        self._state.map_fresh.clear()
        self._state.axis_fresh.clear()
        self._state.device_map_buf = None
        self._state.device_rpm_axis_buf = None
        self._state.device_tps_axis_buf = None
        self._dismiss_sync_warning()
        # One-shot check — the worker reads map + axis at startup within ~0.5 s
        self.after(1500, self._check_map_loaded)

    def _check_map_loaded(self) -> None:
        has_data = (self._state.map_fresh.is_set() or
                    self._state.axis_fresh.is_set())
        if has_data and not self._buffers_match_state():
            self._show_sync_warning(
                "Device values differ from the loaded tune file. "
                "Write all to device, or load from device."
            )

    def _on_disconnect(self) -> None:
        self._worker = None
        self._set_panels_enabled(False)
        self._dismiss_sync_warning()

    # ── Sync warning helpers ──────────────────────────────────────────────────

    def _buffers_match_state(self) -> bool:
        s = self._state
        map_ok  = s.device_map_buf is None or s.device_map_buf == s.inj_map
        axis_ok = (s.device_rpm_axis_buf is None or
                   (s.device_rpm_axis_buf == s.rpm_axis and
                    s.device_tps_axis_buf == s.tps_axis))
        return map_ok and axis_ok

    def _show_sync_warning(self, message: str) -> None:
        self._sync_label.config(text=f"  \u26a0  {message}")
        self._sync_label.pack(side="left", fill="x", expand=True)
        self._sync_write_btn.pack(side="right", padx=4, pady=2)
        self._sync_load_btn.pack(side="right", padx=4, pady=2)
        self._sync_dismiss_btn.pack(side="right", padx=4, pady=2)
        self._sync_bar.pack(fill="x", padx=8, pady=(0, 2), before=self._body_frame)

    def _dismiss_sync_warning(self) -> None:
        self._sync_bar.pack_forget()

    def _load_device_values(self) -> None:
        s = self._state
        if s.device_map_buf is not None:
            s.inj_map = s.device_map_buf
            self._map_editor.refresh_from_state()
        if s.device_rpm_axis_buf is not None:
            s.rpm_axis = s.device_rpm_axis_buf
            s.tps_axis = s.device_tps_axis_buf
            self._map_editor._refresh_axis_from_state()
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
        worker.send_command(CMD_WRITE_IAT_CORR, encode_corrections(s.iat_corr))
        worker.send_command(CMD_WRITE_ET_CORR,  encode_corrections(s.et_corr))
        # Update device buffers to reflect what was just written, so future
        # tune file loads don't falsely flag a mismatch.
        s.device_map_buf      = copy.deepcopy(s.inj_map)
        s.device_rpm_axis_buf = list(s.rpm_axis)
        s.device_tps_axis_buf = list(s.tps_axis)
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
