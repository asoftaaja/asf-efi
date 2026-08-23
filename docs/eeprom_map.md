# EEPROM Map

## Overview

The ATmega328P has 1024 bytes of EEPROM. The ECU stores all tuning parameters in distinct sections, each guarded by its own magic byte. On startup `loadFromEEPROM()` checks each magic byte; if absent it writes the compiled-in defaults and sets the magic, so a blank device starts with safe values.

All multi-byte integers are stored big-endian. Floats are IEEE 754 single-precision, also big-endian.

---

## Address Map

| Address | Size (bytes) | Content | Magic address | Magic value |
|---|---|---|---|---|
| 0 | 40 | Injection map: `RPM_BINS × TPS_BINS` uint8 (1 count = 100 µs), row-major | 82 | `0xB1` |
| 40 | 12 | PID coefficients: `kp`, `ki`, `kd` as float32 BE | 82 | (shared) |
| 52 | 8 | Pressure table: `pressure_low_bar`, `pressure_high_bar` as float32 BE | 82 | (shared) |
| 60 | 2 | `pressure_threshold_rpm` as uint16 BE | 82 | (shared) |
| 62 | 10 | IAT correction: `IAT_CORR_BINS` × uint16 BE Q8.8 (256 = 1.0) | 82 | (shared) |
| 72 | 10 | ET correction: `ET_CORR_BINS` × uint16 BE Q8.8 | 82 | (shared) |
| **82** | **1** | **Main magic byte** | — | **0xB1** |
| 83 | 20 | RPM axis: `RPM_BINS` × uint16 BE (RPM values) | 107 | `0xB2` |
| 103 | 4 | TPS axis: `TPS_BINS` × uint8 (0–100 %) | 107 | (shared) |
| **107** | **1** | **Axis magic byte** | — | **0xB2** |
| 108 | 1 | Pump mode: 0 = PID, 1 = always-on | 109 | `0xA9` |
| **109** | **1** | **Pump mode magic byte** | — | **0xA9** |
| 110 | 2 | TPS cal closed: `tps_adc_closed` as uint16 BE | 114 | `0xAD` |
| 112 | 2 | TPS cal open: `tps_adc_open` as uint16 BE | 114 | (shared) |
| **114** | **1** | **TPS cal magic byte** | — | **0xAD** |
| 115 | 2 | Accel pump threshold: `accel_threshold_pct_per_s` as uint16 BE | 121 | `0xAE` |
| 117 | 2 | Accel pump extra: `accel_extra_us` as uint16 BE | 121 | (shared) |
| 119 | 2 | Accel pump duration: `accel_duration_ms` as uint16 BE | 121 | (shared) |
| **121** | **1** | **Accel pump magic byte** | — | **0xAE** |
| 122 | 1 | Shift cut enable: `shift_cut_enabled` as uint8 (0/1) | 127 | `0xAF` |
| 123 | 2 | Shift cut duration: `shift_cut_duration_ms` as uint16 BE | 127 | (shared) |
| 125 | 2 | Shift cut min RPM: `shift_cut_min_rpm` as uint16 BE | 127 | (shared) |
| **127** | **1** | **Shift cut magic byte** | — | **0xAF** |
| **128** | — | **First free byte** | — | — |

Total used: 128 of 1024 bytes.

---

## Magic Byte Strategy

Each independently tunable section has its own magic byte. This allows new sections to be added without invalidating older data: a new section's magic will be absent on upgrade, triggering default initialisation for just that section while existing data loads normally.

If the main magic byte (`0xB1`) is absent (blank device or firmware with a changed magic), all data in the main section (addresses 0–81) is re-initialised to defaults. The magic value was changed from `0xB0` when the map was resized from 12×5 to 10×4 to force re-initialisation on existing devices.

Similarly, the axis magic changed from `0xAC` to `0xB2` when the axis bins were reduced.

---

## Save Functions

Each section has a dedicated save function in `eeprom_map.cpp`:

| Function | Saves | Serial command that triggers it |
|---|---|---|
| `saveInjectionMap()` | addresses 0–39 | `CMD_WRITE_MAP` |
| `savePIDParams()` | addresses 40–51 | `CMD_WRITE_PID` |
| `savePressureTable()` | addresses 52–61 | `CMD_WRITE_PRESSURE` |
| `saveIATCorrection()` | addresses 62–71 | `CMD_WRITE_IAT_CORR` |
| `saveETCorrection()` | addresses 72–81 | `CMD_WRITE_ET_CORR` |
| `saveAxisBreakpoints()` | addresses 83–106 | `CMD_WRITE_AXIS` |
| `savePumpMode()` | address 108 | `CMD_PUMP_MODE` |
| `saveTpsCalibration()` | addresses 110–113 | `CMD_TPS_CAL_CLOSED` / `CMD_TPS_CAL_OPEN` |
| `saveAccelPump()` | addresses 115–120 | `CMD_WRITE_ACCEL_PUMP` |
| `saveShiftCut()` | addresses 122–126 | `CMD_WRITE_SHIFT_CUT` |
