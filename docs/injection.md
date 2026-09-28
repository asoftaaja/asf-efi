# Injection Control

## Overview

Injection pulse width is determined by a 10×4 (RPM × TPS) lookup map with bilinear interpolation, then multiplied by two temperature correction coefficients (IAT and ET). The injector is opened by asserting D4 high and closed by a Timer1 compare-A ISR that fires after the calculated pulse width. Two scheduling modes exist: synchronised (one shot per CKPS pulse, below `RPM_SYNC_THRESHOLD`) and fixed-frequency (60 Hz, at or above `RPM_SYNC_THRESHOLD`).

---

## Firmware

### Files

- `injection.h` — pin defines, map dimension constants, `MAX_PULSE_US`, `RPM_SYNC_THRESHOLD`, extern timestamps, function declarations
- `injection.cpp` — axis breakpoints, bilinear interpolation, correction interpolation, `fireInjector`, `shutoffInjector`, Timer1 COMPA ISR

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

The order is deliberate: the powerband multiplier scales the mapped and temperature-corrected base only, while the accelerator-pump shot is added at full value afterwards — a transient enrichment should not be leaned out by the low-load multiplier. See [powerband.md](powerband.md) and [accel_pump.md](accel_pump.md) for details.

---

## Constants

| Symbol | Value | Description |
|---|---|---|
| `PIN_INJECTOR` | 4 | D4 = ATmega PD4; high = injector open |
| `MAX_PULSE_US` | 25 000 | Hard ceiling on injector pulse width (µs) |
| `RPM_SYNC_THRESHOLD` | 1 500 | RPM switchover between sync and 60 Hz mode |
