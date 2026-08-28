# Low-Load (Powerband) Injection Multiplier

## Overview

A two-stroke needs far less fuel at light load below its powerband than the injection map — tuned for on-pipe running — delivers. This feature tracks a **powerband state**, true when engine speed *and* throttle position are both at or above user-set thresholds, and scales the computed injection pulse width by a user-set multiplier whenever the engine is outside that state.

To avoid a fuelling step at the transition, the multiplier does not switch abruptly. It ramps linearly between the below-powerband value and 1.00 over a configurable number of crank revolutions (50 by default), in both directions. Progress is counted in engine revolutions rather than milliseconds, so the transition scales with engine speed.

Setting the multiplier to `1.00` disables the feature entirely; there is no separate enable flag.

> **Note:** the multiplier also applies at idle and while cranking, since those conditions are "below powerband" by definition. The map's low-RPM cells must be tuned with the multiplier in mind.

---

## Firmware

### Files

- `powerband.h` — public interface: extern parameter globals and four function declarations
- `powerband.cpp` — implementation: ramp state machine and multiplier interpolation

The module depends only on `Arduino.h`, so it links standalone in unit tests.

### Parameters

| Variable | Type | Default | EEPROM bytes | Description |
|---|---|---|---|---|
| `powerband_multiplier` | uint16 Q8.8 | 128 (0.50) | 130–131 | Injection multiplier applied below the powerband (256 = 1.00) |
| `powerband_threshold_rpm` | uint16 | 9000 | 132–133 | Engine speed at/above which the RPM condition is met |
| `powerband_threshold_tps` | uint8 | 30 | 134 | Throttle percent at/above which the TPS condition is met |
| `powerband_delay_rev` | uint16 | 50 | 135–136 | Crank revolutions for a full ramp (0 = immediate) |

### Revolution counting

`ckps.cpp` maintains a free-running `crank_revs` counter, incremented once per CKPS falling edge (one crank revolution — see [ckps.md](ckps.md)). It is exposed by `getCrankRevs()`.

The counter is incremented **before** the `pulse_count < 2` startup gate returns early, so cranking revolutions are included. It is `uint8_t` so the main loop can read it atomically without disabling interrupts; consumers track progress with wrap-safe `uint8_t` subtraction, which tolerates any gap shorter than 256 revolutions (normal loop timing produces a delta of 0 or 1). `resetCKPS()` deliberately does **not** clear it — it is a monotonic tick source, not engine state.

### Ramp state machine

`updatePowerband(rpm, tps, crank_revs)` is called once per main loop iteration, right after `updateAccelPump()`:

```cpp
uint8_t delta = (uint8_t)(crank_revs_now - pb_last_revs);   // wrap-safe
uint16_t span = powerband_delay_rev ? powerband_delay_rev : 1;
bool in_band = (rpm_val >= powerband_threshold_rpm) &&
               (tps_val >= powerband_threshold_tps);
if (in_band) pb_progress = min(pb_progress + delta, span);
else         pb_progress = (delta >= pb_progress) ? 0 : pb_progress - delta;
```

`pb_progress` walks from 0 (fully out of the powerband) to `span` (fully in). Both conditions are required: losing either one starts the ramp down, and reversing mid-ramp resumes from the current position rather than snapping to an end.

If `powerband_delay_rev` is reduced at runtime while the ramp sits beyond the new span, progress is clamped down on the next update.

### Multiplier

`getPowerbandMultiplier()` interpolates linearly between the configured multiplier and 1.00:

```cpp
int32_t d = 256L - (int32_t)powerband_multiplier;
return (uint16_t)((int32_t)powerband_multiplier
                  + (d * (int32_t)pb_progress) / (int32_t)rampSpan());
```

Signed arithmetic is used so a multiplier above 1.00 ramps downward just as correctly as the usual below-1.00 case.

`isPowerbandActive()` returns true only when the ramp has fully reached the in-powerband end (`pb_progress == span`). The intermediate state is visible from the multiplier, which the PC app uses to display a third "RAMP" indicator state.

`resetPowerband(crank_revs)` returns the ramp to fully-out and resamples the revolution counter so the next update does not see a stale delta. It is called from the main loop's CKPS-timeout branch, alongside `resetCKPS()`.

### Integration with injection

Both injection paths — the CKPS-synchronised low-RPM path and the fixed 60 Hz path — go through one helper in `asf-efi.ino`:

```cpp
static uint16_t computeInjectionPulse(uint32_t now_ms)
{
    uint32_t pw = calculatePulseWidth(rpm, tps, iat_degc, et_degc);
    pw = pw * getPowerbandMultiplier() >> 8;   // low-load / powerband scaling
    pw += getAccelPumpExtra(now_ms);
    return (uint16_t)min(pw, (uint32_t)MAX_PULSE_US);
}
```

Order matters: the multiplier scales the mapped and temperature-corrected base value only. The accelerator-pump shot is added at full value afterwards, since a transient enrichment should not be leaned out by the low-load multiplier. `calculatePulseWidth()` itself is unchanged and remains a pure function of its four arguments.

### EEPROM layout

Addresses 130–137, guarded by an independent magic byte at 137 (`0xB4`). On first boot the compile-time defaults are written and the magic is set; on subsequent boots the values are loaded and range-clamped.

```
Address  Size  Content
130      2     powerband_multiplier (uint16 BE, Q8.8)
132      2     powerband_threshold_rpm (uint16 BE)
134      1     powerband_threshold_tps (uint8, percent)
135      2     powerband_delay_rev (uint16 BE)
137      1     Magic byte (0xB4)
```

Addresses 122–129 are deliberately skipped: they are reserved for the shift cut feature developed on the `feature/shift-cut` branch, so that branch merges without renumbering or forcing an EEPROM re-init. See [eeprom_map.md](eeprom_map.md).

### Serial commands

Defined in `comms.h` and handled in `comms.cpp`. Command IDs `0x17`/`0x18` are likewise reserved for shift cut.

| Command | ID | Direction | Payload |
|---|---|---|---|
| `CMD_WRITE_POWERBAND` | `0x19` | PC → ECU | 7 bytes: multiplier (uint16 BE Q8.8), threshold_rpm (uint16 BE), threshold_tps (uint8), delay_rev (uint16 BE) |
| `CMD_READ_POWERBAND` | `0x1A` | PC → ECU (request) / ECU → PC (response) | 7 bytes same layout |

On write, the ECU saves to EEPROM immediately and responds with ACK. A payload of any other length is NACKed and no value changes.

### Telemetry

The sensor data packet grew from 16 to 19 bytes:

| Offset | Type | Field |
|---|---|---|
| 16 | uint8 | Powerband flag (1 = ramp fully in) |
| 17–18 | uint16 BE | Effective multiplier, Q8.8 (256 = 1.00) |

---

## PC Application

### Protocol (`pc_app/protocol.py`)

```python
CMD_WRITE_POWERBAND = 0x19
CMD_READ_POWERBAND  = 0x1A

class PowerbandParams:
    def __init__(self, multiplier=0.5, threshold_rpm=9000,
                 threshold_tps_pct=30, delay_rev=50):
        ...

encode_powerband(params)   # -> 7 bytes struct.pack('>HHBH', ...)
decode_powerband(payload)  # -> PowerbandParams
```

The multiplier is a float on the PC side and is converted to/from Q8.8 at the protocol boundary. The TPS threshold is an **integer percent (0–100)** end to end, matching the uint8 wire format — unlike `state.tps_axis`, it is never stored as a fraction.

`SensorData` carries `powerband_active` (bool) and `powerband_mult` (float) from the telemetry bytes above.

### State (`pc_app/data_model.py`)

`ECUState.powerband: PowerbandParams` holds the current parameters, with `ECUState.device_powerband_buf` holding what was read from the device on connect (used for the sync warning).

### Serial worker (`pc_app/serial_worker.py`)

On connect, after reading the accel pump settings, the worker sends `CMD_READ_POWERBAND` and stores the result in `device_powerband_buf`. A read failure is non-fatal — the panel then shows defaults. `SerialWorker.read_powerband()` backs the panel's Read button, and the command queue dispatcher handles the `CMD_READ_POWERBAND` response.

### GUI panel (`pc_app/gui/map_editor.py`)

The settings live on the **injection map panel**, in a `Powerband / Low-Load Multiplier` LabelFrame below the axis breakpoint editor (grid row `RPM_BINS + 3`). Four entry fields are laid out in two label/entry column pairs, with Read/Send buttons and a status label:

| Field | Valid range |
|---|---|
| Below-powerband multiplier | 0.00 – 2.00 |
| Threshold RPM | 0 – 20000 |
| Threshold TPS (%) | 0 – 100 |
| Activation delay (rev) | 0 – 2000 |

Out-of-range or unparseable input is rejected with a message in the status label and leaves the ECU state untouched. `MapEditor` implements `flush_powerband_to_state()` (called from `MainWindow._flush_all()` before a tune save or write-all) and `refresh_powerband_from_state()`.

### Indicator (`pc_app/gui/sensor_panel.py`)

A `PBAND` row below `ACCEL` shows three states, since the flag alone only reports the ends of the ramp:

| Display | Condition | Colour |
|---|---|---|
| `ON 1.00` | `powerband_active` is set | green |
| `RAMP 0.72` | multiplier differs from the configured below-powerband value | orange |
| `--- 0.50` | fully out of the powerband | gray |

### Tune file (`pc_app/tune_io.py`)

```json
"powerband": {
  "multiplier": 0.5,
  "threshold_rpm": 9000,
  "threshold_tps_pct": 30,
  "delay_rev": 50
}
```

Loading is backward-compatible: if the key is absent (old tune file), all four parameters fall back to their defaults.

The sync warning lists `powerband` when the device values differ from the loaded tune file. The multiplier is compared as the Q8.8 value actually sent, so a tune-file `0.60` does not read as different from the device's quantised `0.5977`.

### Data logging

Two columns are appended to the CSV log: `powerband_active` (0/1) and `powerband_mult` (3 decimal places). The log viewer plots the multiplier in its own `Powerband Multiplier` subplot, adds the flag to the `Active Flags` step subplot, and shows the live multiplier as a `PBAND` value-bar field. See [data_logging.md](../pc_app/docs/data_logging.md).

---

## Tests

`test/test_powerband.cpp` (22 tests) covers the ramp up and down, reversal mid-ramp, `delay_rev == 0`, both threshold conditions and their `>=` boundaries, multiplier values of 0.00 / 1.00 / above 1.00, runtime delay reduction, reset behaviour, and revolution counter wraparound.

`test/test_ckps.cpp` covers `getCrankRevs()` incrementing during the startup gate, wrapping at 256, and surviving `resetCKPS()`.

`test/test_comms.cpp` covers the `0x19`/`0x1A` round trip, the NACK on a wrong payload length, and the 19-byte sensor packet including the new flag and multiplier bytes.
