"""
ASF EFI Log Viewer — standalone app to plot CSV data logs.

Usage:
    python log_viewer.py [path/to/logfile.csv]
"""

import bisect
import csv
import os
import sys
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

import matplotlib
matplotlib.use("TkAgg")
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk
from matplotlib.figure import Figure
from datetime import datetime

_HEADER_ROWS = 5
_INITIAL_DIR = os.path.join(os.path.dirname(__file__), "logs")

# (subplot_title, [(col, label, color)], ylabel, step_plot)
_SUBPLOT_DEFS = [
    ("RPM",               [("rpm",           "RPM",        "#1f77b4")],            "RPM",     False),
    ("Throttle Position", [("tps_pct",       "TPS",        "#ff7f0e")],            "TPS (%)", False),
    ("Fuel Pressure",     [("fps_bar",       "FPS",        "#2ca02c")],            "bar",     False),
    ("Temperature",       [("iat_degc",      "IAT",        "#d62728"),
                           ("et_degc",       "ET",         "#9467bd")],            "°C",      False),
    ("Battery Voltage",   [("bat_v",         "VBAT",       "#8c564b")],            "V",       False),
    ("Duty Cycles",       [("inj_duty_pct",  "Injector",   "#e377c2"),
                           ("pump_duty_pct", "Pump",       "#7f7f7f")],            "%",       False),
    ("Active Flags",      [("pump_active",   "Pump",       "#bcbd22"),
                           ("accel_active",  "Accel pump", "#17becf")],            "Active",  True),
]

# Fields shown in the value bar: (column, display label, format string)
_VALUE_BAR_FIELDS = [
    ("_elapsed",      "t",       "{:.2f} s"),
    ("rpm",           "RPM",     "{:.0f}"),
    ("tps_pct",       "TPS",     "{:.1f} %"),
    ("fps_bar",       "FPS",     "{:.3f} bar"),
    ("iat_degc",      "IAT",     "{:.1f} °C"),
    ("et_degc",       "ET",      "{:.1f} °C"),
    ("pump_active",   "PUMP",    lambda v: "ON" if v else "OFF"),
    ("bat_v",         "VBAT",    "{:.2f} V"),
    ("pump_duty_pct", "P.DUTY",  "{:.1f} %"),
    ("inj_duty_pct",  "I.DUTY",  "{:.1f} %"),
    ("accel_active",  "ACCEL",   lambda v: "ON" if v else "OFF"),
]


def _load_log(path: str) -> dict:
    """
    Read a log file, skip reserved header rows, and return column arrays.

    Returns a dict mapping column name → list of values (numeric or str).
    """
    with open(path, "r", encoding="utf-8") as fh:
        for _ in range(_HEADER_ROWS):
            fh.readline()
        reader = csv.DictReader(fh)
        rows = list(reader)

    if not rows:
        return {}

    data = {col: [] for col in rows[0]}
    for row in rows:
        for col, val in row.items():
            data[col].append(val)

    timestamps = [datetime.fromisoformat(t) for t in data["timestamp"]]
    t0 = timestamps[0]
    data["_elapsed"] = [(t - t0).total_seconds() for t in timestamps]

    numeric = [
        "rpm", "tps_pct", "fps_bar", "iat_degc", "et_degc",
        "pump_active", "bat_v", "pump_duty_pct", "inj_duty_pct", "accel_active",
    ]
    for col in numeric:
        if col in data:
            data[col] = [float(v) for v in data[col]]

    return data


def _nearest_index(elapsed: list, x: float) -> int:
    """Return the index of the sample whose elapsed time is closest to x."""
    pos = bisect.bisect_left(elapsed, x)
    if pos == 0:
        return 0
    if pos >= len(elapsed):
        return len(elapsed) - 1
    before, after = elapsed[pos - 1], elapsed[pos]
    return pos - 1 if (x - before) <= (after - x) else pos


class LogViewer(tk.Tk):
    """Main window for the ASF EFI log viewer."""

    def __init__(self, initial_path: str = None):
        super().__init__()
        self.title("ASF EFI Log Viewer")
        self.geometry("1100x820")
        self.resizable(True, True)

        self._data: dict | None = None
        self._axes: list = []
        self._cursor_lines: list = []
        self._motion_cid = None
        self._leave_cid = None

        self._build_ui()

        if initial_path:
            self._open_file(initial_path)

    # ── UI construction ───────────────────────────────────────────────────────

    def _build_ui(self) -> None:
        # File picker bar
        toolbar_frame = ttk.Frame(self)
        toolbar_frame.pack(fill="x", padx=6, pady=4)
        ttk.Button(toolbar_frame, text="Open Log File…", command=self._pick_file).pack(side="left")
        self._path_var = tk.StringVar(value="No file loaded")
        ttk.Label(toolbar_frame, textvariable=self._path_var, anchor="w").pack(
            side="left", padx=(8, 0))

        ttk.Separator(self, orient="horizontal").pack(fill="x")

        # Value bar — packed at the bottom so it stays below the canvas
        self._value_bar = tk.Frame(self, background="#1e1e1e", pady=4)
        self._value_vars: list[tk.StringVar] = []
        for i, (col, label, fmt) in enumerate(_VALUE_BAR_FIELDS):
            if i > 0:
                tk.Label(self._value_bar, text="│", background="#1e1e1e",
                         foreground="#555555", font=("Courier", 18)).pack(side="left", padx=2)
            lbl = tk.Label(self._value_bar, text=f"{label}:", background="#1e1e1e",
                           foreground="#888888", font=("Courier", 13))
            lbl.pack(side="left", padx=(6, 1))
            var = tk.StringVar(value="---")
            self._value_vars.append(var)
            tk.Label(self._value_bar, textvariable=var, background="#1e1e1e",
                     foreground="#e0e0e0", font=("Courier", 18, "bold")).pack(side="left", padx=(0, 4))

        # Placeholder shown before a file is opened
        self._placeholder = ttk.Label(
            self,
            text="Open a log file to view sensor data.\n\nExpected file name format: asf_efi_datalog_YYYYMMDD_HHMMSS.csv",
            anchor="center",
            justify="center",
            font=("TkDefaultFont", 12),
        )
        self._placeholder.pack(expand=True)

        # Canvas frame (hidden until a file is loaded)
        self._canvas_frame = ttk.Frame(self)
        self._fig = Figure(figsize=(11, 8))
        self._canvas = FigureCanvasTkAgg(self._fig, master=self._canvas_frame)
        self._nav = NavigationToolbar2Tk(self._canvas, self._canvas_frame)
        self._nav.update()
        self._canvas.get_tk_widget().pack(fill="both", expand=True)

    # ── File handling ─────────────────────────────────────────────────────────

    def _pick_file(self) -> None:
        path = filedialog.askopenfilename(
            title="Open ASF EFI Log",
            initialdir=_INITIAL_DIR if os.path.isdir(_INITIAL_DIR) else os.path.expanduser("~"),
            filetypes=[("CSV log files", "asf_efi_datalog_*.csv"), ("All CSV files", "*.csv")],
        )
        if path:
            self._open_file(path)

    def _open_file(self, path: str) -> None:
        try:
            data = _load_log(path)
        except Exception as exc:
            messagebox.showerror("Load error", str(exc), parent=self)
            return

        if not data:
            messagebox.showwarning("Empty file", "The log file contains no data rows.", parent=self)
            return

        self._data = data
        self.title(f"ASF EFI Log Viewer — {os.path.basename(path)}")
        self._path_var.set(path)

        self._placeholder.pack_forget()
        self._value_bar.pack(fill="x", side="bottom")
        self._canvas_frame.pack(fill="both", expand=True)

        self._build_subplots()
        self._setup_cursor()
        self._reset_value_bar()
        self._canvas.draw()

    # ── Plot construction ─────────────────────────────────────────────────────

    def _build_subplots(self) -> None:
        """Rebuild all subplots from current data."""
        self._fig.clear()
        self._axes = []
        t = self._data["_elapsed"]
        n = len(_SUBPLOT_DEFS)

        for i, (_, series, ylabel, step) in enumerate(_SUBPLOT_DEFS):
            if i == 0:
                ax = self._fig.add_subplot(n, 1, 1)
                first_ax = ax
            else:
                ax = self._fig.add_subplot(n, 1, i + 1, sharex=first_ax)
            self._axes.append(ax)

            for col, label, color in series:
                if col not in self._data:
                    continue
                if step:
                    ax.step(t, self._data[col], where="post", label=label,
                            color=color, linewidth=1.2)
                else:
                    ax.plot(t, self._data[col], label=label, color=color, linewidth=1.2)

            ax.set_ylabel(ylabel, fontsize=8)
            ax.tick_params(labelsize=7)
            ax.grid(True, alpha=0.3)
            if step:
                ax.set_ylim(-0.1, 1.4)
                ax.set_yticks([0, 1])
            if len(series) > 1:
                ax.legend(fontsize=7, loc="upper right")
            if i < n - 1:
                ax.tick_params(labelbottom=False)

        self._axes[-1].set_xlabel("Elapsed time (s)", fontsize=8)
        self._fig.tight_layout(pad=0.4, h_pad=0.3)

    # ── Cursor ────────────────────────────────────────────────────────────────

    def _setup_cursor(self) -> None:
        """Add a hidden vertical cursor line to every subplot and connect mouse events."""
        # Remove old event connections
        if self._motion_cid is not None:
            self._canvas.mpl_disconnect(self._motion_cid)
        if self._leave_cid is not None:
            self._canvas.mpl_disconnect(self._leave_cid)

        self._cursor_lines = []
        for ax in self._axes:
            line = ax.axvline(x=0, color="#ff4444", linewidth=0.8,
                              linestyle="--", alpha=0.85, visible=False)
            self._cursor_lines.append(line)

        self._motion_cid = self._canvas.mpl_connect(
            "motion_notify_event", self._on_mouse_move)
        self._leave_cid = self._canvas.mpl_connect(
            "figure_leave_event", self._on_figure_leave)

    def _on_mouse_move(self, event) -> None:
        if event.inaxes is None or self._data is None:
            self._set_cursor_visible(False)
            return

        x = event.xdata
        for line in self._cursor_lines:
            line.set_xdata([x, x])
            line.set_visible(True)

        idx = _nearest_index(self._data["_elapsed"], x)
        self._update_value_bar(idx)
        self._canvas.draw_idle()

    def _on_figure_leave(self, event) -> None:
        self._set_cursor_visible(False)
        self._reset_value_bar()
        self._canvas.draw_idle()

    def _set_cursor_visible(self, visible: bool) -> None:
        for line in self._cursor_lines:
            line.set_visible(visible)

    # ── Value bar ─────────────────────────────────────────────────────────────

    def _update_value_bar(self, idx: int) -> None:
        """Fill value bar labels with data from sample index idx."""
        for var, (col, _, fmt) in zip(self._value_vars, _VALUE_BAR_FIELDS):
            if col not in self._data:
                var.set("---")
                continue
            val = self._data[col][idx]
            if callable(fmt):
                var.set(fmt(val))
            else:
                var.set(fmt.format(val))

    def _reset_value_bar(self) -> None:
        for var in self._value_vars:
            var.set("---")


def main() -> None:
    initial = sys.argv[1] if len(sys.argv) > 1 else None
    app = LogViewer(initial_path=initial)
    app.mainloop()


if __name__ == "__main__":
    main()
