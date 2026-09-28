# Injection Control

## Overview

Injection pulse width is determined by a 10×4 (RPM × TPS) lookup map with bilinear interpolation, then multiplied by two temperature correction coefficients (IAT and ET), scaled by the low-load (powerband) multiplier and topped up by the accelerator-pump shot. The injector is opened by asserting D4 high and closed by a Timer1 compare-A ISR that fires after the calculated pulse width. Two scheduling modes exist: synchronised (one shot per CKPS pulse, below `RPM_SYNC_THRESHOLD`) and fixed-frequency (60 Hz, at or above `RPM_SYNC_THRESHOLD`).

---

## Firmware

### Files

- `injection.h` — pin defines, map dimension constants, `MAX_PULSE_US`, `RPM_SYNC_THRESHOLD`, extern timestamps, function declarations
- `injection.cpp` — axis breakpoints, bilinear interpolation, correction interpolation, `fireInjector`, `shutoffInjector`, Timer1 COMPA ISR
- `powerband.h` / `powerband.cpp` — low-load multiplier ramp — see [Low-Load (Powerband) Multiplier](#low-load-powerband-multiplier)
- `accel_pump.h` / `accel_pump.cpp` — TPS-rate enrichment — see [Accelerator Pump Enrichment](#accelerator-pump-enrichment)
- `asf-efi.ino` — `computeInjectionPulse()`, the helper that combines all of the above

### Map Dimensions

| Axis | Bins (`#define`) | Default breakpoints |
|---|---|---|
| RPM | `RPM_BINS` = 10 | 1000, 4000, 7000, 9000, 11000, 12500, 13500, 14500, 15500, 17000 RPM |
| TPS | `TPS_BINS` = 4 | 0, 30, 60, 100 % |

Map values (`inj_map[RPM_BINS][TPS_BINS]`) are stored as `uint8_t`, where 1 unit = 100 µs. The maximum representable pulse width before corrections is 255 × 100 = 25 500 µs, which coincides with `MAX_PULSE_US` (25 000 µs hard ceiling).

Axis breakpoints are mutable at runtime via `CMD_WRITE_AXIS` and saved to EEPROM.

### Bilinear Interpolation

`interpolateMap()` locates the surrounding cell in each axis, computes Q16 fractional positions (0x0000–0xFFFF = 0.0–1.0), and applies bilinear interpolation:

```cpp
// Q16 fractions
uint32_t rpm_frac = ((uint32_t)rpm_delta << 16) / rpm_range;
uint32_t tps_frac = ((uint32_t)tps_delta << 16) / tps_range;

// Bilinear blend
int32_t v0 = v00 + (((v10 - v00) * (int32_t)rpm_frac) >> 16);
int32_t v1 = v01 + (((v11 - v01) * (int32_t)rpm_frac) >> 16);
int32_t r  = v0  + (((v1  - v0 ) * (int32_t)tps_frac) >> 16);
```

Input clamping: values outside the axis range are extrapolated to the nearest edge cell, not extrapolated beyond it.

### Temperature Correction Tables

Two independent correction arrays apply multiplicative trim to the base pulse width:

| Array | Bins | Temperature breakpoints | EEPROM |
|---|---|---|---|
| `iat_correction[]` | `IAT_CORR_BINS` = 5 | −20, 0, 20, 40, 70 °C | addr 62 |
| `et_correction[]`  | `ET_CORR_BINS`  = 5 | 0, 25, 50, 80, 100 °C | addr 72 |

Values are Q8.8 fixed-point: 256 = 1.0 (no correction), 512 = 2.0 (double fuel), 128 = 0.5 (half fuel). `interpolateCorrection()` performs linear interpolation within each cell using Q16 fractions.

Application in `calculatePulseWidth()`:

```cpp
uint32_t pw = (uint32_t)base_pw * iat_corr >> 8;  // Q8.8 multiply
pw          = pw          * et_corr  >> 8;
if (pw > MAX_PULSE_US) pw = MAX_PULSE_US;
```

### Injector Firing Mechanism

`fireInjector(pulse_width_us)`:

1. Converts µs to Timer1 ticks: `ticks = pulse_width_us * 2` (0.5 µs/tick at clk/8, 16 MHz).
2. Asserts D4 high (`INJECTOR_ON()`).
3. Schedules close: `OCR1A = TCNT1 + ticks` with interrupts disabled to prevent a race between reading `TCNT1` and writing `OCR1A`.
4. Enables `OCIE1A` (Timer1 compare-A interrupt).

`ISR(TIMER1_COMPA_vect)` disables itself and then clears D4 (`INJECTOR_OFF()`). Timer1 is shared with CKPS; the compare-A channel used for injector timing is independent of the input capture and overflow channels.

`shutoffInjector()` immediately disables `OCIE1A` and clears D4 — used by the CKPS timeout safety path.

### Injection Scheduling Modes

| Mode | Condition | Trigger |
|---|---|---|
| Synchronised | `rpm < RPM_SYNC_THRESHOLD` | `injection_trigger` flag set by CKPS ISR; cleared by main loop |
| Fixed 60 Hz | `rpm >= RPM_SYNC_THRESHOLD` | `handle60HzInjection()` in main loop, period = 16 ms |

Both paths go through the same `computeInjectionPulse()` helper before calling `fireInjector()`.

### Final Pulse Width Chain

`calculatePulseWidth()` is a pure function of RPM, TPS and the two temperatures. The powerband multiplier and the accelerator pump shot are applied outside it, in one helper shared by both scheduling modes:

```cpp
static uint16_t computeInjectionPulse(uint32_t now_ms)
{
    uint32_t pw = calculatePulseWidth(rpm, tps, iat_degc, et_degc);
    pw = pw * getPowerbandMultiplier() >> 8;   // low-load / powerband scaling
    pw += getAccelPumpExtra(now_ms);
    return (uint16_t)min(pw, (uint32_t)MAX_PULSE_US);
}
```

So the full chain is: **map (bilinear) → ×100 µs → ×IAT (Q8.8) → ×ET (Q8.8) → clamp → ×powerband (Q8.8) → + accel pump extra → clamp**.

The order is deliberate: the powerband multiplier scales the mapped and temperature-corrected base only, while the accelerator-pump shot is added at full value afterwards — a transient enrichment should not be leaned out by the low-load multiplier. See [Low-Load (Powerband) Multiplier](#low-load-powerband-multiplier) and [Accelerator Pump Enrichment](#accelerator-pump-enrichment) for details.

---

## Constants

| Symbol | Value | Description |
|---|---|---|
| `PIN_INJECTOR` | 4 | D4 = ATmega PD4; high = injector open |
| `MAX_PULSE_US` | 25 000 | Hard ceiling on injector pulse width (µs) |
| `RPM_SYNC_THRESHOLD` | 1 500 | RPM switchover between sync and 60 Hz mode |

---

## Low-Load (Powerband) Multiplier

### Overview

A two-stroke needs far less fuel at light load below its powerband than the injection map — tuned for on-pipe running — delivers. This feature tracks a **powerband state**, true when engine speed *and* throttle position are both at or above user-set thresholds, and scales the computed injection pulse width by a user-set multiplier whenever the engine is outside that state.

To avoid a fuelling step at the transition, the multiplier does not switch abruptly. It ramps linearly between the below-powerband value and 1.00 over a configurable number of crank revolutions (50 by default), in both directions. Progress is counted in engine revolutions rather than milliseconds, so the transition scales with engine speed.

Setting the multiplier to `1.00` disables the feature entirely; there is no separate enable flag.

> **Note:** the multiplier also applies at idle and while cranking, since those conditions are "below powerband" by definition. The map's low-RPM cells must be tuned with the multiplier in mind.

### Firmware

#### Files

- `powerband.h` — public interface: extern parameter globals and four function declarations
- `powerband.cpp` — implementation: ramp state machine and multiplier interpolation

The module depends only on `Arduino.h`, so it links standalone in unit tests.

#### Parameters

| Variable | Type | Default | EEPROM bytes | Description |
|---|---|---|---|---|
| `powerband_multiplier` | uint16 Q8.8 | 128 (0.50) | 130–131 | Injection multiplier applied below the powerband (256 = 1.00) |
| `powerband_threshold_rpm` | uint16 | 9000 | 132–133 | Engine speed at/above which the RPM condition is met |
| `powerband_threshold_tps` | uint8 | 30 | 134 | Throttle percent at/above which the TPS condition is met |
| `powerband_delay_rev` | uint16 | 50 | 135–136 | Crank revolutions for a full ramp (0 = immediate) |

#### Revolution counting

`ckps.cpp` maintains a free-running `crank_revs` counter, incremented once per CKPS falling edge (one crank revolution — see [sensors.md — CKPS](sensors.md#crankshaft-position-sensor-ckps--d8)). It is exposed by `getCrankRevs()`.

The counter is incremented **before** the `pulse_count < 2` startup gate returns early, so cranking revolutions are included. It is `uint8_t` so the main loop can read it atomically without disabling interrupts; consumers track progress with wrap-safe `uint8_t` subtraction, which tolerates any gap shorter than 256 revolutions (normal loop timing produces a delta of 0 or 1). `resetCKPS()` deliberately does **not** clear it — it is a monotonic tick source, not engine state.

#### Ramp state machine

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

#### Multiplier

`getPowerbandMultiplier()` interpolates linearly between the configured multiplier and 1.00:

```cpp
int32_t d = 256L - (int32_t)powerband_multiplier;
return (uint16_t)((int32_t)powerband_multiplier
                  + (d * (int32_t)pb_progress) / (int32_t)rampSpan());
```

Signed arithmetic is used so a multiplier above 1.00 ramps downward just as correctly as the usual below-1.00 case.

`isPowerbandActive()` returns true only when the ramp has fully reached the in-powerband end (`pb_progress == span`). The intermediate state is visible from the multiplier, which the PC app uses to display a third "RAMP" indicator state.

`resetPowerband(crank_revs)` returns the ramp to fully-out and resamples the revolution counter so the next update does not see a stale delta. It is called from the main loop's CKPS-timeout branch, alongside `resetCKPS()`.

#### Integration with injection

The multiplier is applied in `computeInjectionPulse()`, after the temperature corrections and before the accelerator-pump shot — see [Final Pulse Width Chain](#final-pulse-width-chain). `calculatePulseWidth()` itself is unaware of it and remains a pure function of its four arguments.

#### EEPROM layout

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

#### Serial commands

Defined in `comms.h` and handled in `comms.cpp`. Command IDs `0x17`/`0x18` are likewise reserved for shift cut.

| Command | ID | Direction | Payload |
|---|---|---|---|
| `CMD_WRITE_POWERBAND` | `0x19` | PC → ECU | 7 bytes: multiplier (uint16 BE Q8.8), threshold_rpm (uint16 BE), threshold_tps (uint8), delay_rev (uint16 BE) |
| `CMD_READ_POWERBAND` | `0x1A` | PC → ECU (request) / ECU → PC (response) | 7 bytes same layout |

On write, the ECU saves to EEPROM immediately and responds with ACK. A payload of any other length is NACKed and no value changes.

#### Telemetry

Two fields at the end of the 19-byte sensor data packet:

| Offset | Type | Field |
|---|---|---|
| 16 | uint8 | Powerband flag (1 = ramp fully in) |
| 17–18 | uint16 BE | Effective multiplier, Q8.8 (256 = 1.00) |

### PC Application

#### Protocol (`pc_app/protocol.py`)

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

#### State (`pc_app/data_model.py`)

`ECUState.powerband: PowerbandParams` holds the current parameters, with `ECUState.device_powerband_buf` holding what was read from the device on connect (used for the sync warning).

#### Serial worker (`pc_app/serial_worker.py`)

On connect, after reading the accel pump settings, the worker sends `CMD_READ_POWERBAND` and stores the result in `device_powerband_buf`. A read failure is non-fatal — the panel then shows defaults. `SerialWorker.read_powerband()` backs the panel's Read button, and the command queue dispatcher handles the `CMD_READ_POWERBAND` response.

#### GUI panel (`pc_app/gui/map_editor.py`)

The settings live on the **injection map panel**, in a `Powerband / Low-Load Multiplier` LabelFrame below the axis breakpoint editor (grid row `RPM_BINS + 3`). Four entry fields are laid out in two label/entry column pairs, with Read/Send buttons and a status label:

| Field | Valid range |
|---|---|
| Below-powerband multiplier | 0.00 – 2.00 |
| Threshold RPM | 0 – 20000 |
| Threshold TPS (%) | 0 – 100 |
| Activation delay (rev) | 0 – 2000 |

Out-of-range or unparseable input is rejected with a message in the status label and leaves the ECU state untouched. `MapEditor` implements `flush_powerband_to_state()` (called from `MainWindow._flush_all()` before a tune save or write-all) and `refresh_powerband_from_state()`.

#### Indicator (`pc_app/gui/sensor_panel.py`)

A `PBAND` row below `ACCEL` shows three states, since the flag alone only reports the ends of the ramp:

| Display | Condition | Colour |
|---|---|---|
| `ON 1.00` | `powerband_active` is set | green |
| `RAMP 0.72` | multiplier differs from the configured below-powerband value | orange |
| `--- 0.50` | fully out of the powerband | gray |

#### Tune file (`pc_app/tune_io.py`)

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

#### Data logging

Two columns are appended to the CSV log: `powerband_active` (0/1) and `powerband_mult` (3 decimal places). The log viewer plots the multiplier in its own `Powerband Multiplier` subplot, adds the flag to the `Active Flags` step subplot, and shows the live multiplier as a `PBAND` value-bar field. See [data_logging.md](../pc_app/docs/data_logging.md).

### Tests

`test/test_powerband.cpp` (22 tests) covers the ramp up and down, reversal mid-ramp, `delay_rev == 0`, both threshold conditions and their `>=` boundaries, multiplier values of 0.00 / 1.00 / above 1.00, runtime delay reduction, reset behaviour, and revolution counter wraparound.

`test/test_ckps.cpp` covers `getCrankRevs()` incrementing during the startup gate, wrapping at 256, and surviving `resetCKPS()`.

`test/test_comms.cpp` covers the `0x19`/`0x1A` round trip, the NACK on a wrong payload length, and the 19-byte sensor packet including the new flag and multiplier bytes.

---

## Accelerator Pump Enrichment

### Overview

The accelerator pump feature adds extra fuel when the throttle is opened quickly, preventing a lean stumble — the EFI equivalent of a carburetor accelerator pump squirt. When the TPS rate-of-change exceeds a tunable threshold, a linearly-decaying extra pulse width is added to each injection event for a configurable duration.

### Firmware

#### Files

- `accel_pump.h` — public interface: extern parameter globals and two function declarations
- `accel_pump.cpp` — implementation: rate detection, trigger/re-trigger logic, decay computation

#### Parameters

| Variable | Default | EEPROM bytes | Description |
|---|---|---|---|
| `accel_threshold_pct_per_s` | 50 | 115–116 | Minimum TPS rate (%/sec) to trigger enrichment |
| `accel_extra_us` | 500 | 117–118 | Peak extra pulse width added at trigger moment (µs) |
| `accel_duration_ms` | 300 | 119–120 | Time over which enrichment decays to zero (ms) |

#### Detection

TPS is sampled in `updateAccelPump()`, called from the main loop after every `readTPS()`. Sampling is gated to run at most once every 20 ms to avoid noise from rapid calls:

```cpp
int32_t rate = ((int32_t)delta_tps * 1000L) / (int32_t)delta_ms;  // %/sec
if (rate > (int32_t)accel_threshold_pct_per_s) {
    accel_active   = true;
    accel_start_ms = now_ms;   // re-trigger resets the timer
}
```

Re-triggering: if the throttle is held open or opened again before the duration expires, the timer resets from the new trigger moment, so enrichment never drops prematurely.

#### Enrichment Calculation

`getAccelPumpExtra(now_ms)` returns the extra pulse width in µs for the current moment:

```cpp
return (uint16_t)((uint32_t)accel_extra_us * (accel_duration_ms - elapsed) / accel_duration_ms);
```

This is a linear ramp from `accel_extra_us` (at trigger) to 0 (at `accel_duration_ms`).

#### Integration with injection

The extra pulse width is added in `computeInjectionPulse()`, after the powerband multiplier, so the shot is never scaled down by it — see [Final Pulse Width Chain](#final-pulse-width-chain). The sum is clamped to `MAX_PULSE_US`.

#### EEPROM layout

Addresses 115–121, independent of other tuning data. A separate magic byte at address 121 (`0xAE`) guards the block. On first boot (magic absent), defaults are written and the magic is set. On subsequent boots, values are loaded from EEPROM.

```
Address  Size  Content
115      2     accel_threshold_pct_per_s (uint16 big-endian)
117      2     accel_extra_us (uint16 big-endian)
119      2     accel_duration_ms (uint16 big-endian)
121      1     Magic byte (0xAE)
```

#### Serial commands

Defined in `comms.h` and handled in `comms.cpp`:

| Command | ID | Direction | Payload |
|---|---|---|---|
| `CMD_WRITE_ACCEL_PUMP` | `0x15` | PC → ECU | 6 bytes: threshold, extra_us, duration (uint16 BE each) |
| `CMD_READ_ACCEL_PUMP` | `0x16` | PC → ECU (request) / ECU → PC (response) | 6 bytes same layout |

On write, the ECU immediately saves to EEPROM and responds with ACK.

### PC Application

#### Protocol (`pc_app/protocol.py`)

```python
CMD_WRITE_ACCEL_PUMP = 0x15
CMD_READ_ACCEL_PUMP  = 0x16

@dataclass
class AccelPumpParams:
    threshold_pct_per_s: int = 50
    extra_us:            int = 500
    duration_ms:         int = 300

encode_accel_pump(params)   # → 6 bytes struct.pack('>HHH', ...)
decode_accel_pump(payload)  # → AccelPumpParams
```

#### State (`pc_app/data_model.py`)

`ECUState.accel_pump: AccelPumpParams` — holds the current parameters. Written by the serial worker on connect; written by the GUI panel on send.

#### Serial worker (`pc_app/serial_worker.py`)

On connect, after reading corrections, the worker sends `CMD_READ_ACCEL_PUMP` and stores the result in `state.accel_pump` before setting `config_fresh`. The command queue dispatcher also handles the `CMD_READ_ACCEL_PUMP` response code.

#### GUI panel (`pc_app/gui/accel_pump_panel.py`)

`AccelPumpPanel` — a `ttk.LabelFrame` placed at column 3 of the "PID & Pressure" tab. Contains three entry fields (threshold, extra_us, duration_ms), a Send button, and a status label. Implements:

- `refresh_from_state()` — populates entries from `ECUState.accel_pump`
- `flush_to_state()` — parses entries back into `ECUState.accel_pump` (used before save/write-all)

#### Tune file (`pc_app/tune_io.py`)

Saved as a nested dict under the `"accel_pump"` key in the JSON tune file:

```json
"accel_pump": {
  "threshold_pct_per_s": 50,
  "extra_us": 500,
  "duration_ms": 300
}
```

Loading is backward-compatible: if the key is absent (old tune file), all three parameters fall back to their defaults.
