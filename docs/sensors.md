# Sensors

## Overview

Five ADC channels are read every main loop iteration. TPS and FPS use linear ADC-to-engineering-unit formulas. IAT and ET use NTC thermistor lookup tables with linear interpolation. Battery voltage uses a fixed resistor divider ratio.

---

## Firmware

### Files

- `sensors.h` — pin defines, table size constants, function declarations
- `sensors.cpp` — lookup tables, all `read*()` functions

---

## Throttle Position Sensor (TPS) — A0

Linear voltage sensor: 0 V = closed, 5 V = fully open. Two calibration endpoints (`tps_adc_closed`, `tps_adc_open`) are stored in EEPROM and loaded at startup (defaults: 30 and 730 ADC counts).

```cpp
uint8_t pct = (analogRead(PIN_TPS) - tps_adc_closed) * 100UL / (tps_adc_open - tps_adc_closed);
```

Output clamped to 0–100 %. Values outside the calibrated range are clipped, not extrapolated.

### TPS Calibration

Two serial commands capture the live ADC at the physical endpoints:

| Command | ID | Action |
|---|---|---|
| `CMD_TPS_CAL_CLOSED` | `0x11` | Stores `analogRead(A0)` as `tps_adc_closed` |
| `CMD_TPS_CAL_OPEN`   | `0x12` | Stores `analogRead(A0)` as `tps_adc_open` |

Both save immediately to EEPROM (addr 110–113, magic at 114).

---

## Fuel Pressure Sensor (FPS) — A1

Ratiometric sensor: 0.5 V = 0 bar, 4.5 V = 10 bar.

ADC values: 0.5 V ≈ 102 counts, 4.5 V ≈ 921 counts → span ≈ 819 counts for 10 bar.

```cpp
int32_t v = analogRead(PIN_FPS) - 102;
return clamp(v * 160 / 819, 0, 160);   // units of 1/16 bar (0.0625 bar/count)
```

Return value 0–160 (= 0–10 bar). The global `fps_sixteenth_bar` is this raw value; the PID and sensor packet also use it in these units.

---

## Intake Air Temperature (IAT) — A2

10 kΩ NTC thermistor (β = 3950 K) with a 10 kΩ pull-up to 5 V. The `iat_table` contains 10 `{ADC, °C}` pairs sorted ADC-descending (= temperature ascending), covering −40 °C to +100 °C.

`lookupTemp()` locates the surrounding pair and linearly interpolates:

```cpp
int32_t result = table[i].temp_degc
               + (adc_offset * temp_delta + adc_range / 2) / adc_range;
```

The half-divisor addition rounds to nearest integer rather than truncating.

Return value is `int16_t` in whole °C.

---

## Engine Temperature (ET) — A3

Same NTC circuit and lookup mechanism as IAT. The `et_table` extends to higher temperatures (up to 160 °C) as the cooling system can reach higher steady-state temperatures than intake air.

---

## Battery Voltage — A7

A resistor divider scales the battery voltage to 0–5 V before the ADC. The divider ratio is 3.185:1, so:

```
Vbat = ADC × (5.0 × 3.185 / 1023) ≈ ADC × 0.01558 V
```

Returned as `uint8_t` in units of 1/16 V (0.0625 V per count):

```cpp
return (uint8_t)(analogRead(PIN_BAT) * 255 / 1023);
```

Full scale 255 × 0.0625 = 15.9375 V. The global `bat_v` uses these units; the PC app divides by 16 to display volts.

---

## Pin Summary

| Signal | Pin | ADC channel | Units returned |
|---|---|---|---|
| TPS | A0 | ADC0 | 0–100 % |
| FPS | A1 | ADC1 | 0–160 (1/16 bar per count) |
| IAT | A2 | ADC2 | °C (int16) |
| ET  | A3 | ADC3 | °C (int16) |
| BAT | A7 | ADC7 | 1/16 V per count (uint8) |
