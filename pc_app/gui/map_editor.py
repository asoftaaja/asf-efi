"""
12×5 injection map editor with live cursor, plus axis breakpoint editor.

Layout: header row (TPS%) + 12 data rows (RPM bins), each with 5 Entry cells.
The active cell nearest to the current RPM/TPS is highlighted in gold.
Below the map: an axis editor with editable RPM and TPS breakpoints.
"""

import tkinter as tk
from tkinter import ttk
from typing import Callable, List, Optional, Tuple

from protocol import (
    RPM_BINS, TPS_BINS,
    RPM_BREAKPOINTS, TPS_BREAKPOINTS,
    encode_map, encode_axis,
    nearest_rpm_bin, nearest_tps_bin,
    CMD_WRITE_MAP, CMD_READ_MAP, CMD_WRITE_AXIS, CMD_READ_AXIS,
)
from data_model import ECUState

COLOR_CURSOR  = "#FF0000"   # red
COLOR_NORMAL  = "white"
COLOR_EDIT    = "#E8F4FF"   # light blue when editing


class MapEditor(ttk.LabelFrame):
    def __init__(self, parent, ecu_state: ECUState, get_worker: Callable):
        super().__init__(parent, text="Injection Map  (pulse width µs)", padding=6)
        self._state = ecu_state
        self._get_worker = get_worker
        self._cursor: Optional[Tuple[int, int]] = None

        # 2D list of (StringVar, Entry, Frame) per [row][col]
        self._vars:        List[List[tk.StringVar]] = []
        self._cells:       List[List[ttk.Entry]]    = []
        self._cell_frames: List[List[tk.Frame]]     = []

        # Map axis label widgets (updated when axis changes)
        self._rpm_labels: List[ttk.Label] = []
        self._tps_labels: List[ttk.Label] = []

        # Axis editor StringVars
        self._rpm_axis_vars: List[tk.StringVar] = []
        self._tps_axis_vars: List[tk.StringVar] = []

        self._build_grid()
        self._build_buttons()
        self._build_axis_editor()

    # ── Build ─────────────────────────────────────────────────────────────────

    def _build_grid(self) -> None:
        # Column headers: TPS %
        ttk.Label(self, text="RPM \\ TPS", width=8, anchor="center",
                  font=("TkDefaultFont", 9, "bold")).grid(row=0, column=0, padx=2, pady=2)
        for c, tps in enumerate(self._state.tps_axis):
            lbl = ttk.Label(self, text=f"{tps*100:.0f}%", width=7, anchor="center",
                            font=("TkDefaultFont", 9, "bold"))
            lbl.grid(row=0, column=c + 1, padx=2)
            self._tps_labels.append(lbl)

        # Data rows
        for r in range(RPM_BINS):
            rpm_val = self._state.rpm_axis[r]
            lbl = ttk.Label(self, text=str(rpm_val), width=8, anchor="e",
                            font=("TkDefaultFont", 9, "bold"))
            lbl.grid(row=r + 1, column=0, padx=4, pady=1)
            self._rpm_labels.append(lbl)

            row_vars:   List[tk.StringVar] = []
            row_cells:  List[ttk.Entry]    = []
            row_frames: List[tk.Frame]     = []

            for c in range(TPS_BINS):
                var = tk.StringVar(value=str(self._state.inj_map[r][c]))

                # Thin frame acts as a coloured border around the native entry
                frame = tk.Frame(self)
                frame.grid(row=r + 1, column=c + 1, padx=2, pady=1)
                _normal_bg = frame.cget("background")   # capture system colour for restore

                entry = ttk.Entry(frame, textvariable=var, width=7, justify="center")
                entry.pack(fill="both", expand=True, padx=2, pady=2)

                # Commit on Return or focus-out
                entry.bind("<Return>",   lambda e, row=r, col=c: self._commit(row, col))
                entry.bind("<FocusOut>", lambda e, row=r, col=c: self._commit(row, col))
                entry.bind("<FocusIn>",  lambda e, f=frame: f.configure(background=COLOR_EDIT))
                entry.bind("<Button-2>", lambda e, row=r, col=c: self._show_context_menu(e, row, col))
                entry.bind("<Button-3>", lambda e, row=r, col=c: self._show_context_menu(e, row, col))

                frame._normal_bg = _normal_bg   # stash for restore

                row_vars.append(var)
                row_cells.append(entry)
                row_frames.append(frame)

            self._vars.append(row_vars)
            self._cells.append(row_cells)
            self._cell_frames.append(row_frames)

    def _build_buttons(self) -> None:
        btn_frame = ttk.Frame(self)
        btn_frame.grid(row=RPM_BINS + 1, column=0, columnspan=TPS_BINS + 1,
                       pady=(8, 0), sticky="w")

        self._read_btn = ttk.Button(btn_frame, text="Read Map from ECU", command=self._read_map)
        self._read_btn.grid(row=0, column=0, padx=4)

        self._send_btn = ttk.Button(btn_frame, text="Send Map to ECU", command=self._send_map)
        self._send_btn.grid(row=0, column=1, padx=4)

        ttk.Button(btn_frame, text="Fill All Zeros", command=self._fill_zeros).grid(row=0, column=2, padx=4)

        self._status_var = tk.StringVar()
        ttk.Label(btn_frame, textvariable=self._status_var, width=20).grid(row=0, column=3, padx=8)

    def _build_axis_editor(self) -> None:
        """Build the axis breakpoint editor below the map grid and its buttons."""
        frame = ttk.LabelFrame(self, text="Axis Breakpoints", padding=4)
        frame.grid(row=RPM_BINS + 2, column=0, columnspan=TPS_BINS + 1,
                   pady=(10, 0), sticky="ew")

        # RPM row
        ttk.Label(frame, text="RPM:", font=("TkDefaultFont", 9, "bold")).grid(
            row=0, column=0, padx=(0, 4), sticky="e")
        for i in range(RPM_BINS):
            var = tk.StringVar(value=str(self._state.rpm_axis[i]))
            entry = ttk.Entry(frame, textvariable=var, width=6, justify="center")
            entry.grid(row=0, column=i + 1, padx=2, pady=2)
            self._rpm_axis_vars.append(var)

        # TPS row
        ttk.Label(frame, text="TPS %:", font=("TkDefaultFont", 9, "bold")).grid(
            row=1, column=0, padx=(0, 4), sticky="e")
        for i in range(TPS_BINS):
            var = tk.StringVar(value=f"{self._state.tps_axis[i]*100:.1f}")
            entry = ttk.Entry(frame, textvariable=var, width=6, justify="center")
            entry.grid(row=1, column=i + 1, padx=2, pady=2)
            self._tps_axis_vars.append(var)

        # Buttons
        axis_btn_frame = ttk.Frame(frame)
        axis_btn_frame.grid(row=2, column=0, columnspan=RPM_BINS + 1, pady=(4, 0), sticky="w")

        self._read_axis_btn = ttk.Button(axis_btn_frame, text="Read Axis from ECU",
                                         command=self._read_axis)
        self._read_axis_btn.grid(row=0, column=0, padx=4)

        self._send_axis_btn = ttk.Button(axis_btn_frame, text="Send Axis to ECU",
                                         command=self._send_axis)
        self._send_axis_btn.grid(row=0, column=1, padx=4)

        self._axis_status_var = tk.StringVar()
        ttk.Label(axis_btn_frame, textvariable=self._axis_status_var, width=20).grid(
            row=0, column=2, padx=8)

    # ── Cell editing ──────────────────────────────────────────────────────────

    def _commit(self, row: int, col: int) -> None:
        """Validate and commit a cell value back to ECU state."""
        var = self._vars[row][col]
        raw = var.get().strip()
        try:
            val = int(raw)
            if not (0 <= val <= 65535):
                raise ValueError
            self._state.inj_map[row][col] = val
            var.set(str(val))
            # Restore frame border (cursor takes priority)
            f = self._cell_frames[row][col]
            f.configure(background=COLOR_CURSOR if self._cursor == (row, col) else f._normal_bg)
        except ValueError:
            # Restore last good value
            var.set(str(self._state.inj_map[row][col]))
            f = self._cell_frames[row][col]
            f.configure(background=COLOR_CURSOR if self._cursor == (row, col) else f._normal_bg)

    def _fill_zeros(self) -> None:
        for r in range(RPM_BINS):
            for c in range(TPS_BINS):
                self._state.inj_map[r][c] = 0
                self._vars[r][c].set("0")
        self._status_var.set("Cleared")

    # ── Context menu ──────────────────────────────────────────────────────────

    def _show_context_menu(self, event, row: int, col: int) -> str:
        # Commit using the state value directly — do NOT read the Entry widget,
        # because Button-2 on macOS pastes the selection into the Entry before
        # this handler runs, which would corrupt the value.
        val = self._state.inj_map[row][col]
        # Restore the Entry to the known-good state value in case paste happened
        self._vars[row][col].set(str(val))

        menu = tk.Menu(self, tearoff=0)
        menu.add_command(
            label=f"Fill row {self._state.rpm_axis[row]} RPM with {val}",
            command=lambda: self._fill_row(row, val),
        )
        menu.add_command(
            label=f"Fill column {self._state.tps_axis[col]*100:.0f}% TPS with {val}",
            command=lambda: self._fill_col(col, val),
        )
        try:
            menu.tk_popup(event.x_root, event.y_root)
        finally:
            menu.grab_release()
        return "break"  # suppress default paste-on-middle-click behavior

    def _fill_row(self, row: int, val: int) -> None:
        for c in range(TPS_BINS):
            self._state.inj_map[row][c] = val
            self._vars[row][c].set(str(val))

    def _fill_col(self, col: int, val: int) -> None:
        for r in range(RPM_BINS):
            self._state.inj_map[r][col] = val
            self._vars[r][col].set(str(val))

    # ── Live cursor ───────────────────────────────────────────────────────────

    def update_cursor(self, rpm: int, tps: float) -> None:
        new_row = nearest_rpm_bin(rpm, self._state.rpm_axis)
        new_col = nearest_tps_bin(tps, self._state.tps_axis)
        new_pos = (new_row, new_col)

        if new_pos == self._cursor:
            return

        # Restore previous cell
        if self._cursor is not None:
            pr, pc = self._cursor
            f = self._cell_frames[pr][pc]
            f.configure(background=f._normal_bg)
            self._cells[pr][pc].pack_configure(padx=2, pady=2)

        # Highlight new cell
        self._cell_frames[new_row][new_col].configure(background=COLOR_CURSOR)
        self._cells[new_row][new_col].pack_configure(padx=4, pady=4)
        self._cursor = new_pos

    # ── Read map from ECU ─────────────────────────────────────────────────────

    def _read_map(self) -> None:
        worker = self._get_worker()
        if worker is None:
            self._status_var.set("Not connected")
            return
        fut = worker.read_map()
        self._read_btn.configure(state="disabled")
        self._status_var.set("Reading...")
        self.after(100, lambda: self._check_read_future(fut))

    def _check_read_future(self, fut) -> None:
        if not fut.done():
            self.after(100, lambda: self._check_read_future(fut))
            return
        self._read_btn.configure(state="normal")
        ok, err = fut.result()
        if ok:
            self.refresh_from_state()
            self._status_var.set("Map loaded")
        else:
            self._status_var.set(f"Failed: {err}")

    # ── Send map to ECU ───────────────────────────────────────────────────────

    def _send_map(self) -> None:
        worker = self._get_worker()
        if worker is None:
            self._status_var.set("Not connected")
            return
        # Commit any in-progress edits
        for r in range(RPM_BINS):
            for c in range(TPS_BINS):
                self._commit(r, c)

        payload = encode_map(self._state.inj_map)
        fut = worker.send_command(CMD_WRITE_MAP, payload)
        self._send_btn.configure(state="disabled")
        self._status_var.set("Sending...")
        self.after(100, lambda: self._check_future(fut, self._send_btn, self._status_var))

    def _check_future(self, fut, btn, status_var) -> None:
        if not fut.done():
            self.after(100, lambda: self._check_future(fut, btn, status_var))
            return
        btn.configure(state="normal")
        ok, err = fut.result()
        status_var.set("Sent OK" if ok else f"Failed: {err}")

    # ── Read axis from ECU ────────────────────────────────────────────────────

    def _read_axis(self) -> None:
        worker = self._get_worker()
        if worker is None:
            self._axis_status_var.set("Not connected")
            return
        fut = worker.read_axis()
        self._read_axis_btn.configure(state="disabled")
        self._axis_status_var.set("Reading...")
        self.after(100, lambda: self._check_read_axis_future(fut))

    def _check_read_axis_future(self, fut) -> None:
        if not fut.done():
            self.after(100, lambda: self._check_read_axis_future(fut))
            return
        self._read_axis_btn.configure(state="normal")
        ok, err = fut.result()
        if ok:
            self._refresh_axis_from_state()
            self._axis_status_var.set("Axis loaded")
        else:
            self._axis_status_var.set(f"Failed: {err}")

    # ── Send axis to ECU ──────────────────────────────────────────────────────

    def _send_axis(self) -> None:
        worker = self._get_worker()
        if worker is None:
            self._axis_status_var.set("Not connected")
            return

        # Parse and validate RPM entries (must be ascending uint16)
        rpm_vals: List[int] = []
        for i, var in enumerate(self._rpm_axis_vars):
            try:
                v = int(var.get().strip())
                if not (0 <= v <= 65535):
                    raise ValueError
                rpm_vals.append(v)
            except ValueError:
                self._axis_status_var.set(f"Bad RPM[{i}]")
                return
        if rpm_vals != sorted(rpm_vals):
            self._axis_status_var.set("RPM must be ascending")
            return

        # Parse and validate TPS entries (0–100 %, must be ascending)
        tps_vals: List[float] = []
        for i, var in enumerate(self._tps_axis_vars):
            try:
                pct = float(var.get().strip())
                if not (0.0 <= pct <= 100.0):
                    raise ValueError
                tps_vals.append(pct / 100.0)
            except ValueError:
                self._axis_status_var.set(f"Bad TPS[{i}]")
                return
        if tps_vals != sorted(tps_vals):
            self._axis_status_var.set("TPS must be ascending")
            return

        # Commit to state and send
        self._state.rpm_axis = rpm_vals
        self._state.tps_axis = tps_vals
        self._update_map_labels()

        payload = encode_axis(rpm_vals, tps_vals)
        fut = worker.send_command(CMD_WRITE_AXIS, payload)
        self._send_axis_btn.configure(state="disabled")
        self._axis_status_var.set("Sending...")
        self.after(100, lambda: self._check_future(fut, self._send_axis_btn, self._axis_status_var))

    # ── Sync display from state ───────────────────────────────────────────────

    def refresh_from_state(self) -> None:
        for r in range(RPM_BINS):
            for c in range(TPS_BINS):
                self._vars[r][c].set(str(self._state.inj_map[r][c]))

    def _refresh_axis_from_state(self) -> None:
        """Update axis Entry widgets and map grid labels from ECUState."""
        for i, var in enumerate(self._rpm_axis_vars):
            var.set(str(self._state.rpm_axis[i]))
        for i, var in enumerate(self._tps_axis_vars):
            var.set(f"{self._state.tps_axis[i]*100:.1f}")
        self._update_map_labels()

    def _update_map_labels(self) -> None:
        """Redraw the row/column labels in the map grid to reflect current axis values."""
        for r, lbl in enumerate(self._rpm_labels):
            lbl.configure(text=str(self._state.rpm_axis[r]))
        for c, lbl in enumerate(self._tps_labels):
            lbl.configure(text=f"{self._state.tps_axis[c]*100:.0f}%")
