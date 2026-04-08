"""Live sensor readout panel, refreshed at ~5 Hz."""

import tkinter as tk
from tkinter import ttk

from data_model import ECUState

REFRESH_MS = 200


class SensorPanel(ttk.LabelFrame):
    def __init__(self, parent, ecu_state: ECUState):
        super().__init__(parent, text="Live Sensors", padding=6)
        self._state = ecu_state
        self._map_editor = None   # set by MainWindow after construction
        self._pump_panel = None   # set by MainWindow after construction

        FONT = ("Courier", 30, "bold")

        # Define fields: (label text, var attribute name, default, row)
        fields = [
            ("RPM",  "_rpm_var",  "----   ",  0),
            ("TPS",  "_tps_var",  "--.- %",   0),
            ("FPS",  "_fps_var",  "-.-- bar", 0),
            ("IAT",  "_iat_var",  "---.- °C", 1),
            ("ET",   "_et_var",   "---.- °C", 1),
            ("PUMP", "_pump_var", "OFF",       1),
            ("VBAT", "_vbat_var", "--.- V",   1),
        ]

        row_col = [0, 0]  # current column index per row
        for label, attr, default, row in fields:
            col = row_col[row]
            ttk.Label(self, text=label + ":", font=("TkDefaultFont", 10)).grid(
                row=row, column=col * 2, sticky="e", padx=(8, 2))
            var = tk.StringVar(value=default)
            setattr(self, attr, var)
            lbl = ttk.Label(self, textvariable=var, font=FONT, width=10, anchor="w")
            lbl.grid(row=row, column=col * 2 + 1, sticky="w", padx=(0, 8))
            row_col[row] += 1

        self._schedule()

    def set_map_editor(self, editor) -> None:
        self._map_editor = editor

    def set_pump_panel(self, panel) -> None:
        self._pump_panel = panel

    def _schedule(self) -> None:
        self.after(REFRESH_MS, self._refresh)

    def _refresh(self) -> None:
        if self._state.sensor_fresh.is_set():
            self._state.sensor_fresh.clear()
            data = self._state.get_sensors()
            if data:
                self._rpm_var.set(f"{data.rpm}")
                self._tps_var.set(f"{data.tps * 100:.1f} %")
                self._fps_var.set(f"{data.fps_bar:.2f} bar")
                self._iat_var.set(f"{data.iat_degc:.1f} °C")
                self._et_var.set(f"{data.et_degc:.1f} °C")
                pump_text = "ON" if data.pump_active else "OFF"
                self._pump_var.set(pump_text)
                self._vbat_var.set(f"{data.bat_v:.2f} V")

                if self._map_editor:
                    self._map_editor.update_cursor(data.rpm, data.tps)
                if self._pump_panel:
                    self._pump_panel.sync_state(data.pump_active)
        self._schedule()
