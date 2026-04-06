"""Tune file bar — filename display, Save and Load buttons."""

import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from pathlib import Path
from typing import Callable, Optional

import tune_io


class TuneFilePanel(ttk.Frame):
    def __init__(self, parent, ecu_state, on_loaded: Callable[[], None],
                 on_save: Optional[Callable[[], None]] = None):
        super().__init__(parent)
        self._state = ecu_state
        self._on_loaded = on_loaded
        self._on_save = on_save
        self._current_path: Optional[Path] = None

        # ── Layout ────────────────────────────────────────────────────────────
        ttk.Label(self, text="Tune file:").pack(side="left", padx=(0, 4))

        self._name_var = tk.StringVar(value="(none)")
        name_label = ttk.Label(
            self,
            textvariable=self._name_var,
            font=("TkDefaultFont", 10, "bold"),
            foreground="#1a5276",
            width=30,
            anchor="w",
        )
        name_label.pack(side="left", padx=(0, 12))

        ttk.Button(self, text="Load",    command=self._load).pack(side="left", padx=2)
        ttk.Button(self, text="Save",    command=self._save).pack(side="left", padx=2)
        ttk.Button(self, text="Save As", command=self._save_as).pack(side="left", padx=2)

    # ── Public ────────────────────────────────────────────────────────────────

    def set_path(self, path: Optional[Path]) -> None:
        self._current_path = path
        self._name_var.set(path.name if path else "(none)")

    # ── Callbacks ─────────────────────────────────────────────────────────────

    def _load(self) -> None:
        tune_io._ensure_dir()
        initial = str(self._current_path.parent) if self._current_path else str(tune_io.TUNEFILES_DIR)
        path = filedialog.askopenfilename(
            title="Load tune file",
            initialdir=initial,
            filetypes=[("Tune files", "*.json"), ("All files", "*.*")],
        )
        if not path:
            return
        try:
            tune_io.load_tunefile(path, self._state)
        except Exception as exc:
            messagebox.showerror("Load failed", str(exc))
            return
        self.set_path(Path(path))
        self._on_loaded()

    def _save(self) -> None:
        if self._current_path:
            self._do_save(self._current_path)
        else:
            self._save_as()

    def _save_as(self) -> None:
        tune_io._ensure_dir()
        path = filedialog.asksaveasfilename(
            title="Save tune file",
            initialdir=str(tune_io.TUNEFILES_DIR),
            defaultextension=".json",
            filetypes=[("Tune files", "*.json"), ("All files", "*.*")],
        )
        if not path:
            return
        self._do_save(Path(path))

    def _do_save(self, path: Path) -> None:
        if self._on_save:
            self._on_save()
        try:
            tune_io.save_tunefile(path, self._state)
        except Exception as exc:
            messagebox.showerror("Save failed", str(exc))
            return
        self.set_path(path)
