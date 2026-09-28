"""Shift cut (ignition cut on gear shift) parameter editor."""

import tkinter as tk
from tkinter import ttk
from typing import Callable

from protocol import (
    ShiftCutParams, encode_shift_cut, CMD_WRITE_SHIFT_CUT,
    SHIFT_CUT_MIN_MS, SHIFT_CUT_MAX_MS,
    SHIFT_LOCKOUT_MIN_MS, SHIFT_LOCKOUT_MAX_MS,
)
from data_model import ECUState


class ShiftCutPanel(ttk.LabelFrame):
    def __init__(self, parent, ecu_state: ECUState, get_worker: Callable):
        super().__init__(parent, text="Shift Cut", padding=8)
        self._state = ecu_state
        self._get_worker = get_worker

        p = ecu_state.shift_cut

        self._enabled_var = tk.BooleanVar(value=p.enabled)
        ttk.Checkbutton(self, text="Enabled", variable=self._enabled_var) \
            .grid(row=0, column=0, columnspan=2, sticky="w", padx=4, pady=3)

        params = [
            ("Cut duration (ms):", "_duration_var", str(p.duration_ms)),
            ("Min RPM:",           "_min_rpm_var",  str(p.min_rpm)),
            ("Lockout (ms):",      "_lockout_var",  str(p.lockout_ms)),
        ]

        for i, (label, attr, default) in enumerate(params):
            row = i + 1
            ttk.Label(self, text=label, anchor="e", width=16).grid(row=row, column=0, padx=4, pady=3)
            var = tk.StringVar(value=default)
            setattr(self, attr, var)
            ttk.Entry(self, textvariable=var, width=10).grid(row=row, column=1, padx=4)

        self._send_btn = ttk.Button(self, text="Send", command=self._send)
        self._send_btn.grid(row=4, column=0, columnspan=2, pady=(6, 0))

        self._status_var = tk.StringVar()
        ttk.Label(self, textvariable=self._status_var).grid(row=5, column=0, columnspan=2)

    def _send(self) -> None:
        worker = self._get_worker()
        if worker is None:
            self._status_var.set("Not connected")
            return
        try:
            params = ShiftCutParams(
                enabled=bool(self._enabled_var.get()),
                duration_ms=int(self._duration_var.get()),
                min_rpm=int(self._min_rpm_var.get()),
                lockout_ms=int(self._lockout_var.get()),
            )
            if not (SHIFT_CUT_MIN_MS <= params.duration_ms <= SHIFT_CUT_MAX_MS):
                self._status_var.set(
                    "Duration must be %d-%d ms" % (SHIFT_CUT_MIN_MS, SHIFT_CUT_MAX_MS))
                return
            if not (SHIFT_LOCKOUT_MIN_MS <= params.lockout_ms <= SHIFT_LOCKOUT_MAX_MS):
                self._status_var.set(
                    "Lockout must be %d-%d ms" % (SHIFT_LOCKOUT_MIN_MS, SHIFT_LOCKOUT_MAX_MS))
                return
            if not (0 <= params.min_rpm <= 65535):
                raise ValueError
        except ValueError:
            self._status_var.set("Invalid value")
            return

        self._state.shift_cut = params
        fut = worker.send_command(CMD_WRITE_SHIFT_CUT, encode_shift_cut(params))
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
        p = self._state.shift_cut
        self._enabled_var.set(p.enabled)
        self._duration_var.set(str(p.duration_ms))
        self._min_rpm_var.set(str(p.min_rpm))
        self._lockout_var.set(str(p.lockout_ms))

    def flush_to_state(self) -> None:
        try:
            self._state.shift_cut = ShiftCutParams(
                enabled=bool(self._enabled_var.get()),
                duration_ms=int(self._duration_var.get()),
                min_rpm=int(self._min_rpm_var.get()),
                lockout_ms=int(self._lockout_var.get()),
            )
        except ValueError:
            pass
