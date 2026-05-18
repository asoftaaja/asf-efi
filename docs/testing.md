# Test Framework

## Overview

The firmware test suite uses **Ceedling 1.0.1** with **Unity 2.6.1** running on the host machine (macOS, g++). Tests are pure host-side unit tests — no hardware required. The suite covers the four most critical subsystems: CKPS/RPM calculation, injection pulse width calculation, serial communications, and fuel pump pressure control.

Run all tests:

```
ceedling test:all
```

---

## Architecture

### Problem: Arduino C++ on a host test runner

The firmware is written for an ATmega328P and depends on Arduino APIs (`millis`, `analogWrite`, `Serial`), AVR hardware registers (`TCNT1`, `PORTD`, `TIMSK1`, etc.), and AVR ISR macros (`ISR(vec)`). None of these exist on a Linux/macOS host.

The solution is a mock header layer in `test/support/` that shadows the real AVR/Arduino headers. Ceedling adds `test/support/` to the include path before any system paths, so firmware source files pick up the mocks automatically without any `#ifdef` guards in the production code.

### C++ compilation of Unity runners

Ceedling generates test runner files as `.c` (plain C). The firmware is C++ and its symbols have C++ name mangling. Running a plain-C runner that calls C++ test functions would fail to link.

The fix is the `-x c++` compiler flag in `project.yml`, which forces every translation unit — including generated runners and `unity.c` — to compile as C++. Unity 2.6.1 wraps its implementation in `extern "C"` guards (`unity.c` lines 17-18 and 695), so it remains link-compatible under this scheme.

---

## File Layout

```
project.yml              Ceedling configuration
test/
  test_ckps.cpp          CKPS / RPM tests
  test_injection.cpp     Injection pulse width tests
  test_comms.cpp         Serial protocol tests
  test_pump.cpp          Fuel pump PI controller tests
  support/
    Arduino.h            millis(), analogWrite(), Serial mock
    EEPROM.h             EEPROM.get/put/read/write mock
    test_globals.cpp     All shared globals (always linked)
    avr/
      io.h               Hardware registers as extern volatile globals
      interrupt.h        ISR() macro -> void function, cli/sei no-ops
      pgmspace.h         PROGMEM/PSTR/pgm_read_* pass-throughs
```

### test_globals.cpp

This file lives in `test/support/` so Ceedling links it into every test executable. It defines:

- All `.ino`-level globals (`tps`, `fps_sixteenth_bar`, `iat_degc`, `et_degc`, `bat_v`, `rpm`, `pump_active`, `pump_manual`, `pump_mode_always_on`)
- Injection map and correction tables (`inj_map`, `iat_correction`, `et_correction`)
- PID tuning and pressure globals (`pid_kp`, `pid_ki`, `pid_kd`, `pressure_low_bar`, `pressure_high_bar`, `pressure_threshold_rpm`)
- All AVR hardware register instances (`TCNT1`, `ICR1`, `OCR1A`, `TCCR1A`, `TCCR1B`, `TIFR1`, `TIMSK1`, `PORTD`, `SREG_reg`)
- Mock state (`mock_millis_val`, `mock_analog_write_val`, `mock_analog_write_pin`, `mock_analog_read_val`)
- `HardwareSerial Serial` and `EEPROMClass EEPROM` instances

Per-module globals that belong to a specific `.cpp` file are **not** defined here (e.g., `pump_pwm` is in `pump.cpp`, `rpm_axis[]`/`tps_axis[]` are in `injection.cpp`).

### ISR testability

The AVR `ISR(vec)` macro normally expands to a special interrupt vector function. The mock header redefines it as:

```c
#define ISR(vec) void vec(void)
```

Each ISR body becomes a plain function that the test can call directly:

```cpp
void TIMER1_CAPT_vect(void);  // forward-declared in test
TIMER1_CAPT_vect();           // called from test like any function
```

---

## Test Modules

### test_ckps.cpp — CKPS / RPM (16 tests)

Covers `ISR(TIMER1_CAPT_vect)` and `ISR(TIMER1_OVF_vect)` from `ckps.cpp`.

| Test | What it checks |
|---|---|
| RPM at 6000 | Period 10 ticks/us, no overflow: `ICR1 = 10 000` |
| RPM with 1 overflow | `ICR1 = 1000` + 1 OVF: `1 000 RPM` |
| RPM with 2 overflows | `ICR1 = 1000` + 2 OVF: `~888 RPM` |
| Race condition (TOV1 set, ICR1 < 0x8000) | Extra overflow counted correctly |
| No false race (TOV1 clear) | Overflow not double-counted |
| RPM capped at 20 000 | Implausibly short period rejected |
| Pump gated after 2 pulses | `pump_active` false after 1 pulse, true after 2 |
| injection_trigger set below 1500 RPM | `~1333 RPM` -> trigger set |
| injection_trigger not set above 1500 RPM | `6000 RPM` -> trigger clear |
| injection_trigger not set at exactly 1500 RPM | Boundary: trigger clear |
| injection_trigger not set during startup | First two pulses are discarded |
| Timeout true/false/exact boundary | `hasRPMTimeout()` at 100/99/100 ms |
| resetCKPS | Clears trigger, re-arms pump gate |

### test_injection.cpp — Injection Pulse Width (20 tests)

Covers `calculatePulseWidth()`, `fireInjector()`, `shutoffInjector()`, and `ISR(TIMER1_COMPA_vect)` from `injection.cpp`.

| Test | What it checks |
|---|---|
| Zero map -> 0 us | All-zero map returns 0 |
| Flat map, neutral corrections | `map=10` -> `1000 us` |
| Bilinear: exact corners | Lower-left and lower-right corners match exactly |
| Bilinear: RPM midpoint | `RPM=2500` between `1000`/`4000`: interpolated correctly |
| Bilinear: TPS midpoint | `TPS=15` between `0`/`30`: interpolated correctly |
| Bilinear: 2x2 block center | Full bilinear with different corner values |
| RPM below axis min | Clamps to first column |
| RPM above axis max | Clamps to last column |
| TPS above axis max | Clamps to last TPS column |
| IAT correction x2.0 | Q8.8 value `512` doubles pulse width |
| ET correction x0.5 | Q8.8 value `128` halves pulse width |
| Both corrections combined | Multiplicative: x2.0 x x0.5 = x1.0 |
| Correction interpolation at midpoint | `-10 degC` between `-20`/`0` -> `1500 us` |
| MAX_PULSE_US cap | Output clamped at `25 000 us` |
| fireInjector: PORTD bit and OCR1A | Injector on, `OCR1A = TCNT1 + ticks`, `OCIE1A` set |
| fireInjector: zero pulse is no-op | Injector stays off |
| fireInjector: tick clamping | `33 000 us x2 = 66 000` -> clamped to `65 000` |
| shutoffInjector | `PD4` cleared, `OCIE1A` disabled |
| COMPA ISR closes injector | ISR call clears `PD4` and disables `OCIE1A` |

### test_comms.cpp — Serial Protocol (28 tests)

Covers `processSerial()` and `sendSensorData()` from `comms.cpp`. Does not link `injection.cpp` or `pump.cpp` — constants (`RPM_BINS`, etc.) are defined locally and dependent functions are stubbed inline.

| Test area | What it checks |
|---|---|
| CRC validation | Correct CRC accepted; wrong CRC rejected |
| FSM framing | Header byte `0xAA` required; bytes before header discarded |
| Multiple packets | Two back-to-back valid packets both processed |
| CMD_GET_SENSOR_DATA | `sendSensorData()` response format and CRC |
| CMD_SET_INJ_MAP | `inj_map` updated from payload |
| CMD_GET_INJ_MAP | Map bytes transmitted correctly |
| CMD_SET_PRESSURE | `pressure_low_bar`/`pressure_high_bar` updated |
| CMD_GET_PRESSURE | Pressure values echoed back |
| CMD_SET_PID | `pid_kp`/`pid_ki`/`pid_kd` updated |
| CMD_GET_PID | PID values echoed back |
| CMD_PUMP_PRIME | `primePump()` called |
| CMD_PUMP_SET | `pump_manual` flag set/cleared |
| CMD_PUMP_MODE | `pump_mode_always_on` flag set |
| CMD_SET_TPS_CAL | `tps_adc_closed`/`tps_adc_open` updated |
| CMD_GET_TPS_CAL | Calibration values echoed back |
| CMD_SET_IAT_CORR | `iat_correction[]` updated |
| CMD_GET_IAT_CORR | Correction array transmitted |
| CMD_SET_ET_CORR | `et_correction[]` updated |
| CMD_GET_ET_CORR | Correction array transmitted |
| CMD_SET_ACCEL | Accel pump parameters updated |
| CMD_GET_ACCEL | Accel pump parameters echoed back |
| Bad CRC | Packet silently discarded, no state change |

### test_pump.cpp — Fuel Pump PI Controller (14 tests)

Covers `updatePump()`, `disablePump()`, `primePump()`, and `isPriming()` from `pump.cpp`.

The `pid_prev_ms` timestamp is a static local inside `pump.cpp` and cannot be reset directly. The `setUp` function works around this by calling `updatePump(48, 0)` with `fps = target = 48` (= 3.0 bar x 16), giving zero error. With error = 0 the integral accumulates nothing (`0 * dt = 0`) regardless of any stale `dt` left from a previous test.

| Test | What it checks |
|---|---|
| Proportional basic | `kp=16`, `error=16` -> `pwm=16` |
| Zero error | `fps = target` -> `pwm = 0` |
| High RPM selects high target | `rpm > threshold` -> `pressure_high_bar` used |
| Output clamped at 255 | Huge error -> `pwm = 255` |
| Output clamped at 0 (overpressure) | Negative error -> `pwm = 0` |
| Integral accumulates | Two consecutive 1000 ms calls: pwm goes 16, 32 |
| Anti-windup at high saturation | Saturated integral does not grow further |
| Anti-windup at low saturation | Negative integral does not wind negative |
| disablePump zeroes PWM | `pump_pwm = 0`, `analogWrite = 0` |
| disablePump resets integral | 1 ms after reset, integral-only output is 0 |
| primePump sets full PWM | `pump_pwm = 255`, `analogWrite = 255` |
| isPriming true during prime | Returns true within `PRIME_DURATION_MS` |
| isPriming false after duration | Returns false at `PRIME_DURATION_MS + 1` |
| isPriming false at exact boundary | Returns false at `PRIME_DURATION_MS` |
| disablePump cancels prime | `isPriming()` returns false immediately |

---

## Adding New Tests

1. Create `test/test_<module>.cpp` in the `test/` directory.
2. Include the module header (`#include "module.h"`). Ceedling auto-links `module.cpp` based on this include.
3. Include only headers whose corresponding `.cpp` files you want linked. Pulling in an extra header auto-links its `.cpp`, which may cause duplicate symbol errors if `test_globals.cpp` already defines those symbols.
4. Declare ISR functions as `void VECTOR_vect(void);` before calling them.
5. All globals shared across modules are already in `test_globals.cpp` and available via `extern`.
