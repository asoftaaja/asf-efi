# PC App — Injection Map Editor

## Overview

The injection map editor (`gui/map_editor.py`) presents the 10×4 (RPM × TPS) fuel map as an editable grid of `ttk.Entry` widgets. A live red cursor tracks the active cell nearest to the current engine operating point. A sub-panel below the map allows editing and uploading the axis breakpoints.

---

## Map Format

Values are `uint8` integers where **1 unit = 100 µs** of injector pulse width. The range is 0–255 (= 0–25 500 µs). This matches the firmware's `inj_map[RPM_BINS][TPS_BINS]` array.

Default axis breakpoints (compile-time, overridable via `CMD_WRITE_AXIS`):

| Axis | Bins | Default breakpoints |
|---|---|---|
| RPM | 10 | 1000, 4000, 7000, 9000, 11000, 12500, 13500, 14500, 15500, 17000 RPM |
| TPS | 4  | 0, 30, 60, 100 % |

---

## Grid Layout

```
RPM \ TPS  |  0%   30%   60%  100%
-----------+----------------------
    1000   | [ ]   [ ]   [ ]  [ ]
    4000   | [ ]   [■]   [ ]  [ ]   ← red cursor
    ...
```

The corner label reads "RPM \ TPS". Column headers show TPS percentages; row headers show RPM values. Both update live when axis breakpoints are changed.

Each cell is a `ttk.Entry` inside a thin `tk.Frame`. The frame's background colour acts as a coloured border:
- **Normal**: system default
- **Red** (`#FF0000`): current cursor position
- **Light blue** (`#E8F4FF`): cell that currently has keyboard focus

---

## Cell Editing

Typing a value and pressing **Return** or clicking away triggers `_commit(row, col)`:
- The string is parsed as an integer.
- If valid (0–255): written to `ECUState.inj_map[row][col]` and the Entry is updated.
- If invalid: the Entry is restored to the last known-good value from `ECUState`.

Changes are local until **Send Map to ECU** is clicked — no automatic transmission on edit.

### Right-click / middle-click context menu

Right-clicking (or middle-clicking, which on macOS pastes clipboard) opens a context menu with two options:
- **Fill row N RPM with V** — sets all 4 cells in that row to the current cell's value.
- **Fill column N% TPS with V** — sets all 10 cells in that column to the current cell's value.

Middle-click paste is suppressed: the handler reads the value from `ECUState` (not the Entry widget) before the paste can corrupt it, then shows the menu.

---

## Live Cursor

`SensorPanel._refresh()` calls `MapEditor.update_cursor(rpm, tps)` at 5 Hz. The editor uses `nearest_rpm_bin()` and `nearest_tps_bin()` (from `protocol.py`) to find the closest bin to the current operating point using nearest-neighbour (not interpolation).

When the cursor moves to a new cell:
1. The previous cell's frame background is restored to its normal colour.
2. The new cell's frame is set to red and its Entry padding is increased from 2 px to 4 px to make the highlight more visible.

---

## Read / Send Map

### Read Map from ECU

1. `MapEditor._read_map()` calls `worker.send_command(CMD_READ_MAP)`, disables the Read button, and sets the status label to "Reading…".
2. The GUI polls the `Future` every 100 ms with `widget.after()`.
3. On success: `ECUState.update_inj_map()` is called by the worker, and `refresh_from_state()` redraws all cells.
4. On failure: the status label shows the error.

### Send Map to ECU

1. `MapEditor._send_map()` first calls `_commit()` on every cell to flush any in-progress edits.
2. `encode_map()` packs the 10×4 array to 40 bytes and `send_command(CMD_WRITE_MAP, payload)` queues it.
3. Same Future-polling pattern as Read. The ECU saves the map to EEPROM and responds with ACK.

---

## Axis Breakpoint Editor

A `ttk.LabelFrame` below the map grid contains two rows of `ttk.Entry` widgets — one per RPM bin and one per TPS bin.

### Validation on send

- RPM values: must be integers 0–65535, must be strictly ascending.
- TPS values: must be floats 0.0–100.0, must be strictly ascending.

If validation passes, values are written to `ECUState.rpm_axis` / `ECUState.tps_axis`, the map grid labels are updated, and `encode_axis()` packs the payload for `CMD_WRITE_AXIS`.

### Read Axis from ECU

Same Future-polling pattern as the map read. On success, `_refresh_axis_from_state()` updates both the Entry widgets and the map grid row/column labels.

---

## Fill All Zeros

The **Fill All Zeros** button sets every cell in `ECUState.inj_map` to 0 and updates all Entry widgets. This does not transmit to the ECU automatically.
