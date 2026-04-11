"""Alarm threshold settings panel (local to PC app, not sent to device)."""

import tkinter as tk
from tkinter import ttk

from data_model import ECUState


class AlarmPanel(ttk.LabelFrame):
    def __init__(self, parent, ecu_state: ECUState):
        super().__init__(parent, text="Alarms", padding=8)
        self._state = ecu_state

        fields = [
            ("ET alarm above (°C):",  "_et_var",   str(ecu_state.et_alarm_threshold)),
            ("VBAT alarm below (V):", "_vbat_var", str(ecu_state.vbat_alarm_threshold)),
        ]

        for row, (label, attr, default) in enumerate(fields):
            ttk.Label(self, text=label, anchor="e", width=16).grid(
                row=row, column=0, padx=4, pady=3)
            var = tk.StringVar(value=default)
            setattr(self, attr, var)
            ttk.Entry(self, textvariable=var, width=10).grid(row=row, column=1, padx=4)

        ttk.Button(self, text="Apply", command=self._apply).grid(
            row=2, column=0, columnspan=2, pady=(6, 0))

        self._status_var = tk.StringVar()
        ttk.Label(self, textvariable=self._status_var).grid(row=3, column=0, columnspan=2)

    def _apply(self) -> None:
        ok = True
        try:
            self._state.et_alarm_threshold = float(self._et_var.get())
        except ValueError:
            self._status_var.set("Invalid ET value")
            ok = False
        try:
            self._state.vbat_alarm_threshold = float(self._vbat_var.get())
        except ValueError:
            self._status_var.set("Invalid VBAT value")
            ok = False
        if ok:
            self._status_var.set("Applied")

    def refresh_from_state(self) -> None:
        self._et_var.set(str(self._state.et_alarm_threshold))
        self._vbat_var.set(str(self._state.vbat_alarm_threshold))

    def flush_to_state(self) -> None:
        try:
            self._state.et_alarm_threshold = float(self._et_var.get())
        except ValueError:
            pass
        try:
            self._state.vbat_alarm_threshold = float(self._vbat_var.get())
        except ValueError:
            pass
