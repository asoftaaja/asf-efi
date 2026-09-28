# ASF EFI — Dyno Sync Server

## Overview

The PC tuner runs a small TCP server so that the dyno controller application
(`qt/dyno-controller`) can fetch the injection map that is currently loaded in the
tuner. The dyno app asks for the map every time a dyno run is started and stores it
next to the run data, so every power curve can later be viewed together with the
fuelling that produced it.

No ECU connection is needed for the server to work: it reports the map shown in the
map editor (`ECUState.inj_map`), which is what gets written to the ECU on "Write all
to device".

---

## Configuration

`pc_app/dyno_sync_config.json` (next to `main.py`):

```json
{
  "host": "127.0.0.1",
  "port": 5555,
  "enabled": true
}
```

- `host` — interface to listen on. `127.0.0.1` when both apps run on the same PC;
  `0.0.0.0` to accept the dyno app from another machine on the LAN.
- `port` — TCP port. Must match the port entered in the dyno app under
  *ECU sync → Setup…*.
- `enabled` — `false` disables the server entirely.

Missing or malformed file → defaults above. The file is never written by the app.

A status line at the bottom of the main window shows the listen state, e.g.
`Dyno sync: listening on 127.0.0.1:5555 (1 client connected)` or the bind error.

---

## Protocol (version 1)

Newline-delimited JSON over TCP. The client sends one JSON object per line, the
server answers with exactly one JSON object per line. The connection may stay open;
the dyno app keeps it open and uses it as its "connected" indicator.

### `ping`

```
-> {"cmd": "ping"}
<- {"ok": true, "app": "asf-efi", "proto": 1}
```

### `get_map`

```
-> {"cmd": "get_map"}
<- {
     "ok": true,
     "proto": 1,
     "timestamp": "2026-09-12T14:03:22",
     "rpm_axis": [1000, 4000, 7000, 9000, 11000, 12500, 13500, 14500, 15500, 17000],
     "tps_axis_pct": [0, 30, 60, 100],
     "pw_unit_us": 100,
     "map": [[12, 18, 24, 30], ...],
     "ecu_connected": true,
     "matches_ecu": true,
     "tunefile": "test_tune2.json"
   }
```

| Field | Meaning |
|---|---|
| `rpm_axis` | RPM breakpoints, one per map row (`RPM_BINS` = 10) |
| `tps_axis_pct` | Throttle breakpoints in **integer percent**, one per map column (`TPS_BINS` = 4) |
| `pw_unit_us` | Microseconds per raw map unit (100 → 1 unit = 0.1 ms) |
| `map` | `RPM_BINS × TPS_BINS` raw uint8 cells, `map[rpm_idx][tps_idx]` |
| `ecu_connected` | Whether the tuner currently has a serial connection to the ECU |
| `matches_ecu` | `true` if map and axes equal the last map read from / written to the ECU, `false` if they differ, `null` if no device map has been read yet |
| `tunefile` | Basename of the last loaded/saved tune file, or `null` |

### Errors

```
<- {"ok": false, "error": "unknown cmd"}
<- {"ok": false, "error": "invalid json: ..."}
```

A client that sends more than 64 KB without a newline is disconnected.

### Manual test

```bash
printf '{"cmd":"get_map"}\n' | nc 127.0.0.1 5555
```

---

## Implementation

| File | Role |
|---|---|
| `pc_app/dyno_sync_server.py` | `DynoSyncServer` thread, config loader, request handling |
| `pc_app/dyno_sync_config.json` | Host / port / enabled |
| `pc_app/gui/main_window.py` | Starts the server after the UI is built; polls `status_text` into the status label |

### Thread model

`DynoSyncServer` is a daemon thread (like `DataLoggerWorker`). One `selectors`
loop serves the listening socket and any number of clients; there are no per-client
threads. The thread only **reads** `ECUState` and never touches tkinter. The GUI
reads `server.status_text` from an `after()` poll.

`ECUState.inj_map`, `rpm_axis` and `tps_axis` are not lock-protected (only the sensor
snapshot is). The server takes a `copy.deepcopy` snapshot per request. List
rebinding (`update_inj_map`, tune-file load) is atomic, and a map-editor commit is a
single int store, so the snapshot is at worst one cell behind an in-progress edit.

### TPS axis units

`ECUState.tps_axis` always holds fractions 0..1, including the `TPS_BREAKPOINTS`
defaults used before a device read or tune-file load. The server converts with
`tps_pct_raw()` from `protocol.py` — the same narrowing the wire applies — so clients
receive exactly the integer percent the ECU stores.

---

## Dyno side

The dyno controller saves the reply as `injmap_run_<N>.csv` in the run's session
folder, next to `run<N>.csv`, with pulse widths converted to milliseconds:

```
format:injmap_v1
timestamp;2026-09-12T14:03:22
tunefile;test_tune2.json
ecu_connected;1
matches_ecu;1
pw_unit;ms
rpm\tps_pct;0;30;60;100
1000;1.20;1.80;2.40;3.00
4000;...
```

It then plots the pulse-width-vs-RPM curve for a selectable throttle position under
the power graph of each run.
