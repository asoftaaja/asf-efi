# Serial Protocol

## Overview

All communication between the PC and the ECU uses a framed binary packet protocol over UART at 115 200 baud. Each packet is protected by a CRC-8/SMBUS checksum. The PC always initiates requests; the ECU responds.

---

## Packet Format

```
[0xAA] [LEN] [CMD] [DATA...] [CRC8]
```

| Field | Size | Description |
|---|---|---|
| Start byte | 1 | Always `0xAA` |
| LEN | 1 | Number of bytes that follow: `1 (CMD) + len(DATA)`. Does not include the start byte, LEN byte, or CRC byte. |
| CMD | 1 | Command ID (see table below) |
| DATA | 0–N | Payload bytes, command-dependent |
| CRC8 | 1 | CRC-8/SMBUS over CMD + DATA bytes (poly 0x07, init 0x00, no reflection) |

The receive state machine in `comms.cpp` (`processSerial()`) is a four-state FSM: `RX_IDLE → RX_LEN → RX_DATA → RX_CRC`. On a bad CRC the ECU sends NACK and resets to idle.

---

## Command Reference

### General

| ID | Name | Direction | Payload (bytes) | Description |
|---|---|---|---|---|
| `0x06` | `CMD_ACK` | ECU → PC | 0 | Command accepted |
| `0x07` | `CMD_NACK` | ECU → PC | 0 | Rejected (bad CRC, unknown command, wrong payload length) |

### Sensor Data

| ID | Name | Direction | Payload |
|---|---|---|---|
| `0x01` | `CMD_READ_SENSORS` | PC → ECU (request) | 0 |
| `0x01` | `CMD_READ_SENSORS` | ECU → PC (response) | 16 bytes — see layout below |

**Sensor data response layout (16 bytes):**

| Offset | Size | Field | Units |
|---|---|---|---|
| 0–1 | uint16 BE | RPM | rev/min |
| 2 | uint8 | TPS | 0–100 % |
| 3 | uint8 | FPS | 1/16 bar per count (0–160) |
| 4–5 | int16 BE | IAT × 10 | 0.1 °C per count |
| 6–7 | int16 BE | ET × 10 | 0.1 °C per count |
| 8 | uint8 | pump active | 0 or 1 |
| 9 | uint8 | battery voltage | 1/16 V per count |
| 10 | uint8 | pump PWM | 0–255 raw |
| 11–12 | uint16 BE | injector duty | per-mille (0–1000) |
| 13 | uint8 | accel pump active | 0 or 1 |
| 14–15 | uint16 BE | injector open duration | µs (0–25 000) |

Injector duty is computed as `last_pulse_width_us × 1000 / period_us`, where `period_us` is either 16 667 µs (60 Hz) or `60 000 000 / rpm` (sync mode). Injector open duration is `last_pulse_width_us` transmitted directly.

### Injection Map

| ID | Name | Direction | Payload |
|---|---|---|---|
| `0x0A` | `CMD_READ_MAP` | PC → ECU (request) | 0 |
| `0x0A` | `CMD_READ_MAP` | ECU → PC (response) | 40 bytes: `RPM_BINS × TPS_BINS` uint8 values, row-major (RPM outer) |
| `0x02` | `CMD_WRITE_MAP` | PC → ECU | 40 bytes same layout; ECU saves to EEPROM, responds ACK |

Map unit: 1 count = 100 µs of injector open time.

### Axis Breakpoints

| ID | Name | Direction | Payload |
|---|---|---|---|
| `0x0C` | `CMD_READ_AXIS` | PC → ECU (request) | 0 |
| `0x0C` | `CMD_READ_AXIS` | ECU → PC (response) | 24 bytes: 10 × uint16 BE RPM + 4 × uint8 TPS percent |
| `0x0B` | `CMD_WRITE_AXIS` | PC → ECU | 24 bytes same layout; ECU saves, responds ACK |

### Temperature Corrections

| ID | Name | Direction | Payload |
|---|---|---|---|
| `0x10` | `CMD_READ_CORRECTIONS` | PC → ECU (request) | 0 |
| `0x10` | `CMD_READ_CORRECTIONS` | ECU → PC (response) | 20 bytes: 5 × uint16 BE IAT Q8.8 + 5 × uint16 BE ET Q8.8 |
| `0x08` | `CMD_WRITE_IAT_CORR` | PC → ECU | 10 bytes: 5 × uint16 BE Q8.8; ECU saves, responds ACK |
| `0x09` | `CMD_WRITE_ET_CORR` | PC → ECU | 10 bytes: 5 × uint16 BE Q8.8; ECU saves, responds ACK |

Q8.8 encoding: 256 = 1.0 (no correction).

### Pump & PID Configuration

| ID | Name | Direction | Payload |
|---|---|---|---|
| `0x0F` | `CMD_READ_PUMP_CONFIG` | PC → ECU (request) | 0 |
| `0x0F` | `CMD_READ_PUMP_CONFIG` | ECU → PC (response) | 23 bytes — see layout below |
| `0x03` | `CMD_WRITE_PID` | PC → ECU | 12 bytes: kp, ki, kd as float32 BE; ECU saves, responds ACK |
| `0x04` | `CMD_WRITE_PRESSURE` | PC → ECU | 10 bytes: low_bar, high_bar as float32 BE + threshold RPM as uint16 BE; ECU saves, responds ACK |
| `0x05` | `CMD_PUMP_PRIME` | PC → ECU | 0; ECU runs pump at full PWM for 2 s, responds ACK |
| `0x0D` | `CMD_PUMP_SET` | PC → ECU | 1 byte: 1=manual on, 0=off; ECU responds ACK |
| `0x0E` | `CMD_PUMP_MODE` | PC → ECU | 1 byte: 0=PID, 1=always-on; ECU saves, responds ACK |

**Pump config response layout (23 bytes):**

| Offset | Size | Field |
|---|---|---|
| 0–3 | float32 BE | `pid_kp` |
| 4–7 | float32 BE | `pid_ki` |
| 8–11 | float32 BE | `pid_kd` |
| 12–15 | float32 BE | `pressure_low_bar` |
| 16–19 | float32 BE | `pressure_high_bar` |
| 20–21 | uint16 BE | `pressure_threshold_rpm` |
| 22 | uint8 | pump mode (0=PID, 1=always-on) |

### TPS Calibration

| ID | Name | Direction | Payload |
|---|---|---|---|
| `0x11` | `CMD_TPS_CAL_CLOSED` | PC → ECU | 0; ECU captures live ADC as closed position, saves, responds ACK |
| `0x12` | `CMD_TPS_CAL_OPEN` | PC → ECU | 0; ECU captures live ADC as open position, saves, responds ACK |

### Accelerator Pump

| ID | Name | Direction | Payload |
|---|---|---|---|
| `0x16` | `CMD_READ_ACCEL_PUMP` | PC → ECU (request) | 0 |
| `0x16` | `CMD_READ_ACCEL_PUMP` | ECU → PC (response) | 6 bytes — see layout below |
| `0x15` | `CMD_WRITE_ACCEL_PUMP` | PC → ECU | 6 bytes same layout; ECU saves, responds ACK |

**Accel pump layout (6 bytes):** threshold (uint16 BE), extra_us (uint16 BE), duration_ms (uint16 BE). See [accel_pump.md](accel_pump.md).

---

## CRC Algorithm

CRC-8/SMBUS: polynomial 0x07, initial value 0x00, no input/output reflection. Computed over CMD + DATA bytes only (not the start byte, LEN, or CRC byte itself).

```cpp
uint8_t crc = 0x00;
for each byte b:
    crc ^= b;
    for 8 bits:
        crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
```
