"""Connection panel: port selector, baud rate, connect/disconnect."""

import tkinter as tk
from tkinter import ttk
from typing import Callable, Optional

from serial_worker import SerialWorker, list_ports


class ConnectionPanel(ttk.LabelFrame):
    def __init__(
        self,
        parent,
        ecu_state,
        on_connect: Callable[[SerialWorker], None],
        on_disconnect: Callable[[], None],
    ):
        super().__init__(parent, text="Connection", padding=6)
        self._state = ecu_state
        self._on_connect = on_connect
        self._on_disconnect = on_disconnect
        self._worker: Optional[SerialWorker] = None

        # Port
        ttk.Label(self, text="Port:").grid(row=0, column=0, sticky="e", padx=4)
        self._port_var = tk.StringVar()
        self._port_combo = ttk.Combobox(self, textvariable=self._port_var, width=18, state="readonly")
        self._port_combo.grid(row=0, column=1, padx=4)
        ttk.Button(self, text="Refresh", command=self._refresh_ports).grid(row=0, column=2, padx=4)

        # Baud
        ttk.Label(self, text="Baud:").grid(row=0, column=3, sticky="e", padx=4)
        self._baud_var = tk.StringVar(value="115200")
        ttk.Entry(self, textvariable=self._baud_var, width=8).grid(row=0, column=4, padx=4)

        # Connect button
        self._btn = ttk.Button(self, text="Connect", command=self._toggle)
        self._btn.grid(row=0, column=5, padx=8)

        # Status label
        self._status_var = tk.StringVar(value="Disconnected")
        self._status_label = tk.Label(self, textvariable=self._status_var, foreground="red")
        self._status_label.grid(row=0, column=6, padx=8)

        self._refresh_ports()

    def _refresh_ports(self) -> None:
        ports = list_ports()
        self._port_combo["values"] = ports
        if ports and not self._port_var.get():
            self._port_var.set(ports[0])

    def _toggle(self) -> None:
        if self._state.connected:
            self._disconnect()
        else:
            self._connect()

    def _connect(self) -> None:
        port = self._port_var.get()
        if not port:
            self._status_var.set("No port selected")
            return
        try:
            baud = int(self._baud_var.get())
        except ValueError:
            self._status_var.set("Invalid baud rate")
            return

        self._worker = SerialWorker(port, baud, self._state, self._handle_error)
        self._state.connected = True
        self._worker.start()

        self._btn.configure(text="Disconnect")
        self._status_var.set(f"Connected to {port}")
        self._status_label.configure(foreground="green")
        self._on_connect(self._worker)

    def _disconnect(self) -> None:
        if self._worker:
            self._worker.stop()
            self._worker = None
        self._state.connected = False
        self._btn.configure(text="Connect")
        self._status_var.set("Disconnected")
        self._status_label.configure(foreground="red")
        self._on_disconnect()

    def _handle_error(self, msg: str) -> None:
        # Called from serial worker thread — schedule GUI update on main thread
        self.after(0, lambda: self._show_error(msg))

    def _show_error(self, msg: str) -> None:
        self._state.connected = False
        self._worker = None
        self._btn.configure(text="Connect")
        self._status_var.set(f"Error: {msg}")
        self._status_label.configure(foreground="red")
        self._on_disconnect()
