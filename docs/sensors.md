# Sensors

## Overview

Five ADC channels are read every main loop iteration. TPS and FPS use linear ADC-to-engineering-unit formulas. IAT and ET use NTC thermistor lookup tables with linear interpolation. Battery voltage uses a fixed resistor divider ratio. The crankshaft position sensor is a digital input on Timer1 input capture: it measures RPM, gates the fuel pump and triggers synchronised injection.

---

## Firmware

### Files

- `sensors.h` — pin defines, table size constants, function declarations
- `sensors.cpp` — lookup tables, all `read*()` functions
- `ckps.h` — CKPS pin define, timeout constant, `injection_trigger` extern, function declarations
- `ckps.cpp` — Timer1 configuration, capture and overflow ISRs, `getRPM`, `isCKPSTimeout`, `resetCKPS`, `getCrankRevs`

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

## Crankshaft Position Sensor (CKPS) — D8

The CKPS subsystem measures engine RPM and provides the injection synchronisation trigger. A hall-effect or magnetic sensor produces one falling-edge pulse per crankshaft revolution on pin D8 (Timer1 ICP1). Two ISRs share Timer1: `TIMER1_CAPT_vect` fires on each CKPS edge; `TIMER1_OVF_vect` counts counter overflows so that low-RPM periods longer than one 16-bit wrap (≈ 32 ms at clk/8) are measured correctly.

### Timer1 Configuration

Configured in `initCKPS()`:

| Bit field | Value | Meaning |
|---|---|---|
| `ICNC1` | 1 | Noise canceller — requires four consecutive equal samples |
| `ICES1` | 0 | Capture on falling edge |
| `CS11` | 1 | Prescaler clk/8 → 0.5 µs per tick at 16 MHz |
| `ICIE1` | 1 | Input capture interrupt enable |
| `TOIE1` | 1 | Overflow interrupt enable |

The same Timer1 is shared with the injector close interrupt (`OCIE1A`). This is safe because the injector close is scheduled relative to `TCNT1` at fire time; overflow counting and RPM capture are independent of compare-A.

### RPM Calculation

Each capture ISR reads `ICR1` and the accumulated overflow count to build a 32-bit period:

```cpp
int32_t  signed_diff  = (int32_t)capture - (int32_t)prev_capture;
uint32_t period_ticks = (uint32_t)((int32_t)ovf * 65536L + signed_diff);
rpm = 120000000UL / period_ticks;   // = 60 s × 2 000 000 ticks/s / period_ticks
```

The signed subtraction handles the case where the 16-bit counter wraps between two captures without double-counting the wrap (the overflow count already covers the full 65536-tick increment).

Race condition fix: if the overflow interrupt flag (`TOV1`) is set but `TIMER1_OVF_vect` has not yet executed, and the capture value is small (i.e. the capture happened just after the overflow), the overflow is counted immediately inside the capture ISR:

```cpp
if ((TIFR1 & (1 << TOV1)) && capture < 0x8000) ovf++;
```

### Pump Enable Gating

A `pulse_count` variable tracks the number of CKPS pulses since startup (or since the last `resetCKPS()`). The pump is only allowed to run once two valid pulses have been received and a reliable RPM figure is available:

```cpp
if (pulse_count < 2) {
    pulse_count++;
    if (pulse_count == 2) pump_active = true;
    return;   // skip injection trigger on startup pulses
}
```

### Injection Trigger

After the first two pulses, the ISR sets `injection_trigger = true` whenever `rpm < RPM_SYNC_THRESHOLD`. The main loop clears the flag after processing it. Above the threshold, the ISR does nothing with injection — the 60 Hz scheduler in the main loop takes over.

### Revolution Counter

`ckps.cpp` also keeps a free-running `uint8_t crank_revs` counter, incremented on every CKPS edge (including the two startup pulses) and exposed by `getCrankRevs()`. `resetCKPS()` does not clear it. It drives the powerband ramp — see [injection.md — Revolution counting](injection.md#revolution-counting).

### Timeout Detection

`isCKPSTimeout()` returns `true` when `millis() - last_pulse_ms > CKPS_TIMEOUT_MS` (500 ms). The main loop calls this every iteration; on timeout it shuts off the injector, resets CKPS state, and (unless `pump_manual` is set) disables the pump.

### Constants

| Symbol | Value | Description |
|---|---|---|
| `PIN_CKPS` | 8 | Arduino pin D8 = ATmega PB0 = Timer1 ICP1 |
| `CKPS_TIMEOUT_MS` | 500 | ms of silence before engine is considered stopped |
| `RPM_SYNC_THRESHOLD` | see `injection.h` | RPM below which injection is synchronised to CKPS |

---

## Pin Summary

| Signal | Pin | ADC channel | Units returned |
|---|---|---|---|
| TPS | A0 | ADC0 | 0–100 % |
| FPS | A1 | ADC1 | 0–160 (1/16 bar per count) |
| IAT | A2 | ADC2 | °C (int16) |
| ET  | A3 | ADC3 | °C (int16) |
| BAT | A7 | ADC7 | 1/16 V per count (uint8) |
| CKPS | D8 | — (Timer1 ICP1) | RPM |
