"""Live sensor readout panel, refreshed at ~5 Hz."""

import tkinter as tk
from tkinter import ttk

from data_model import ECUState
from data_logger import DataLogger

REFRESH_MS = 200
FLASH_MS   = 500   # flash period half-cycle (2 Hz total)

# Fields that can trigger alarms; mapped to (label_attr, var_attr)
_ALARM_FIELDS = {
    "fps":  ("_fps_label",  "_fps_var"),
    "et":   ("_et_label",   "_et_var"),
    "vbat": ("_vbat_label", "_vbat_var"),
}


class SensorPanel(ttk.LabelFrame):
    def __init__(self, parent, ecu_state: ECUState, log_dir: str = "logs"):
        super().__init__(parent, text="Sensors", padding=6)
        self._state = ecu_state
        self._map_editor = None   # set by MainWindow after construction
        self._pump_panel = None   # set by MainWindow after construction
        self._log_dir = log_dir
        self._logger = DataLogger()

        self._alarms: set = set()
        self._flash_on: bool = False
        self._alarm_default_fg: dict = {}

        FONT = ("Courier", 28, "bold")

        fields = [
            ("RPM",    "_rpm_var",  "----",     False),
            ("TPS",    "_tps_var",  "--.- %",   False),
            ("FPS",    "_fps_var",  "-.-- bar", True),
            ("IAT",    "_iat_var",  "---.- °C", False),
            ("ET",     "_et_var",   "---.- °C", True),
            ("PUMP",   "_pump_var", "OFF",      False),
            ("P.DUTY", "_pdut_var", "--- %",    False),
            ("I.DUTY", "_idut_var", "--.- %",    False),
            ("INJ",    "_inj_var",  "--.- ms",   False),
            ("VBAT",   "_vbat_var", "--.- V",    True),
        ]

        # Map from alarm key to label widget
        _alarm_key_for_var = {"_fps_var": "fps", "_et_var": "et", "_vbat_var": "vbat"}

        for row, (label, attr, default, alarmable) in enumerate(fields):
            ttk.Label(self, text=label + ":", anchor="e").grid(
                row=row, column=0, sticky="e", padx=(6, 2), pady=1)
            var = tk.StringVar(value=default)
            setattr(self, attr, var)
            if alarmable:
                lbl = tk.Label(self, textvariable=var, font=FONT, anchor="w")
                alarm_key = _alarm_key_for_var[attr]
                setattr(self, f"_{alarm_key}_label", lbl)
                self._alarm_default_fg[alarm_key] = lbl.cget("foreground")
            else:
                lbl = ttk.Label(self, textvariable=var, font=FONT, anchor="w")
            lbl.grid(row=row, column=1, sticky="w", padx=(0, 6), pady=1)

        accel_row = len(fields)
        ttk.Label(self, text="ACCEL:", anchor="e").grid(
            row=accel_row, column=0, sticky="e", padx=(6, 2), pady=1)
        self._accel_label = tk.Label(self, text="---", font=FONT, anchor="w", foreground="gray")
        self._accel_label.grid(row=accel_row, column=1, sticky="w", padx=(0, 6), pady=1)

        # Logging controls
        log_row = accel_row + 1
        ttk.Separator(self, orient="horizontal").grid(
            row=log_row, column=0, columnspan=2, sticky="ew", pady=(8, 4))
        self._log_btn = ttk.Button(self, text="Start Log", command=self._toggle_log,
                                   state="disabled")
        self._log_btn.grid(row=log_row + 1, column=0, columnspan=2, pady=(0, 2))
        self._rec_label = tk.Label(self, text="", foreground="red",
                                   font=("Courier", 11, "bold"))
        self._rec_label.grid(row=log_row + 2, column=0, columnspan=2)

        self._schedule()
        self._flash_schedule()

    def set_map_editor(self, editor) -> None:
        self._map_editor = editor

    def set_pump_panel(self, panel) -> None:
        self._pump_panel = panel

    def set_logging_enabled(self, enabled: bool) -> None:
        """Enable or disable the log button. Stops any active log when disabling."""
        if not enabled:
            self.stop_log()
        self._log_btn.configure(state="normal" if enabled else "disabled")

    def stop_log(self) -> None:
        """Stop the active log session if one is running."""
        if self._logger.is_active:
            self._logger.stop()
            self._log_btn.configure(text="Start Log")
            self._rec_label.configure(text="")

    def _toggle_log(self) -> None:
        """Start or stop the log session."""
        if self._logger.is_active:
            self.stop_log()
        else:
            path = self._logger.start(self._log_dir)
            self._log_btn.configure(text="Stop Log")
            self._rec_label.configure(text="● RECORDING")

    def _schedule(self) -> None:
        self.after(REFRESH_MS, self._refresh)

    def _flash_schedule(self) -> None:
        self.after(FLASH_MS, self._flash_tick)

    def _flash_tick(self) -> None:
        self._flash_on = not self._flash_on
        for key, (lbl_attr, _) in _ALARM_FIELDS.items():
            lbl = getattr(self, lbl_attr)
            default_fg = self._alarm_default_fg.get(key, "black")
            if key in self._alarms:
                lbl.config(foreground="red" if self._flash_on else default_fg)
            else:
                lbl.config(foreground=default_fg)
        self._flash_schedule()

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
                self._inj_var.set(f"{data.inj_open_us / 1000:.1f} ms")
                self._vbat_var.set(f"{data.bat_v:.2f} V")
                if data.accel_active:
                    self._accel_label.config(text="ACTIVE", foreground="#FF8C00")
                else:
                    self._accel_label.config(text="---", foreground="gray")

                self._update_alarms(data)

                if self._logger.is_active:
                    self._logger.log(data)

                if self._map_editor:
                    self._map_editor.update_cursor(data.rpm, data.tps)
                if self._pump_panel:
                    self._pump_panel.sync_state(data.pump_active)
        self._schedule()

    def _update_alarms(self, data) -> None:
        pressure = self._state.pressure
        rpm = data.rpm
        target = pressure.high_bar if rpm >= pressure.threshold_rpm else pressure.low_bar

        self._alarms.clear()
        if data.fps_bar < (target - 0.2):
            self._alarms.add("fps")
        if data.et_degc > self._state.et_alarm_threshold:
            self._alarms.add("et")
        if data.bat_v < self._state.vbat_alarm_threshold:
            self._alarms.add("vbat")
