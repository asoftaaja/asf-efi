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
timestamp,rpm,tps_pct,fps_bar,iat_degc,et_degc,pump_active,bat_v,pump_duty_pct,inj_duty_pct,inj_open_ms,accel_active
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

---

## Implementation

| File | Role |
|---|---|
| `pc_app/data_logger.py` | `DataLogger` class — file creation, row writing, close |
| `pc_app/gui/sensor_panel.py` | Start/Stop button, indicator label, calls `DataLogger.log()` at 5 Hz |
| `pc_app/gui/main_window.py` | Enables the button on connect; calls `stop_log()` on disconnect |

`DataLogger` is instantiated inside `SensorPanel` and is only accessed from the Tk main thread, so no additional locking is required.

---

## Log Viewer

```bash
python log_viewer.py                        # open with file picker
python log_viewer.py logs/asf_efi_datalog_20260518_143022.csv  # open directly
```

Requires `matplotlib` (`pip install matplotlib`).

The viewer opens a file-picker dialog and plots all sensor channels in 8 shared-X subplots:

| Subplot | Channels |
|---|---|
| RPM | RPM |
| Throttle Position | TPS % |
| Fuel Pressure | FPS bar |
| Temperature | IAT °C, ET °C |
| Battery Voltage | VBAT V |
| Duty Cycles | Injector %, Pump % |
| Injector Pulse | Injector open duration ms |
| Active Flags | Pump active, Accel pump active |

The X axis shows elapsed time in seconds from the first sample. All subplots share the same X axis — pan and zoom in any subplot and all others follow. The matplotlib navigation toolbar provides zoom, pan, and save controls.
