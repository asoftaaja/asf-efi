"""Pump prime button."""

import tkinter as tk
from tkinter import ttk
from typing import Callable

from protocol import CMD_PUMP_PRIME, CMD_PUMP_SET


PRIME_LOCKOUT_MS = 3000   # disable button for this long after pressing


class PumpPanel(ttk.LabelFrame):
    def __init__(self, parent, get_worker: Callable):
        super().__init__(parent, text="Fuel Pump", padding=8)
        self._get_worker = get_worker

        self._btn = ttk.Button(self, text="Prime Pump (2 s)", command=self._prime)
        self._btn.grid(row=0, column=0, padx=8, pady=4)

        self._pump_on = False
        self._toggle_btn = ttk.Button(self, text="Pump On (test)", command=self._toggle_pump)
        self._toggle_btn.grid(row=0, column=1, padx=8, pady=4)

        self._status_var = tk.StringVar()
        ttk.Label(self, textvariable=self._status_var, width=16).grid(row=1, column=0, columnspan=2, pady=(4, 0))

    def _prime(self) -> None:
        worker = self._get_worker()
        if worker is None:
            self._status_var.set("Not connected")
            return

        self._btn.configure(state="disabled")
        self._status_var.set("Priming...")

        fut = worker.send_command(CMD_PUMP_PRIME)
        self.after(100, lambda: self._check_future(fut))

    def _check_future(self, fut) -> None:
        if not fut.done():
            self.after(100, lambda: self._check_future(fut))
            return
        ok, err = fut.result()
        self._status_var.set("Priming..." if ok else f"Failed: {err}")
        # Re-enable after the prime duration + margin
        self.after(PRIME_LOCKOUT_MS, self._reset)

    def _reset(self) -> None:
        self._btn.configure(state="normal")
        self._status_var.set("")

    def _toggle_pump(self) -> None:
        worker = self._get_worker()
        if worker is None:
            self._status_var.set("Not connected")
            return

        self._toggle_btn.configure(state="disabled")
        new_state = not self._pump_on
        payload = bytes([1 if new_state else 0])
        fut = worker.send_command(CMD_PUMP_SET, payload)
        self.after(100, lambda: self._check_toggle(fut, new_state))

    def _check_toggle(self, fut, new_state: bool) -> None:
        if not fut.done():
            self.after(100, lambda: self._check_toggle(fut, new_state))
            return
        ok, err = fut.result()
        if ok:
            self._pump_on = new_state
            self._toggle_btn.configure(text="Pump Off (test)" if new_state else "Pump On (test)")
            self._status_var.set("Pump ON" if new_state else "")
        else:
            self._status_var.set(f"Failed: {err}")
        self._toggle_btn.configure(state="normal")
