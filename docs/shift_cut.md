# Shift Cut (Ignition Cut on Gear Shift)

## Overview

A switch on the gear lever signals the ECU that a shift is starting. The ECU responds with a short high pulse on a dedicated output that drives the ignition cut, unloading the dog rings so the shift completes without the rider closing the throttle.

The switch is sampled inside the existing CKPS input capture ISR — once per crank pulse — so no extra timer, external interrupt, or polling loop is needed, and the sampling rate scales naturally with engine speed. Detection sets a flag; the main loop drives the output and times the pulse.

---

## Hardware

| Signal | Pin | Notes |
|---|---|---|
| Shift sensor | D2 (PD2) | `INPUT_PULLUP`, active low — the switch shorts the pin to ground |
| Ignition cut | D7 (PD7) | Idle low, high during the cut |

D2 is on PORTD, so the ISR reads it with a single `PIND` test rather than a `digitalRead()` call, and D2 is INT0 should a true external interrupt ever be wanted. D7 is likewise PORTD, driven with the same direct-port macro style as the injector output.

Timer1 (CKPS capture + injector close) and Timer2 (fuel pump PWM) are untouched by this feature.

---

## Firmware

### Files

- `shift_cut.h` — pin defines, port macros, duration limits, extern parameter globals, function declarations
- `shift_cut.cpp` — sampling, re-arm logic, pulse timing

### Parameters

| Variable | Default | EEPROM bytes | Description |
|---|---|---|---|
| `shift_cut_enabled` | 1 | 122 | Master on/off (0 = feature disabled entirely) |
| `shift_cut_duration_ms` | 50 | 123–124 | Ignition cut pulse length, 10–100 ms |
| `shift_cut_min_rpm` | 3000 | 125–126 | Below this RPM the switch is ignored |

The minimum-RPM gate prevents a bump of the lever at idle or while stationary from cutting ignition.

### Sampling — `sampleShiftSensor(rpm)`

Called from `ISR(TIMER1_CAPT_vect)` in `ckps.cpp`, placed **after** the RPM calculation and **before** the `pulse_count < 2` pump-enable gate's early return, so the first two pulses after startup are not skipped.

```cpp
if (!shift_cut_enabled) return;

if (!SHIFT_PRESSED()) {          // switch released -> re-arm
    armed = true;
    return;
}

if (armed && current_rpm >= shift_cut_min_rpm) {
    armed         = false;
    shift_trigger = true;
}
```

Debouncing is free: the switch is read at most once per crank revolution, and re-arming requires actually seeing it released. Holding the lever down therefore produces exactly one cut, no matter how many pulses go by.

`shift_trigger` is the same ISR-sets/loop-clears pattern as `injection_trigger` in `ckps.cpp`.

### Pulse timing — `updateShiftCut(now_ms)`

Called unconditionally from `loop()` (not gated on `pump_active`), so an in-flight pulse always gets terminated:

```cpp
if (shift_trigger) {
    shift_trigger = false;
    if (!cutting) { IGN_CUT_ON(); cut_start_ms = now_ms; cutting = true; }
}
if (cutting && (now_ms - cut_start_ms >= shift_cut_duration_ms)) {
    IGN_CUT_OFF();
    cutting = false;
}
```

A press arriving while a cut is already running is discarded rather than extending or restarting it, bounding the worst-case cut length at `shift_cut_duration_ms`.

**Why `millis()` and not Timer1.** Timer1 runs at prescaler 8 (0.5 µs/tick) and wraps every 32.768 ms, so a 100 ms pulse does not fit in a single `OCR1B` compare. The main loop runs at roughly 1 ms, which is acceptable jitter on a 10–100 ms pulse. This mirrors the `primePump()`/`isPriming()` timing pattern in `pump.cpp`.

### Reset

`resetShiftCut()` drops the output, clears the trigger flag, and re-arms. It is called from `resetCKPS()`, which the main loop invokes on the `isCKPSTimeout()` safety path — so a stall or a lost CKPS signal always leaves the ignition cut output low.

Writing `shift_cut_enabled = 0` over serial also calls `resetShiftCut()`, so disabling the feature never leaves the output stuck high.

---

## EEPROM

Section at addresses 122–127 with its own magic byte, following the per-section strategy described in [eeprom_map.md](eeprom_map.md):

```
122  1  shift_cut_enabled (uint8, 0/1)
123  2  shift_cut_duration_ms (uint16 BE)
125  2  shift_cut_min_rpm (uint16 BE)
127  1  Shift cut magic — 0xAF
```

`loadFromEEPROM()` writes the compile-time defaults and the magic byte if the magic is absent, so existing controllers pick up the feature without a full EEPROM re-init. On load, `shift_cut_duration_ms` is clamped to 10–100 ms to guard against a corrupted cell. `saveShiftCut()` persists the section.

---

## Serial Protocol

| ID | Name | Direction | Payload |
|---|---|---|---|
| 0x17 | `CMD_WRITE_SHIFT_CUT` | PC→AVR | 5 bytes |
| 0x18 | `CMD_READ_SHIFT_CUT` | PC→AVR request (empty), AVR→PC response | 5 bytes |

Payload layout (both directions):

| Offset | Type | Field |
|---|---|---|
| 0 | uint8 | enabled (0/1) |
| 1–2 | uint16 BE | duration_ms |
| 3–4 | uint16 BE | min_rpm |

The write command NACKs on a wrong payload length or a duration outside 10–100 ms — the value is rejected rather than silently clamped, so the tuner reports the error. On success it saves to EEPROM and ACKs.

No shift cut status is included in the sensor data packet; the feature is not currently telemetered or logged.

---

## PC Application

- `protocol.py` — `CMD_WRITE_SHIFT_CUT` / `CMD_READ_SHIFT_CUT`, `SHIFT_CUT_MIN_MS` / `SHIFT_CUT_MAX_MS`, the `ShiftCutParams` data class, `encode_shift_cut()` / `decode_shift_cut()`
- `data_model.py` — `ECUState.shift_cut` plus the `device_shift_cut_buf` device snapshot
- `serial_worker.py` — read on connect into `device_shift_cut_buf`; `CMD_READ_SHIFT_CUT` branch in the command queue handler
- `gui/shift_cut_panel.py` — "Shift Cut" panel on the ECU Settings tab: an Enabled checkbox, duration and min-RPM entries, and a Send button. Duration is validated against 10–100 ms before anything is sent.
- `gui/main_window.py` — panel registered in `_tuning_panels` (enable/disable with connection), and included in `_write_all_to_device()`, `_load_device_values()` and the `_diff_device_vs_state()` sync warning
- `tune_io.py` — `"shift_cut"` object in the tune file JSON; loaded with per-key defaults so tune files written before this feature still open

---

## Tests

`test/test_shift_cut.cpp` drives the `PIND` mock (bit `PD2`) and asserts on the `PORTD` mock (bit `PD7`): enable and min-RPM gating, pulse assertion, exact duration at 10/50/100 ms, one-cut-per-press with the lever held, re-arm after release, no extension on re-trigger, `resetShiftCut()` mid-pulse, and that the injector bit `PD4` is left alone.

`test/test_ckps.cpp` covers the ISR hook: sampling happens on the startup pulses too, no cut with the switch released, and `resetCKPS()` clearing the output.

`test/test_comms.cpp` covers 0x17/0x18: parameter update and ACK, NACK on out-of-range duration, NACK on wrong length, and the read response layout.
