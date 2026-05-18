# Crankshaft Position Sensor (CKPS)

## Overview

The CKPS subsystem measures engine RPM and provides the injection synchronisation trigger. A hall-effect or magnetic sensor produces one falling-edge pulse per crankshaft revolution on pin D8 (Timer1 ICP1). Two ISRs share Timer1: `TIMER1_CAPT_vect` fires on each CKPS edge; `TIMER1_OVF_vect` counts counter overflows so that low-RPM periods longer than one 16-bit wrap (≈ 32 ms at clk/8) are measured correctly.

---

## Firmware

### Files

- `ckps.h` — public interface: pin define, timeout constant, `injection_trigger` extern, four function declarations
- `ckps.cpp` — Timer1 configuration, both ISRs, `getRPM`, `isCKPSTimeout`, `resetCKPS`

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

After the first two pulses, the ISR sets `injection_trigger = true` whenever `rpm < RPM_SYNC_THRESHOLD` (1500 RPM). The main loop clears the flag after processing it. Above the threshold, the ISR does nothing with injection — the 60 Hz scheduler in the main loop takes over.

### Timeout Detection

`isCKPSTimeout()` returns `true` when `millis() - last_pulse_ms > CKPS_TIMEOUT_MS` (500 ms). The main loop calls this every iteration; on timeout it shuts off the injector, resets CKPS state, and (unless `pump_manual` is set) disables the pump.

---

## Constants

| Symbol | Value | Description |
|---|---|---|
| `PIN_CKPS` | 8 | Arduino pin D8 = ATmega PB0 = Timer1 ICP1 |
| `CKPS_TIMEOUT_MS` | 500 | ms of silence before engine is considered stopped |
| `RPM_SYNC_THRESHOLD` | 1500 | RPM below which injection is synchronised to CKPS |
