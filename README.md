# ASF EFI — Single-Cylinder Fuel Injection Controller

An open-loop fuel injection ECU for a small single-cylinder engine, built on an Arduino Nano (ATmega328P). A Python desktop application provides real-time monitoring and map tuning over USB serial.

---

## Hardware

| Signal | Pin | Notes |
|---|---|---|
| TPS | A0 | 0 V = closed, 5 V = WOT |
| FPS | A1 | 0.5 V = 0 bar, 4.5 V = 10 bar |
| IAT | A2 | NTC thermistor with 10 kΩ pull-up |
| ET | A3 | NTC thermistor with 10 kΩ pull-up |
| CKPS | D8 | Falling edge, 1 pulse per revolution (ICP1) |
| Injector | D4 | High = open |
| Fuel pump | D3 | PWM via Timer2 OC2B |
| Green LED | D12 | System status |
| Red LED | D13 | Injector activity |

---

## Firmware Architecture

### Files

| File | Role |
|---|---|
| `asf_efi.ino` | Global state, `setup()`, `loop()` |
| `sensors.h/cpp` | ADC reads — TPS, FPS, IAT, ET |
| `ckps.h/cpp` | Crankshaft position — Timer1 input capture, RPM, trigger flag |
| `injection.h/cpp` | Pulse width calculation and injector firing |
| `pump.h/cpp` | Fuel pump PID controller |
| `eeprom_map.h/cpp` | EEPROM persistence for all tuning data |
| `comms.h/cpp` | Serial packet protocol, command dispatcher |

### Main Loop

Each iteration of `loop()` does the following in order:

1. **Read sensors** — TPS (0–1000 per-mille), FPS (bar), IAT (°C), ET (°C)
2. **CKPS timeout check** — if no crankshaft pulse for 500 ms, shut off injector and pump
3. **Pump control** — run PID controller to maintain target fuel pressure
4. **Injection** — fire injector at the computed pulse width (see below)
5. **LED update** — green/red blink logic
6. **Serial** — process any incoming PC commands

### Injection Timing

The injection mode switches based on RPM:

- **Below `RPM_SYNC_THRESHOLD`** — synchronised to CKPS. The CKPS ISR sets an `injection_trigger` flag on each pulse; the main loop fires the injector once per revolution.
- **At or above `RPM_SYNC_THRESHOLD`** — fixed 60 Hz (one injection every ~16 ms), independent of crank position.

The injector is opened by setting D4 high. It is closed by a **Timer1 output-compare ISR** (OCR1A), which fires at a precise hardware-scheduled time. This means the pulse *duration* is always accurate regardless of main-loop latency.

The pump is not activated until at least two CKPS pulses have been received and a valid RPM reading has been established.

### Pulse Width Calculation

`calculatePulseWidth(rpm, tps, iat_degc, et_degc)` — all integer arithmetic, no floats.

1. **Bilinear interpolation** of the 12×5 injection map at the current (RPM, TPS) operating point, using Q16 fixed-point fractions internally.
2. **IAT correction** — linear interpolation of a 10-element coefficient table (Q8.8 format, 256 = ×1.0) over temperature.
3. **ET correction** — same as IAT.
4. **Apply corrections**: `pw = base_pw × iat_corr >> 8`, then `× et_corr >> 8`.
5. **Clamp** to 0–25 000 µs.

The ATmega328P has no FPU, so keeping all injection math in integers eliminates ~300 µs of software-float overhead per calculation — significant at 16 000 RPM where one revolution takes only 3.75 ms.

### Fixed-Point Representation

| Value | Type | Scale |
|---|---|---|
| TPS | `uint16_t` | 0–1000 per-mille (1000 = 100%) |
| IAT, ET | `int16_t` | Whole °C |
| Correction tables | `uint16_t` | Q8.8 — 256 = ×1.0 |
| TPS axis breakpoints | `uint16_t` | 0–1000 per-mille |
| Injection map values | `uint16_t` | Microseconds directly |

### Fuel Pressure Control

A PID controller runs each loop iteration to maintain target fuel pressure:

- **Error** = target pressure − measured FPS
- **Output** = Kp × error + Ki × ∫ error dt + Kd × Δerror/Δt
- Applied as 0–255 PWM to the pump pin (D3)
- Integral is clamped to ±20 to prevent wind-up
- Target pressure switches between `pressure_low_bar` and `pressure_high_bar` at a configurable RPM threshold
- **Pump mode** — selectable from the PC app: **PID mode** (default, actively regulates pressure) or **always-on mode** (full PWM, no pressure feedback)

### LED Indicators

| LED | Condition | Pattern |
|---|---|---|
| Green | System initialised, engine off | Solid on |
| Green | Pump running or priming | Blink 5 Hz |
| Red | Injector has fired within 500 ms | Blink 5 Hz |

### EEPROM Layout

All tuning data survives power cycles. Magic bytes detect uninitialised or incompatible EEPROM and write safe defaults automatically.

```
Address  Size   Content
      0   120   Injection map (12×5 × uint16, big-endian)
    120    12   PID coefficients (kp, ki, kd as float32)
    132    10   Pressure config (low_bar, high_bar float32 + threshold uint16)
    142    10   IAT correction (5 × uint16 Q8.8)
    152    10   ET correction  (5 × uint16 Q8.8)
    162     1   Magic byte 0xAB
    163    24   RPM axis (12 × uint16)
    187    10   TPS axis (5 × uint16 per-mille)
    197     1   Axis magic byte 0xA8
    198     1   Pump mode (0 = PID, 1 = always-on)
    199     1   Pump mode magic 0xA9
```

Total: 200 bytes of 1024 available.

**Note:** The magic byte changed from 0xA7 to 0xAB when correction tables were reduced from 10 to 5 bins. Firmware flashed after this change will re-initialise EEPROM with safe defaults on first boot; re-upload your tune from the PC app afterwards.

---

## Serial Protocol

Packets are framed as:

```
[0xAA][LEN][CMD][DATA...][CRC8]
```

- **0xAA** — start byte
- **LEN** — number of bytes that follow (CMD + DATA), not including start byte or LEN itself
- **CMD** — command ID
- **DATA** — payload (may be empty)
- **CRC8** — CRC-8/SMBUS (poly 0x07, init 0x00) computed over CMD + DATA

### Commands

| ID | Name | Direction | Payload |
|---|---|---|---|
| 0x01 | `READ_SENSORS` | PC → ECU | none; ECU replies with 12-byte sensor packet |
| 0x02 | `WRITE_MAP` | PC → ECU | 120 bytes (12×5 × uint16 big-endian) |
| 0x03 | `WRITE_PID` | PC → ECU | 12 bytes (kp, ki, kd as float32) |
| 0x04 | `WRITE_PRESSURE` | PC → ECU | 10 bytes (low_bar, high_bar float32 + threshold uint16) |
| 0x05 | `PUMP_PRIME` | PC → ECU | none |
| 0x06 | `ACK` | ECU → PC | none |
| 0x07 | `NACK` | ECU → PC | none (bad CRC or unknown command) |
| 0x08 | `WRITE_IAT_CORR` | PC → ECU | 20 bytes (10 × uint16 Q8.8) |
| 0x09 | `WRITE_ET_CORR` | PC → ECU | 20 bytes (10 × uint16 Q8.8) |
| 0x0A | `READ_MAP` | PC → ECU | none; ECU replies with 120-byte map |
| 0x0B | `WRITE_AXIS` | PC → ECU | 34 bytes (12 × uint16 RPM + 5 × uint16 TPS per-mille) |
| 0x0C | `READ_AXIS` | PC → ECU | none; ECU replies with 34-byte axis |
| 0x0D | `PUMP_SET` | PC → ECU | 1 byte (1 = on, 0 = off) — manual test override |
| 0x0E | `PUMP_MODE` | PC → ECU | 1 byte (0 = PID, 1 = always-on) |
| 0x0F | `READ_PUMP_CONFIG` | PC → ECU | none; ECU replies with 23-byte config (kp/ki/kd float32 + pressure + threshold + mode) |
| 0x10 | `READ_CORRECTIONS` | PC → ECU | none; ECU replies with 20-byte correction tables (5×uint16 IAT + 5×uint16 ET) |

### Sensor Packet Format (16 bytes)

| Bytes | Field | Encoding |
|---|---|---|
| 0–1 | RPM | uint16 big-endian |
| 2–3 | TPS | uint16, per-mille (0–1000) |
| 4–5 | FPS | uint16, bar × 100 |
| 6–7 | IAT | int16, °C × 10 |
| 8–9 | ET | int16, °C × 10 |
| 10 | Pump active | 0 or 1 |
| 11–12 | Battery voltage | uint16, V × 100 (e.g. 1234 = 12.34 V) |
| 13 | Pump duty | uint8, raw PWM (0–255) |
| 14–15 | Injector duty | uint16, per-mille (0–1000) |

---

## PC Application

Located in `pc_app/`. Requires Python 3.10+ and the packages in `requirements.txt`.

```
pip install pyserial
python pc_app/main.py
```

### Architecture

| File | Role |
|---|---|
| `protocol.py` | Packet framing, CRC, encode/decode helpers |
| `data_model.py` | Thread-safe `ECUState` container, event flags |
| `serial_worker.py` | Background thread — polls sensors at 5 Hz, drains command queue |
| `tune_io.py` | JSON tune file load/save |
| `gui/main_window.py` | Root Tk window, wires all panels together |
| `gui/connection_panel.py` | Port selector, connect/disconnect |
| `gui/sensor_panel.py` | Live RPM, TPS, FPS, IAT, ET, pump duty, injector duty, battery voltage readouts |
| `gui/map_editor.py` | 12×5 injection map table + axis editors |
| `gui/pid_panel.py` | PID coefficient editor |
| `gui/pressure_panel.py` | Pressure config editor |
| `gui/correction_panel.py` | IAT and ET correction table editors |
| `gui/pump_panel.py` | Pump prime button and manual on/off toggle |
| `gui/tune_file_panel.py` | Load/save tune files |

### Data Flow

**ECU → PC (continuous):**
The `SerialWorker` thread sends `READ_SENSORS` every 200 ms. The ECU responds with a 12-byte sensor packet. The worker decodes it, updates `ECUState`, and signals `sensor_fresh`. The sensor panel and map editor cursor refresh from this.

**PC → ECU (on demand):**
When the user clicks Send on any panel, the GUI calls `worker.send_command(cmd, payload)`, which returns a `concurrent.futures.Future`. The worker serialises the packet, transmits it, and waits up to 1 second for ACK/NACK. The result is shown in the panel's status label.

On connect, the worker automatically reads the current map, axis, pump config (PID coefficients, pressure settings, pump mode), and correction tables from the ECU to populate all panels.

### Injection Map Editor

The map editor displays a 12×5 grid of pulse width values (µs). Rows are RPM bins, columns are TPS bins.

- **Live cursor** — the cell nearest to the current RPM and TPS is highlighted in gold while the engine runs.
- **Cell editing** — click any cell, type a value, press Enter or click away to commit.
- **Right-click** — fill an entire row or column with a single value.
- **Send Map** — transmits the full 120-byte map to the ECU and saves it to EEPROM.

### Temperature Correction Tables

IAT and ET corrections are multiplier coefficients applied to the base pulse width from the map. Each table has 5 temperature bins with fixed breakpoints:

- **IAT breakpoints**: −20, 0, 20, 40, 70 °C
- **ET breakpoints**: 0, 25, 50, 80, 100 °C

A value of 1.0 means no correction. Values greater than 1.0 enrich; values less than 1.0 lean out. Corrections are transmitted as Q8.8 fixed-point integers (1.0 = 256). `WRITE_IAT_CORR`/`WRITE_ET_CORR` carry 10 bytes each (5 × uint16); `READ_CORRECTIONS` returns all 20 bytes in one response.

### Tune Files

Tune files are JSON documents stored in `pc_app/tunefiles/`. They capture the full ECU state: injection map, axis breakpoints, PID coefficients, pressure config, and correction tables. The last-used file is remembered between sessions.

---

## Building and Flashing

Open the project folder in the Arduino IDE. All `.ino`, `.h`, and `.cpp` files in the root directory are compiled together. Select board **Arduino Nano**, processor **ATmega328P (Old Bootloader)** or **ATmega328P** depending on your board, and upload.

**Note:** After flashing new firmware, EEPROM magic bytes are checked on first boot. If they do not match (e.g. after an upgrade), the firmware writes safe defaults and the previous map is lost. Re-upload your tune from the PC app.
