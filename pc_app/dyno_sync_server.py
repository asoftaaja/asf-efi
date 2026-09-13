"""
Dyno sync server — lets an external application (the dyno controller) query the
injection map that is currently loaded in this tuner over a local TCP socket.

Protocol (version 1): newline-delimited JSON. The client sends one JSON object
per line, the server answers with one JSON object per line.

    {"cmd": "ping"}     -> {"ok": true, "app": "asf-efi", "proto": 1}
    {"cmd": "get_map"}  -> {"ok": true, "proto": 1, "timestamp": "...",
                            "rpm_axis": [...], "tps_axis_pct": [...],
                            "pw_unit_us": 100, "map": [[...], ...],
                            "ecu_connected": bool, "matches_ecu": bool|null,
                            "tunefile": "name.json"|null}
    anything else       -> {"ok": false, "error": "..."}

Threading: runs on its own daemon thread and only *reads* ECUState. It never
touches tkinter. The GUI polls ``status_text`` to display the listen state.
Python 3.6 compatible.
"""

import copy
import datetime
import json
import os
import selectors
import socket
import threading
from typing import Dict, Optional

import tune_io

PROTO_VERSION = 1
PW_UNIT_US = 100                 # one raw map unit = 100 µs of injector pulse
_MAX_LINE_BYTES = 64 * 1024      # drop clients that send this much without a newline
_SELECT_TIMEOUT_S = 0.5

_CONFIG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "dyno_sync_config.json")


def load_config() -> dict:
    """Load dyno_sync_config.json, returning defaults if missing or malformed."""
    defaults = {"host": "127.0.0.1", "port": 5555, "enabled": True}
    try:
        with open(_CONFIG_PATH, "r", encoding="utf-8") as fh:
            cfg = json.load(fh)
        if isinstance(cfg, dict):
            defaults.update(cfg)
    except (OSError, ValueError):
        pass
    return defaults


def _tps_axis_to_pct(tps_axis) -> list:
    """Normalise the TPS axis to integer percent.

    ECUState.tps_axis holds fractions 0..1 after a device read or tune-file load,
    but percent integers (TPS_BREAKPOINTS) before either has happened.
    """
    vals = [float(v) for v in tps_axis]
    if vals and max(vals) <= 1.0:
        vals = [v * 100.0 for v in vals]
    return [int(round(v)) for v in vals]


class DynoSyncServer(threading.Thread):
    """TCP/JSON-lines server exposing the current injection map."""

    def __init__(self, ecu_state, host: str = "127.0.0.1", port: int = 5555):
        super().__init__(daemon=True, name="DynoSyncServer")
        self._state = ecu_state
        self._host = host
        self._port = port
        self._stop_evt = threading.Event()
        self._listener = None   # type: Optional[socket.socket]
        self._clients = {}      # type: Dict[socket.socket, bytes]
        self.status_text = "Dyno sync: starting..."
        self.client_count = 0

    # ── Public API ───────────────────────────────────────────────────────────

    def stop(self) -> None:
        """Signal the thread to exit; the listener is closed inside run()."""
        self._stop_evt.set()

    # ── Thread body ──────────────────────────────────────────────────────────

    def run(self) -> None:
        try:
            self._listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self._listener.bind((self._host, self._port))
            self._listener.listen(4)
            self._listener.setblocking(False)
        except OSError as exc:
            self.status_text = "Dyno sync: cannot listen on {}:{} ({})".format(
                self._host, self._port, exc)
            self._listener = None
            return

        self.status_text = "Dyno sync: listening on {}:{}".format(self._host, self._port)

        sel = selectors.DefaultSelector()
        sel.register(self._listener, selectors.EVENT_READ)

        try:
            while not self._stop_evt.is_set():
                for key, _ in sel.select(timeout=_SELECT_TIMEOUT_S):
                    if key.fileobj is self._listener:
                        self._accept(sel)
                    else:
                        self._service_client(sel, key.fileobj)
        finally:
            for conn in list(self._clients):
                self._drop_client(sel, conn)
            sel.unregister(self._listener)
            sel.close()
            self._listener.close()
            self._listener = None
            self.status_text = "Dyno sync: stopped"

    # ── Connection handling ──────────────────────────────────────────────────

    def _accept(self, sel) -> None:
        try:
            conn, _addr = self._listener.accept()
        except OSError:
            return
        conn.setblocking(False)
        self._clients[conn] = b""
        sel.register(conn, selectors.EVENT_READ)
        self._update_client_count()

    def _drop_client(self, sel, conn) -> None:
        try:
            sel.unregister(conn)
        except (KeyError, ValueError):
            pass
        try:
            conn.close()
        except OSError:
            pass
        self._clients.pop(conn, None)
        self._update_client_count()

    def _update_client_count(self) -> None:
        self.client_count = len(self._clients)
        base = "Dyno sync: listening on {}:{}".format(self._host, self._port)
        if self.client_count:
            base += "  ({} client{} connected)".format(
                self.client_count, "" if self.client_count == 1 else "s")
        self.status_text = base

    def _service_client(self, sel, conn) -> None:
        try:
            data = conn.recv(4096)
        except OSError:
            self._drop_client(sel, conn)
            return
        if not data:
            self._drop_client(sel, conn)
            return

        buf = self._clients.get(conn, b"") + data
        if b"\n" not in buf and len(buf) > _MAX_LINE_BYTES:
            self._drop_client(sel, conn)
            return

        lines = buf.split(b"\n")
        self._clients[conn] = lines.pop()      # trailing partial line stays buffered

        for raw in lines:
            raw = raw.strip()
            if not raw:
                continue
            reply = self._handle_line(raw)
            try:
                conn.sendall((json.dumps(reply) + "\n").encode("utf-8"))
            except OSError:
                self._drop_client(sel, conn)
                return

    # ── Request handling ─────────────────────────────────────────────────────

    def _handle_line(self, raw: bytes) -> dict:
        try:
            req = json.loads(raw.decode("utf-8"))
        except (ValueError, UnicodeDecodeError) as exc:
            return {"ok": False, "error": "invalid json: {}".format(exc)}
        if not isinstance(req, dict):
            return {"ok": False, "error": "request must be a json object"}

        cmd = req.get("cmd")
        if cmd == "ping":
            return {"ok": True, "app": "asf-efi", "proto": PROTO_VERSION}
        if cmd == "get_map":
            return self._build_map_reply()
        return {"ok": False, "error": "unknown cmd"}

    def _build_map_reply(self) -> dict:
        """Snapshot the GUI-side injection map and axes.

        ECUState has no lock for these fields. List rebinding is atomic and a
        cell commit is a single int store, so a deepcopy taken here is at worst
        one cell behind an in-progress edit.
        """
        s = self._state
        inj_map = copy.deepcopy(s.inj_map)
        rpm_axis = [int(v) for v in s.rpm_axis]
        tps_axis = list(s.tps_axis)

        matches = None  # type: Optional[bool]
        if s.device_map_buf is not None:
            matches = (s.device_map_buf == s.inj_map and
                       s.device_rpm_axis_buf == s.rpm_axis and
                       s.device_tps_axis_buf == s.tps_axis)

        tunefile = None
        try:
            last = tune_io.get_last_tunefile()
            if last is not None:
                tunefile = last.name
        except OSError:
            pass

        return {
            "ok": True,
            "proto": PROTO_VERSION,
            "timestamp": datetime.datetime.now().replace(microsecond=0).isoformat(),
            "rpm_axis": rpm_axis,
            "tps_axis_pct": _tps_axis_to_pct(tps_axis),
            "pw_unit_us": PW_UNIT_US,
            "map": [[int(v) for v in row] for row in inj_map],
            "ecu_connected": bool(s.connected),
            "matches_ecu": matches,
            "tunefile": tunefile,
        }
