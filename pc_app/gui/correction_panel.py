"""IAT and ET correction multiplier tables (5 values each), and TPS calibration."""

import tkinter as tk
from tkinter import ttk
from typing import Callable, List, Optional

from protocol import IAT_BINS, ET_BINS, IAT_CORR_TEMPS, ET_CORR_TEMPS, encode_corrections, CMD_WRITE_IAT_CORR, CMD_WRITE_ET_CORR, CMD_TPS_CAL_CLOSED, CMD_TPS_CAL_OPEN
from data_model import ECUState


class _CorrTable(ttk.LabelFrame):
    """A single correction table with temperature labels and a Send button."""

    def __init__(self, parent, title: str, values: List[float], temps: List[int], cmd: int, get_worker: Callable):
        super().__init__(parent, text=title, padding=6)
        self._cmd = cmd
        self._get_worker = get_worker
        self._n = len(values)
        self._vars: List[tk.StringVar] = []

        ttk.Label(self, text="°C",    width=5, anchor="center", font=("TkDefaultFont", 9, "bold")).grid(row=0, column=0)
        ttk.Label(self, text="Mult.", width=8, anchor="center", font=("TkDefaultFont", 9, "bold")).grid(row=0, column=1)

        for i, (v, t) in enumerate(zip(values, temps)):
            ttk.Label(self, text=str(t), width=5, anchor="e").grid(row=i + 1, column=0, padx=2, pady=1)
            var = tk.StringVar(value=f"{v:.4f}")
            self._vars.append(var)
            ttk.Entry(self, textvariable=var, width=8, justify="center").grid(row=i + 1, column=1, padx=2, pady=1)

        self._send_btn = ttk.Button(self, text="Send", command=self._send)
        self._send_btn.grid(row=self._n + 1, column=0, columnspan=2, pady=(6, 0))

        self._status_var = tk.StringVar()
        ttk.Label(self, textvariable=self._status_var, width=12).grid(row=self._n + 2, column=0, columnspan=2)

    def get_values(self) -> Optional[List[float]]:
        result = []
        for var in self._vars:
            try:
                result.append(float(var.get()))
            except ValueError:
                self._status_var.set("Invalid value")
                return None
        return result

    def _send(self) -> None:
        worker = self._get_worker()
        if worker is None:
            self._status_var.set("Not connected")
            return
        values = self.get_values()
        if values is None:
            return
        fut = worker.send_command(self._cmd, encode_corrections(values))
        self._send_btn.configure(state="disabled")
        self._status_var.set("Sending...")
        self.after(100, lambda: self._check_future(fut))

    def _check_future(self, fut) -> None:
        if not fut.done():
            self.after(100, lambda: self._check_future(fut))
            return
        self._send_btn.configure(state="normal")
        ok, err = fut.result()
        self._status_var.set("OK" if ok else f"Err: {err}")

    def load_values(self, values: List[float]) -> None:
        for var, v in zip(self._vars, values):
            var.set(f"{v:.4f}")


class _TpsCalPanel(ttk.LabelFrame):
    """Two buttons to capture the current TPS ADC value as 0% or 100%."""

    def __init__(self, parent, get_worker: Callable):
        super().__init__(parent, text="TPS Calibration", padding=6)
        self._get_worker = get_worker

        ttk.Label(self, text="Hold throttle at position, then click the button to calibrate.").grid(
            row=0, column=0, columnspan=2, pady=(0, 6)
        )

        self._closed_btn = ttk.Button(self, text="Set 0% (Closed)", command=lambda: self._send(CMD_TPS_CAL_CLOSED, self._closed_btn))
        self._closed_btn.grid(row=1, column=0, padx=6, pady=2)

        self._open_btn = ttk.Button(self, text="Set 100% (Open)", command=lambda: self._send(CMD_TPS_CAL_OPEN, self._open_btn))
        self._open_btn.grid(row=1, column=1, padx=6, pady=2)

        self._status_var = tk.StringVar()
        ttk.Label(self, textvariable=self._status_var, width=20).grid(row=2, column=0, columnspan=2)

    def _send(self, cmd: int, btn: ttk.Button) -> None:
        worker = self._get_worker()
        if worker is None:
            self._status_var.set("Not connected")
            return
        fut = worker.send_command(cmd, bytes())
        btn.configure(state="disabled")
        self._status_var.set("Sending...")
        self.after(100, lambda: self._check_future(fut, btn))

    def _check_future(self, fut, btn: ttk.Button) -> None:
        if not fut.done():
            self.after(100, lambda: self._check_future(fut, btn))
            return
        btn.configure(state="normal")
        ok, err = fut.result()
        self._status_var.set("OK" if ok else f"Err: {err}")


class CorrectionPanel(ttk.LabelFrame):
    def __init__(self, parent, ecu_state: ECUState, get_worker: Callable):
        super().__init__(parent, text="Temperature Corrections", padding=8)
        self._state = ecu_state

        self._iat_table = _CorrTable(
            self, "IAT Correction", ecu_state.iat_corr, IAT_CORR_TEMPS, CMD_WRITE_IAT_CORR, get_worker
        )
        self._iat_table.grid(row=0, column=0, padx=8, pady=4, sticky="n")

        self._et_table = _CorrTable(
            self, "ET Correction", ecu_state.et_corr, ET_CORR_TEMPS, CMD_WRITE_ET_CORR, get_worker
        )
        self._et_table.grid(row=0, column=1, padx=8, pady=4, sticky="n")

        ttk.Label(
            self,
            text="Values are multipliers applied to base pulse width (1.0 = no correction).",
            font=("TkDefaultFont", 8),
            foreground="gray",
        ).grid(row=1, column=0, columnspan=2, pady=(4, 0))

        _TpsCalPanel(self, get_worker).grid(row=2, column=0, columnspan=2, padx=8, pady=(8, 4), sticky="ew")

    def refresh_from_state(self) -> None:
        self._iat_table.load_values(self._state.iat_corr)
        self._et_table.load_values(self._state.et_corr)

    def flush_to_state(self) -> None:
        iat = self._iat_table.get_values()
        et = self._et_table.get_values()
        if iat is not None:
            self._state.iat_corr = iat
        if et is not None:
            self._state.et_corr = et
