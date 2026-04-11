"""Accelerator pump enrichment parameter editor."""

import tkinter as tk
from tkinter import ttk
from typing import Callable

from protocol import AccelPumpParams, encode_accel_pump, CMD_WRITE_ACCEL_PUMP
from data_model import ECUState


class AccelPumpPanel(ttk.LabelFrame):
    def __init__(self, parent, ecu_state: ECUState, get_worker: Callable):
        super().__init__(parent, text="Accel Pump", padding=8)
        self._state = ecu_state
        self._get_worker = get_worker

        p = ecu_state.accel_pump
        params = [
            ("Threshold (%/s):", "_thresh_var",   str(p.threshold_pct_per_s)),
            ("Extra pulse (us):", "_extra_var",    str(p.extra_us)),
            ("Duration (ms):",    "_duration_var", str(p.duration_ms)),
        ]

        for row, (label, attr, default) in enumerate(params):
            ttk.Label(self, text=label, anchor="e", width=16).grid(row=row, column=0, padx=4, pady=3)
            var = tk.StringVar(value=default)
            setattr(self, attr, var)
            ttk.Entry(self, textvariable=var, width=10).grid(row=row, column=1, padx=4)

        self._send_btn = ttk.Button(self, text="Send", command=self._send)
        self._send_btn.grid(row=3, column=0, columnspan=2, pady=(6, 0))

        self._status_var = tk.StringVar()
        ttk.Label(self, textvariable=self._status_var).grid(row=4, column=0, columnspan=2)

    def _send(self) -> None:
        worker = self._get_worker()
        if worker is None:
            self._status_var.set("Not connected")
            return
        try:
            params = AccelPumpParams(
                threshold_pct_per_s=int(self._thresh_var.get()),
                extra_us=int(self._extra_var.get()),
                duration_ms=int(self._duration_var.get()),
            )
            for v in (params.threshold_pct_per_s, params.extra_us, params.duration_ms):
                if not (0 <= v <= 65535):
                    raise ValueError
        except ValueError:
            self._status_var.set("Invalid value")
            return

        self._state.accel_pump = params
        fut = worker.send_command(CMD_WRITE_ACCEL_PUMP, encode_accel_pump(params))
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
        p = self._state.accel_pump
        self._thresh_var.set(str(p.threshold_pct_per_s))
        self._extra_var.set(str(p.extra_us))
        self._duration_var.set(str(p.duration_ms))

    def flush_to_state(self) -> None:
        try:
            self._state.accel_pump = AccelPumpParams(
                threshold_pct_per_s=int(self._thresh_var.get()),
                extra_us=int(self._extra_var.get()),
                duration_ms=int(self._duration_var.get()),
            )
        except ValueError:
            pass
