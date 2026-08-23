# ASF EFI Tuner — PC Application Implementation Summary

## Overview

The PC application is a Python/tkinter desktop GUI for real-time monitoring and tuning of the ASF EFI Arduino-based fuel injection controller. It communicates with the ECU over a USB serial connection using a custom binary protocol.

---

## Feature Documentation

Detailed implementation notes for specific features are kept in this `docs/` folder.

| File | Topic |
|---|---|
| [connection.md](connection.md) | Connection lifecycle, SerialWorker startup sequence, error handling, sync warning |
| [serial_protocol.md](serial_protocol.md) | Packet format, CRC, all command IDs with payload layouts, data classes |
| [injection_map.md](injection_map.md) | Map editor grid, live cursor, axis breakpoint editor, read/write flow |
| [tune_file.md](tune_file.md) | JSON tune file format, load/save, auto-load, sync warning integration |
| [data_logging.md](data_logging.md) | CSV data logger, file format, log viewer app |

---

## Quick Start

```bash
cd pc_app
pip install -r requirements.txt
python3 main.py
```

Requires Python 3.6+ and the packages in `requirements.txt` (`pyserial`, `matplotlib`). tkinter is included with standard Python distributions.

---

## File Structure

```
pc_app/
├── main.py                   Entry point
├── protocol.py               Serial protocol: CRC, packet framing, encode/decode
├── data_model.py             Shared in-memory ECU state (thread-safe)
├── serial_worker.py          Background serial I/O thread
├── requirements.txt          pyserial>=3.5
└── gui/
    ├── main_window.py        Root Tk window and tab layout
    ├── connection_panel.py   Port/baud selector and connect button
    ├── sensor_panel.py       Live sensor readouts
    ├── map_editor.py         12×5 injection map table with live cursor
    ├── pid_panel.py          PID coefficient editor
    ├── pressure_panel.py     Fuel pressure target editor
    ├── correction_panel.py   IAT and ET correction multiplier tables
    ├── accel_pump_panel.py   Accelerator pump enrichment editor
    ├── shift_cut_panel.py    Shift cut enable, pulse duration and min RPM editor
    ├── alarm_panel.py        ET and VBAT alarm thresholds (local to the PC app)
    ├── tune_file_panel.py    Tune file load/save controls
    └── pump_panel.py         Pump prime button
```

---

## Serial Protocol

The protocol matches the Arduino firmware (`comms.cpp` / `comms.h`) exactly.

### Packet Format

```
[0xAA] [LEN] [CMD] [DATA...] [CRC8]
```

| Field   | Size    | Description                                    |
|---------|---------|------------------------------------------------|
| `0xAA`  | 1 byte  | Start byte (PKT_START)                         |
| `LEN`   | 1 byte  | Number of bytes that follow: 1 (CMD) + payload |
| `CMD`   | 1 byte  | Command ID                                     |
| `DATA`  | 0–120 B | Payload (command-dependent)                    |
| `CRC8`  | 1 byte  | CRC-8/SMBUS over CMD + DATA bytes              |

### CRC Algorithm

CRC-8/SMBUS: polynomial `0x07`, initial value `0x00`, no reflection, no final XOR. Implemented in `protocol.py:crc8_smbus()` to match the firmware's `calcCRC()` exactly.

### Command Reference

| Command              | ID     | Direction  | Payload Size | Description                               |
|----------------------|--------|------------|-------------|-------------------------------------------|
| `CMD_READ_SENSORS`   | `0x01` | PC → ECU → PC | 0 / 12 B  | Request live sensor data                  |
| `CMD_WRITE_MAP`      | `0x02` | PC → ECU   | 120 B       | Upload 12×5 injection map (uint16 µs)     |
| `CMD_WRITE_PID`      | `0x03` | PC → ECU   | 12 B        | Upload Kp, Ki, Kd (3× float32)            |
| `CMD_WRITE_PRESSURE` | `0x04` | PC → ECU   | 10 B        | Low/high pressure (2× float32) + RPM threshold (uint16) |
| `CMD_PUMP_PRIME`     | `0x05` | PC → ECU   | 0 B         | Trigger 2-second pump prime               |
| `CMD_ACK`            | `0x06` | ECU → PC   | 0 B         | Command accepted                          |
| `CMD_NACK`           | `0x07` | ECU → PC   | 0 B         | Command rejected (bad CRC or unknown)     |
| `CMD_WRITE_IAT_CORR` | `0x08` | PC → ECU   | 40 B        | 10× IAT correction multipliers (float32) |
| `CMD_WRITE_ET_CORR`  | `0x09` | PC → ECU   | 40 B        | 10× ET correction multipliers (float32)  |

All multi-byte values are **big-endian**. Floats are IEEE-754 single precision.

### Sensor Response Payload (12 bytes)

| Bytes  | Type    | Field        | Conversion              |
|--------|---------|--------------|-------------------------|
| 0–1    | uint16  | RPM          | direct (rev/min)        |
| 2–3    | uint16  | TPS raw      | divide by 1000 → 0.0–1.0 |
| 4–5    | uint16  | FPS raw      | divide by 100 → bar     |
| 6–7    | int16   | IAT raw      | divide by 10 → °C       |
| 8–9    | int16   | ET raw       | divide by 10 → °C       |
| 10     | uint8   | Pump active  | 0 = off, 1 = on         |
| 11     | uint8   | Reserved     | —                       |

---

## Architecture

### Threading Model

```
Main Thread (Tk event loop)
│
├── root.after(200 ms) ──► SensorPanel._refresh()
│                              reads ECUState.sensors under lock
│                              updates labels
│                              calls MapEditor.update_cursor()
│
└── Button callbacks ────► serial_worker.send_command()
                               returns concurrent.futures.Future
                               polled via root.after(100 ms) until done
                               updates status label with result

SerialWorker Thread (daemon)
│
├── Polls CMD_READ_SENSORS every 200 ms
├── Drains command queue between polls
├── Updates ECUState.sensors under lock, sets sensor_fresh event
└── On error: schedules on_error callback on main thread via root.after(0, ...)
```

**Invariant**: pyserial is only touched inside `SerialWorker`. All tkinter calls happen only on the main thread. `ECUState` is the shared boundary, protected by `threading.Lock` for the sensor snapshot.

### Module Responsibilities

**`protocol.py`** — pure data layer, no I/O. Contains CRC function, `build_packet()`, `parse_packet()`, encode/decode helpers for each command type, and the `SensorData`, `PIDParams`, `PressureConfig` dataclasses.

**`data_model.py`** — `ECUState` holds all shared state: the latest sensor snapshot (with a `threading.Lock`), the current tuning values (injection map, PID, pressure config, correction tables), and a `threading.Event` that signals when fresh sensor data has arrived.

**`serial_worker.py`** — `SerialWorker(threading.Thread)` owns the `serial.Serial` object. Its `run()` loop polls sensors, drains the command queue, resolves `concurrent.futures.Future` objects, and handles port errors. The `_read_packet()` method implements the firmware's receive state machine in Python (wait for `0xAA` → read LEN → read body → read CRC → validate).

**`gui/main_window.py`** — `MainWindow(tk.Tk)` composes all panels, creates the single `ECUState` instance, manages the notebook tabs, and holds the worker reference that is passed (as a callable) to each panel that needs to send commands.

---

## GUI Layout

```
┌─────────────────────────────────────────────────────┐
│  Connection: [port ▼] [baud] [Refresh] [Connect]    │
├─────────────────────────────────────────────────────┤
│  Live Sensors: RPM  TPS  FPS  IAT  ET  PUMP         │
├─────────────────────────────────────────────────────┤
│  [Injection Map] [PID & Pressure] [Corrections]     │  ← tabs
│                                                     │
│  Tab 1 — 12×5 editable table                        │
│           RPM\TPS  0%   25%  50%  75%  100%         │
│           500    [ ]  [ ]  [ ]  [ ]  [ ]            │
│           1000   [ ]  [ ]  [■] [ ]  [ ]  ← cursor  │
│           ...                                       │
│           [Send Map to ECU]  [Fill All Zeros]       │
│                                                     │
│  Tab 2 — PID | Pressure | Pump                      │
│           Kp/Ki/Kd entries  |  Low/High bar  |  Prime│
│                                                     │
│  Tab 3 — IAT corrections | ET corrections           │
│           index 0–9, float multipliers              │
└─────────────────────────────────────────────────────┘
```

---

## Injection Map Editor

The map is a 12×5 grid (RPM bins × TPS bins). Each cell is an editable `ttk.Entry` widget.

**Bin breakpoints** (must match firmware `injection.cpp`):

| Axis | Bins |
|------|------|
| RPM  | 500, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 10000, 13000, 16000 |
| TPS  | 0%, 25%, 50%, 75%, 100% |

**Live cursor**: the cell nearest to the current RPM and TPS (nearest-neighbour, not interpolated) is highlighted in gold (`#FFD700`). Updated at 5 Hz from the sensor refresh cycle.

**Editing**: click a cell, type a value (0–65535 µs), press Enter or click away to commit. Invalid input is rejected and the last good value is restored. Click "Send Map to ECU" to transmit all 120 bytes; the ECU saves to EEPROM automatically.

---

## Temperature Correction Tables

Both IAT and ET correction tables contain 10 multiplier values (float). These are applied in the firmware as multiplicative factors on top of the base injection pulse width from the map. A value of `1.0` means no correction; `1.2` means 20% more fuel.

The table indices (0–9) map to temperature breakpoints defined in the firmware's `sensors.cpp` lookup tables.

---

## Connection Behaviour

- On connect: `SerialWorker` is created and started; tuning panels are enabled.
- On disconnect / port error: worker is stopped; tuning panels are disabled; sensor display freezes at last values.
- The serial port is opened with a 300 ms read timeout. Up to 5 consecutive sensor poll misses trigger an automatic disconnect with an error message.
- On macOS the Arduino typically appears as `/dev/tty.usbmodem*`. On Windows it appears as `COMx`. The port list is auto-populated using `serial.tools.list_ports`.

---

## Default Values

These match the firmware defaults written to EEPROM on first boot:

| Parameter       | Default                                      |
|-----------------|----------------------------------------------|
| Injection map   | All zeros (requires user programming)        |
| Kp              | 20.0                                         |
| Ki              | 1.0                                          |
| Kd              | 0.5                                          |
| Low pressure    | 2.0 bar                                      |
| High pressure   | 3.0 bar                                      |
| RPM threshold   | 3000 RPM                                     |
| IAT correction  | 1.0 × 10 (no correction)                     |
| ET correction   | 1.0 × 10 (no correction)                     |
