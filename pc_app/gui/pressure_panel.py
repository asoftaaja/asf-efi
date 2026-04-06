"""Fuel pressure target editor."""

import tkinter as tk
from tkinter import ttk
from typing import Callable

from protocol import PressureConfig, encode_pressure, CMD_WRITE_PRESSURE
from data_model import ECUState


class PressurePanel(ttk.LabelFrame):
    def __init__(self, parent, ecu_state: ECUState, get_worker: Callable):
        super().__init__(parent, text="Fuel Pressure", padding=8)
        self._state = ecu_state
        self._get_worker = get_worker

        cfg = ecu_state.pressure
        fields = [
            ("Low bar:",       "_low_var",       str(cfg.low_bar)),
            ("High bar:",      "_high_var",      str(cfg.high_bar)),
            ("Threshold RPM:", "_thresh_var",    str(cfg.threshold_rpm)),
        ]

        for row, (label, attr, default) in enumerate(fields):
            ttk.Label(self, text=label, anchor="e", width=14).grid(row=row, column=0, padx=4, pady=3)
            var = tk.StringVar(value=default)
            setattr(self, attr, var)
            ttk.Entry(self, textvariable=var, width=10).grid(row=row, column=1, padx=4)

        self._send_btn = ttk.Button(self, text="Send Pressure", command=self._send)
        self._send_btn.grid(row=3, column=0, columnspan=2, pady=(6, 0))

        self._status_var = tk.StringVar()
        ttk.Label(self, textvariable=self._status_var).grid(row=4, column=0, columnspan=2)

    def _send(self) -> None:
        worker = self._get_worker()
        if worker is None:
            self._status_var.set("Not connected")
            return
        try:
            cfg = PressureConfig(
                low_bar=float(self._low_var.get()),
                high_bar=float(self._high_var.get()),
                threshold_rpm=int(self._thresh_var.get()),
            )
        except ValueError:
            self._status_var.set("Invalid value")
            return

        self._state.pressure = cfg
        fut = worker.send_command(CMD_WRITE_PRESSURE, encode_pressure(cfg))
        self._send_btn.configure(state="disabled")
        self._status_var.set("Sending...")
        self.after(100, lambda: self._check_future(fut))

    def _check_future(self, fut) -> None:
        if not fut.done():
            self.after(100, lambda: self._check_future(fut))
            return
        self._send_btn.configure(state="normal")
        ok, err = fut.result()
        self._status_var.set("Sent OK" if ok else f"Failed: {err}")

    def refresh_from_state(self) -> None:
        self._low_var.set(str(self._state.pressure.low_bar))
        self._high_var.set(str(self._state.pressure.high_bar))
        self._thresh_var.set(str(self._state.pressure.threshold_rpm))

    def flush_to_state(self) -> None:
        try:
            self._state.pressure = PressureConfig(
                low_bar=float(self._low_var.get()),
                high_bar=float(self._high_var.get()),
                threshold_rpm=int(self._thresh_var.get()),
            )
        except ValueError:
            pass
