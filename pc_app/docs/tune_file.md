# PC App — Tune Files

## Overview

Tune files are JSON documents that capture all editable ECU parameters in one file. They are saved to and loaded from the `pc_app/tunefiles/` directory. The last-used file is remembered across sessions.

---

## File Format

Files are plain JSON, pretty-printed with 2-space indentation. There is no version field — missing keys fall back to firmware defaults on load.

```json
{
  "inj_map": [[0, 0, 0, 0], ...],        // 10×4 list of uint8 (1 unit = 100 µs)
  "pid": {
    "kp": 20.0,
    "ki": 1.0,
    "kd": 0.5
  },
  "pressure": {
    "low_bar": 2.0,
    "high_bar": 3.0,
    "threshold_rpm": 3000
  },
  "iat_corr": [1.0, 1.0, 1.0, 1.0, 1.0],   // 5 Q8.8 multipliers (1.0 = no correction)
  "et_corr":  [1.0, 1.0, 1.0, 1.0, 1.0],
  "rpm_axis": [1000, 4000, 7000, 9000, 11000, 12500, 13500, 14500, 15500, 17000],
  "tps_axis": [0.0, 0.3, 0.6, 1.0],          // stored as 0.0–1.0 fractions
  "pump_mode_always_on": false,
  "accel_pump": {
    "threshold_pct_per_s": 50,
    "extra_us": 500,
    "duration_ms": 300
  },
  "shift_cut": {
    "enabled": true,
    "duration_ms": 50,        // ignition cut pulse length, 10-100 ms
    "min_rpm": 3000,
    "lockout_ms": 500         // switch ignored this long after a shift, 500-1000 ms
  },
  "alarms": {
    "et_threshold": 110.0,
    "vbat_threshold": 11.5
  }
}
```

---

## Load and Save

Implemented in `tune_io.py`.

**`save_tunefile(path, state)`** — serialises the entire `ECUState` to the file, then writes the path to `tunefiles/.last`.

**`load_tunefile(path, state)`** — reads the JSON and writes all fields into `ECUState`. Fields absent from the file (older tune files) are silently defaulted:
- `rpm_axis`, `tps_axis` → compile-time defaults from `protocol.py`
- `pump_mode_always_on` → `False`
- `accel_pump` → threshold 50 %/s, extra 500 µs, duration 300 ms
- `shift_cut` → enabled, 50 ms cut, 3000 min RPM, 500 ms lockout
- `alarms` → ET threshold 110 °C, VBAT threshold 11.5 V

After loading, the path is written to `tunefiles/.last`.

---

## Auto-load on Startup

`MainWindow._autoload_tunefile()` runs before the window is shown:
1. Reads `tunefiles/.last` (if it exists) to get the last file path.
2. If the file still exists, calls `load_tunefile()` silently.
3. On any error (file missing, corrupt JSON), the auto-load is skipped without showing an error — the app starts with defaults.

---

## Sync Warning Integration

When a tune file is loaded while the device is connected, `MainWindow._on_tunefile_loaded()` compares the newly loaded values against the device buffers read at connect time. If they differ, the gold sync warning bar is shown offering to write all to device or discard and load from device. See [connection.md](connection.md) for details.

---

## GUI — Tune File Panel

`gui/tune_file_panel.py` — a `ttk.LabelFrame` with a path Entry, a **Load** button, and a **Save** button.

- **Load**: opens a `filedialog.askopenfilename` restricted to `*.json`, calls `load_tunefile()`, then triggers `on_loaded` callback to refresh all editor panels.
- **Save**: calls `flush_to_state()` on all panels to commit any unsaved edits, opens `filedialog.asksaveasfilename`, then calls `save_tunefile()`.
- The path Entry is read-only and updated programmatically via `set_path()`.
