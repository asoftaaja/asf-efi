# Fuel Pump Control

## Overview

The fuel pump is driven by a PWM signal on D3 (Timer2 OC2B, via `analogWrite`). It operates in two modes: **PID mode** (default) — a PI controller regulates fuel pressure to a target derived from an RPM-based table — and **always-on mode** — pump runs at full PWM whenever active. The pump is not started at power-on; it waits for two valid CKPS pulses before the CKPS ISR sets `pump_active = true`. A timed prime function allows the pump to be run from the PC app before the engine starts.

---

## Firmware

### Files

- `pump.h` — pin defines, `PRIME_DURATION_MS`, `pump_pwm` extern, function declarations
- `pump.cpp` — PID state, `initPump`, `updatePump`, `disablePump`, `primePump`, `isPriming`

### Pump Enable Gating

`pump_active` is a global `volatile bool` defined in `asf-efi.ino`. It is set to `true` inside `ISR(TIMER1_CAPT_vect)` after the second CKPS pulse. The main loop checks `pump_active` before calling `updatePump()`.

Three flags govern pump behaviour:

| Flag | Type | Meaning |
|---|---|---|
| `pump_active` | `volatile bool` | Set by CKPS ISR; true = engine running, pump allowed |
| `pump_manual` | `bool` | Set by `CMD_PUMP_SET`; keeps pump on regardless of engine state |
| `pump_mode_always_on` | `bool` | Set by `CMD_PUMP_MODE`; skips PID, writes 255 to PWM |

### PID Controller

`updatePump(fps_sixteenth_bar, rpm)` implements a PI controller (the `pid_kd` global exists for the serial interface but derivative is not used in the current control loop):

```
target = pressure_low_bar or pressure_high_bar (depending on rpm vs pressure_threshold_rpm)
target_units = round(target_bar × 16)          // convert to 1/16 bar units
error = target_units − fps_sixteenth_bar        // 1/16 bar

output = kp × (error / 16) + ki × (integral / 16000)
       = (kp × error × 1000 + ki × integral) / 16000
```

Integral windup prevention: the integral is only updated when the output is not saturated in the direction of the error (the pump cannot actively reduce pressure, so negative output saturation combined with negative error would otherwise wind up the integrand without effect).

```cpp
bool saturated_low  = (output_scaled <= 0        && error < 0);
bool saturated_high = (output_scaled >= 4080000   && error > 0);  // 255 × 16000
if (!saturated_low && !saturated_high) pid_integral = new_integral;
```

Integral is clamped to ±320 000 (equivalent to 20 × 16 000) to prevent extreme windup on startup.

### Target Pressure Selection

Two target pressures and a threshold RPM are stored in EEPROM:

| Variable | EEPROM | Description |
|---|---|---|
| `pressure_low_bar` | addr 52 | Target pressure below `pressure_threshold_rpm` |
| `pressure_high_bar` | addr 56 | Target pressure at or above `pressure_threshold_rpm` |
| `pressure_threshold_rpm` | addr 60 | RPM switchover point |

### Prime Function

`primePump()` sets `pump_pwm = 255`, writes full PWM to the output, and records an end time of `millis() + PRIME_DURATION_MS` (2000 ms). `isPriming()` checks whether the time has elapsed and turns the pump off automatically. The main loop calls `isPriming()` each iteration; the LED blinking logic also calls it so the green LED blinks during priming.

### Pump Modes

| Mode | How to enter | Behaviour |
|---|---|---|
| PID (default) | `CMD_PUMP_MODE` payload 0 | `updatePump()` runs PI loop each main loop iteration |
| Always-on | `CMD_PUMP_MODE` payload 1 | `pump_pwm = 255`; `analogWrite(PIN_PUMP, 255)` each loop |

The always-on mode is intended for bench testing or diagnosing pressure-related issues.

### Safety Shutoff

On CKPS timeout, the main loop calls `disablePump()` (unless `pump_manual` is set). `disablePump()` sets `pump_pwm = 0`, writes 0 to `analogWrite`, and resets the PID integral and priming state.

---

## Constants

| Symbol | Value | Description |
|---|---|---|
| `PIN_PUMP` | 3 | D3 = ATmega PD3, Timer2 OC2B |
| `PRIME_DURATION_MS` | 2000 | Duration of a pump prime cycle (ms) |
