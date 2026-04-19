# Accelerator Pump Enrichment

## Overview

The accelerator pump feature adds extra fuel when the throttle is opened quickly, preventing a lean stumble — the EFI equivalent of a carburetor accelerator pump squirt. When the TPS rate-of-change exceeds a tunable threshold, a linearly-decaying extra pulse width is added to each injection event for a configurable duration.

---

## Firmware

### Files

- `accel_pump.h` — public interface: extern parameter globals and two function declarations
- `accel_pump.cpp` — implementation: rate detection, trigger/re-trigger logic, decay computation

### Parameters

| Variable | Default | EEPROM bytes | Description |
|---|---|---|---|
| `accel_threshold_pct_per_s` | 50 | 200–201 | Minimum TPS rate (%/sec) to trigger enrichment |
| `accel_extra_us` | 500 | 202–203 | Peak extra pulse width added at trigger moment (µs) |
| `accel_duration_ms` | 300 | 204–205 | Time over which enrichment decays to zero (ms) |

### Detection

TPS is sampled in `updateAccelPump()`, called from the main loop after every `readTPS()`. Sampling is gated to run at most once every 20 ms to avoid noise from rapid calls:

```cpp
int32_t rate = ((int32_t)delta_tps * 1000L) / (int32_t)delta_ms;  // %/sec
if (rate > (int32_t)accel_threshold_pct_per_s) {
    accel_active   = true;
    accel_start_ms = now_ms;   // re-trigger resets the timer
}
```

Re-triggering: if the throttle is held open or opened again before the duration expires, the timer resets from the new trigger moment, so enrichment never drops prematurely.

### Enrichment Calculation

`getAccelPumpExtra(now_ms)` returns the extra pulse width in µs for the current moment:

```cpp
return (uint16_t)((uint32_t)accel_extra_us * (accel_duration_ms - elapsed) / accel_duration_ms);
```

This is a linear ramp from `accel_extra_us` (at trigger) to 0 (at `accel_duration_ms`).

### Integration with Injection

Applied at both injection sites in `asf-efi.ino` — the CKPS-synchronised low-RPM path and the fixed 60 Hz path:

```cpp
uint16_t pw    = calculatePulseWidth(rpm, tps, iat_degc, et_degc);
uint16_t accel = getAccelPumpExtra(millis());
if (accel > 0) pw = (uint16_t)min((uint32_t)pw + accel, (uint32_t)MAX_PULSE_US);
fireInjector(pw);
```

`MAX_PULSE_US` (25000 µs) is defined in `injection.h` and used here as a hard ceiling.

### EEPROM Layout

Addresses 200–206, independent of other tuning data. A separate magic byte at address 206 (`0xAE`) guards the block. On first boot (magic absent), defaults are written and the magic is set. On subsequent boots, values are loaded from EEPROM.

```
Address  Size  Content
200      2     accel_threshold_pct_per_s (uint16 big-endian)
202      2     accel_extra_us (uint16 big-endian)
204      2     accel_duration_ms (uint16 big-endian)
206      1     Magic byte (0xAE)
```

### Serial Commands

Defined in `comms.h` and handled in `comms.cpp`:

| Command | ID | Direction | Payload |
|---|---|---|---|
| `CMD_WRITE_ACCEL_PUMP` | `0x15` | PC → ECU | 6 bytes: threshold, extra_us, duration (uint16 BE each) |
| `CMD_READ_ACCEL_PUMP` | `0x16` | PC → ECU (request) / ECU → PC (response) | 6 bytes same layout |

On write, the ECU immediately saves to EEPROM and responds with ACK.

---

## PC Application

### Protocol (`pc_app/protocol.py`)

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

### State (`pc_app/data_model.py`)

`ECUState.accel_pump: AccelPumpParams` — holds the current parameters. Written by the serial worker on connect; written by the GUI panel on send.

### Serial Worker (`pc_app/serial_worker.py`)

On connect, after reading corrections, the worker sends `CMD_READ_ACCEL_PUMP` and stores the result in `state.accel_pump` before setting `config_fresh`. The command queue dispatcher also handles the `CMD_READ_ACCEL_PUMP` response code.

### GUI Panel (`pc_app/gui/accel_pump_panel.py`)

`AccelPumpPanel` — a `ttk.LabelFrame` placed at column 3 of the "PID & Pressure" tab. Contains three entry fields (threshold, extra_us, duration_ms), a Send button, and a status label. Implements:

- `refresh_from_state()` — populates entries from `ECUState.accel_pump`
- `flush_to_state()` — parses entries back into `ECUState.accel_pump` (used before save/write-all)

### Tune File (`pc_app/tune_io.py`)

Saved as a nested dict under the `"accel_pump"` key in the JSON tune file:

```json
"accel_pump": {
  "threshold_pct_per_s": 50,
  "extra_us": 500,
  "duration_ms": 300
}
```

Loading is backward-compatible: if the key is absent (old tune file), all three parameters fall back to their defaults.
