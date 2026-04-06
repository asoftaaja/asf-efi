"""PID coefficient editor."""

import tkinter as tk
from tkinter import ttk
from typing import Callable

from protocol import PIDParams, encode_pid, CMD_WRITE_PID
from data_model import ECUState


class PIDPanel(ttk.LabelFrame):
    def __init__(self, parent, ecu_state: ECUState, get_worker: Callable):
        super().__init__(parent, text="PID Coefficients", padding=8)
        self._state = ecu_state
        self._get_worker = get_worker

        params = [
            ("Kp", "_kp_var", ecu_state.pid.kp),
            ("Ki", "_ki_var", ecu_state.pid.ki),
            ("Kd", "_kd_var", ecu_state.pid.kd),
        ]

        for row, (label, attr, default) in enumerate(params):
            ttk.Label(self, text=label + ":", width=4, anchor="e").grid(row=row, column=0, padx=4, pady=3)
            var = tk.StringVar(value=str(default))
            setattr(self, attr, var)
            ttk.Entry(self, textvariable=var, width=10).grid(row=row, column=1, padx=4)

        self._send_btn = ttk.Button(self, text="Send PID", command=self._send)
        self._send_btn.grid(row=3, column=0, columnspan=2, pady=(6, 0))

        self._status_var = tk.StringVar()
        ttk.Label(self, textvariable=self._status_var).grid(row=4, column=0, columnspan=2)

    def _send(self) -> None:
        worker = self._get_worker()
        if worker is None:
            self._status_var.set("Not connected")
            return
        try:
            params = PIDParams(
                kp=float(self._kp_var.get()),
                ki=float(self._ki_var.get()),
                kd=float(self._kd_var.get()),
            )
        except ValueError:
            self._status_var.set("Invalid value")
            return

        self._state.pid = params
        fut = worker.send_command(CMD_WRITE_PID, encode_pid(params))
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
        self._kp_var.set(str(self._state.pid.kp))
        self._ki_var.set(str(self._state.pid.ki))
        self._kd_var.set(str(self._state.pid.kd))

    def flush_to_state(self) -> None:
        try:
            self._state.pid = PIDParams(
                kp=float(self._kp_var.get()),
                ki=float(self._ki_var.get()),
                kd=float(self._kd_var.get()),
            )
        except ValueError:
            pass
