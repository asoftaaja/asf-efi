# ASF EFI — Data Logging

## Overview

The PC application can record live ECU sensor data to a CSV file at 5 Hz. A separate log viewer script can open any recorded file and plot all sensor channels interactively.

---

## Recording a Log

1. Connect to the ECU — the **Start Log** button in the Sensors panel becomes active.
2. Click **Start Log**. A red **● RECORDING** indicator appears and the button label changes to **Stop Log**.
3. Click **Stop Log** to finish. The file is flushed and closed.

Disconnecting the device while a log is active also closes the file cleanly.

---

## Log Files

Files are saved to `pc_app/logs/` (created automatically). The directory is relative to `main.py`.

### File naming

```
asf_efi_datalog_YYYYMMDD_HHMMSS.csv
```

Example: `asf_efi_datalog_20260518_143022.csv`

### File format

```
#
#
#
#
#
timestamp,rpm,tps_pct,fps_bar,iat_degc,et_degc,pump_active,bat_v,pump_duty_pct,inj_duty_pct,inj_open_ms,accel_active,powerband_active,powerband_mult
2026-05-18T14:30:22.401,1450,23.14,3.501,21.0,68.3,1,12.45,58.82,12.35,2.1,0
...
```

The first 5 lines are reserved comment lines (blank `#` lines) for future metadata use. The 6th line is the CSV column header. Data rows follow at ~5 Hz.

### Column reference

| Column | Unit | Notes |
|---|---|---|
| `timestamp` | ISO-8601 with ms | `datetime.isoformat(timespec='milliseconds')` |
| `rpm` | rev/min | Integer |
| `tps_pct` | % (0–100) | Throttle position |
| `fps_bar` | bar | Fuel pressure |
| `iat_degc` | °C | Intake air temperature |
| `et_degc` | °C | Engine temperature |
| `pump_active` | 0 / 1 | Fuel pump state |
| `bat_v` | V | Battery voltage |
| `pump_duty_pct` | % (0–100) | Pump PWM duty (raw 0–255 → %) |
| `inj_duty_pct` | % (0–100) | Injector duty cycle |
| `inj_open_ms` | ms (0.0–25.0) | Injector open duration, 0.1 ms resolution |
| `accel_active` | 0 / 1 | Accelerator pump enrichment active |
| `powerband_active` | 0 / 1 | Powerband ramp has fully reached the in-powerband end |
| `powerband_mult` | 0.500 – 1.000 | Effective low-load injection multiplier (1.0 = unaltered) |

---

## Implementation

| File | Role |
|---|---|
| `pc_app/data_logger.py` | `DataLogger` class — file creation, worker thread, row writing, close |
| `pc_app/data_model.py` | `ECUState.log_sensor_fresh` event — set whenever a new sensor packet arrives |
| `pc_app/gui/sensor_panel.py` | Start/Stop button, recording/error indicator, polls `DataLogger.error_msg` |
| `pc_app/gui/main_window.py` | Enables the button on connect; calls `stop_log()` on disconnect |

### Thread model

Logging runs on its own background thread (`DataLoggerWorker`), spawned by `DataLogger.start()` and joined by `DataLogger.stop()`. The worker waits on `ECUState.log_sensor_fresh` (a parallel `threading.Event` set by `ECUState.update_sensors()` alongside `sensor_fresh`). This keeps logging fully decoupled from the Tk main loop:

- Modal dialogs, map redraws, and other GUI activity cannot pause logging.
- Slow disk I/O cannot freeze the GUI.
- The GUI's own `sensor_fresh` event is untouched by the logger, so both consumers see every packet.

### Skipped frames

If no new sensor packet arrives within one 200 ms sample window, the worker writes nothing for that tick — a serial stall is visible as a gap in the `timestamp` column rather than as a duplicated row.

### Flush policy

The worker flushes every 5 rows (~1 s) and unconditionally on `stop()`. Up to ~1 s of trailing data may be lost on a hard process crash; this trades a small risk window for far fewer syscalls.

### Error handling

If a write raises `OSError` (full disk, removed USB stick, etc.), the worker stores the message in `DataLogger.error_msg` and exits cleanly. The `SensorPanel` refresh loop polls this field at 5 Hz and switches the indicator to `● LOG ERROR`, resetting the button to **Start Log**.

---

## Log Viewer

```bash
python log_viewer.py                        # open with file picker
python log_viewer.py logs/asf_efi_datalog_20260518_143022.csv  # open directly
```

Requires `matplotlib` (`pip install matplotlib`).

The viewer opens a file-picker dialog and plots all sensor channels in 9 shared-X subplots:

| Subplot | Channels |
|---|---|
| RPM | RPM |
| Throttle Position | TPS % |
| Fuel Pressure | FPS bar |
| Temperature | IAT °C, ET °C |
| Battery Voltage | VBAT V |
| Duty Cycles | Injector %, Pump % |
| Injector Pulse | Injector open duration ms |
| Powerband Multiplier | Effective low-load multiplier |
| Active Flags | Pump active, Accel pump active, Powerband active |

The X axis shows elapsed time in seconds from the first sample. All subplots share the same X axis — pan and zoom in any subplot and all others follow. The matplotlib navigation toolbar provides zoom, pan, and save controls.
