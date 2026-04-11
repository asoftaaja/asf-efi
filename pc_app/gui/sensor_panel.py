"""Live sensor readout panel, refreshed at ~5 Hz."""

import tkinter as tk
from tkinter import ttk

from data_model import ECUState

REFRESH_MS = 200


class SensorPanel(ttk.LabelFrame):
    def __init__(self, parent, ecu_state: ECUState):
        super().__init__(parent, text="Sensors", padding=6)
        self._state = ecu_state
        self._map_editor = None   # set by MainWindow after construction
        self._pump_panel = None   # set by MainWindow after construction

        FONT = ("Courier", 28, "bold")

        fields = [
            ("RPM",    "_rpm_var",  "----"),
            ("TPS",    "_tps_var",  "--.- %"),
            ("FPS",    "_fps_var",  "-.-- bar"),
            ("IAT",    "_iat_var",  "---.- °C"),
            ("ET",     "_et_var",   "---.- °C"),
            ("PUMP",   "_pump_var", "OFF"),
            ("P.DUTY", "_pdut_var", "--- %"),
            ("I.DUTY", "_idut_var", "--.- %"),
            ("VBAT",   "_vbat_var", "--.- V"),
        ]

        for row, (label, attr, default) in enumerate(fields):
            ttk.Label(self, text=label + ":", anchor="e").grid(
                row=row, column=0, sticky="e", padx=(6, 2), pady=1)
            var = tk.StringVar(value=default)
            setattr(self, attr, var)
            ttk.Label(self, textvariable=var, font=FONT, anchor="w").grid(
                row=row, column=1, sticky="w", padx=(0, 6), pady=1)

        accel_row = len(fields)
        ttk.Label(self, text="ACCEL:", anchor="e").grid(
            row=accel_row, column=0, sticky="e", padx=(6, 2), pady=1)
        self._accel_label = tk.Label(self, text="---", font=FONT, anchor="w", foreground="gray")
        self._accel_label.grid(row=accel_row, column=1, sticky="w", padx=(0, 6), pady=1)

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
                self._pump_var.set("ON" if data.pump_active else "OFF")
                self._pdut_var.set(f"{data.pump_duty / 255 * 100:.0f} %")
                self._idut_var.set(f"{data.inj_duty:.1f} %")
                self._vbat_var.set(f"{data.bat_v:.2f} V")
                if data.accel_active:
                    self._accel_label.config(text="ACTIVE", foreground="#FF8C00")
                else:
                    self._accel_label.config(text="---", foreground="gray")

                if self._map_editor:
                    self._map_editor.update_cursor(data.rpm, data.tps)
                if self._pump_panel:
                    self._pump_panel.sync_state(data.pump_active)
        self._schedule()
