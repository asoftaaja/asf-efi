# PC App — Serial Protocol

## Overview

The PC app communicates with the ECU over a USB-CDC serial link at 115200 baud. All traffic uses a binary packet format with a CRC-8/SMBUS integrity check. The protocol is implemented in `protocol.py` and must match `comms.cpp` on the Arduino exactly.

---

## Packet Format

```
[0xAA] [LEN] [CMD] [DATA...] [CRC8]
```

| Field  | Size   | Description |
|--------|--------|-------------|
| `0xAA` | 1 byte | Start byte (`PKT_START`) |
| `LEN`  | 1 byte | Number of bytes that follow: 1 (CMD) + len(payload) |
| `CMD`  | 1 byte | Command ID |
| `DATA` | 0–N B  | Payload, command-dependent |
| `CRC8` | 1 byte | CRC-8/SMBUS over CMD + DATA bytes |

All multi-byte integers are **big-endian**. Floats are **IEEE-754 single precision** big-endian.

`build_packet(cmd, payload)` assembles the full byte string. `parse_packet(body)` validates the CRC and returns `(cmd, payload)` or `None`.

---

## CRC Algorithm

CRC-8/SMBUS: polynomial `0x07`, initial value `0x00`, no input/output reflection, no final XOR. Implemented in `crc8_smbus()` to match the firmware's `calcCRC()` exactly.

---

## Command Reference

| Command | ID | Direction | Payload | Response |
|---|---|---|---|---|
| `CMD_READ_SENSORS`    | `0x01` | PC → ECU | none | 16-byte sensor data (see below) |
| `CMD_WRITE_MAP`       | `0x02` | PC → ECU | 40 bytes (10×4 uint8) | ACK/NACK |
| `CMD_WRITE_PID`       | `0x03` | PC → ECU | 12 bytes (3× float32) | ACK/NACK |
| `CMD_WRITE_PRESSURE`  | `0x04` | PC → ECU | 10 bytes (2× float32 + uint16) | ACK/NACK |
| `CMD_PUMP_PRIME`      | `0x05` | PC → ECU | none | ACK/NACK |
| `CMD_ACK`             | `0x06` | ECU → PC | none | — |
| `CMD_NACK`            | `0x07` | ECU → PC | none | — |
| `CMD_WRITE_IAT_CORR`  | `0x08` | PC → ECU | 10 bytes (5× uint16 Q8.8) | ACK/NACK |
| `CMD_WRITE_ET_CORR`   | `0x09` | PC → ECU | 10 bytes (5× uint16 Q8.8) | ACK/NACK |
| `CMD_READ_MAP`        | `0x0A` | PC → ECU | none | 40-byte map |
| `CMD_WRITE_AXIS`      | `0x0B` | PC → ECU | 24 bytes (10× uint16 RPM + 4× uint8 TPS%) | ACK/NACK |
| `CMD_READ_AXIS`       | `0x0C` | PC → ECU | none | 24-byte axis |
| `CMD_PUMP_SET`        | `0x0D` | PC → ECU | 1 byte (0=off, 1=on) | ACK/NACK |
| `CMD_PUMP_MODE`       | `0x0E` | PC → ECU | 1 byte (0=PID, 1=always-on) | ACK/NACK |
| `CMD_READ_PUMP_CONFIG`| `0x0F` | PC → ECU | none | 23-byte pump config |
| `CMD_READ_CORRECTIONS`| `0x10` | PC → ECU | none | 20-byte correction tables |
| `CMD_TPS_CAL_CLOSED`  | `0x11` | PC → ECU | none | ACK/NACK |
| `CMD_TPS_CAL_OPEN`    | `0x12` | PC → ECU | none | ACK/NACK |
| `CMD_WRITE_ACCEL_PUMP`| `0x15` | PC → ECU | 6 bytes (3× uint16) | ACK/NACK |
| `CMD_READ_ACCEL_PUMP` | `0x16` | PC → ECU | none | 6-byte accel pump params |

---

## Payload Layouts

### Sensor response — `CMD_READ_SENSORS` (16 bytes)

Decoded by `decode_sensor_data()` into a `SensorData` object.

| Bytes | Type    | Field                  | Conversion |
|-------|---------|------------------------|------------|
| 0–1   | uint16  | RPM                    | direct (rev/min) |
| 2     | uint8   | TPS raw                | ÷ 100 → 0.0–1.0 |
| 3     | uint8   | FPS raw                | ÷ 16 → bar |
| 4–5   | int16   | IAT raw                | ÷ 10 → °C |
| 6–7   | int16   | ET raw                 | ÷ 10 → °C |
| 8     | uint8   | pump_active            | 0/1 |
| 9     | uint8   | battery raw            | ÷ 16 → V |
| 10    | uint8   | pump PWM               | raw 0–255 |
| 11–12 | uint16  | injector duty          | ÷ 10 → % |
| 13    | uint8   | accel_active           | 0/1 |
| 14–15 | uint16  | injector open duration | direct → µs (0–25 000) |

### Injection map — `CMD_WRITE_MAP` / `CMD_READ_MAP` (40 bytes)

10×4 array of uint8 values, row-major (RPM axis outer, TPS axis inner). 1 unit = 100 µs. Packed and unpacked by `encode_map()` / `decode_map()`.

### Axis breakpoints — `CMD_WRITE_AXIS` / `CMD_READ_AXIS` (24 bytes)

10 × uint16 big-endian RPM values followed by 4 × uint8 TPS percentages (0–100). Packed by `encode_axis(rpm_pts, tps_pts)` where `tps_pts` is a list of 0.0–1.0 floats (multiplied by 100 before packing).

### PID parameters — `CMD_WRITE_PID` (12 bytes)

3 × float32 big-endian: Kp, Ki, Kd. Packed by `encode_pid(PIDParams)`.

### Pressure config — `CMD_WRITE_PRESSURE` (10 bytes)

2 × float32 (low_bar, high_bar) + 1 × uint16 (threshold_rpm). Packed by `encode_pressure(PressureConfig)`.

### Temperature corrections — `CMD_WRITE_IAT_CORR` / `CMD_WRITE_ET_CORR` (10 bytes each)

5 × uint16 big-endian Q8.8 fixed-point values. 256 = 1.0 (no correction). Packed by `encode_corrections(values)` where `values` is a list of Python floats.

### Pump config response — `CMD_READ_PUMP_CONFIG` (23 bytes)

| Bytes | Type    | Field |
|-------|---------|-------|
| 0–11  | 3×float32 | Kp, Ki, Kd |
| 12–19 | 2×float32 | low_bar, high_bar |
| 20–21 | uint16  | threshold_rpm |
| 22    | uint8   | pump_mode_always_on (0/1) |

Unpacked by `decode_pump_config()` into `(PIDParams, PressureConfig, bool)`.

### Accelerator pump — `CMD_WRITE_ACCEL_PUMP` / `CMD_READ_ACCEL_PUMP` (6 bytes)

3 × uint16 big-endian: threshold_pct_per_s, extra_us, duration_ms. Packed/unpacked by `encode_accel_pump()` / `decode_accel_pump()`.

---

## Data Classes

Defined in `protocol.py`:

| Class | Fields |
|---|---|
| `SensorData` | rpm, tps (0–1), fps_bar, iat_degc, et_degc, pump_active, bat_v, pump_duty, inj_duty, accel_active, inj_open_us |
| `PIDParams` | kp, ki, kd |
| `PressureConfig` | low_bar, high_bar, threshold_rpm |
| `AccelPumpParams` | threshold_pct_per_s, extra_us, duration_ms |
