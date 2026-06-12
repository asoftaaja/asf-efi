ASF EFI is a fuel injection controller for a small single-cylinder engine, running on an Arduino Nano (ATmega328P). The firmware reads engine sensors, controls a fuel injector and a PWM-driven fuel pump, and communicates with a Python/tkinter PC tuning application over serial. All tuning parameters are persisted to EEPROM.

## Pin assignments

| Signal | Pin | Notes |
|---|---|---|
| TPS | A0 | 0 V = closed, 5 V = full |
| FPS | A1 | 0.5 V = 0 bar, 4.5 V = 10 bar |
| IAT | A2 | NTC thermistor |
| ET | A3 | NTC thermistor |
| CKPS | D8 | Falling edge, Timer1 ICP1 |
| Injector | D4 | High = open |
| Fuel pump | D3 | PWM output, Timer2 OC2B |
| Green LED | D12 | High = on |
| Red LED | D13 | High = on |

## Coding guidelines

Compiled in Arduino IDE; code is split across multiple `.h`/`.cpp` files.
Doxygen style documentation should be added in each function.
The PC app must be compatible with Python 3.6.

## Feature documentation

Detailed implementation notes for specific features are kept in the `docs/` folder. Consult these before modifying related code.

| File | Topic |
|---|---|
| [docs/accel_pump.md](docs/accel_pump.md) | Accelerator pump enrichment — TPS rate detection, linear decay logic, EEPROM layout (addresses 115–121), serial commands 0x15/0x16, PC app integration |
| [docs/ckps.md](docs/ckps.md) | CKPS signal processing — Timer1 input capture, RPM calculation with overflow handling, pump enable gating, injection_trigger flag, timeout detection |
| [docs/injection.md](docs/injection.md) | Injection control — 10×4 map, bilinear interpolation, Q8.8 temperature corrections, Timer1 COMPA fire/close mechanism, sync vs 60 Hz scheduling modes |
| [docs/pump.md](docs/pump.md) | Fuel pump control — PI pressure regulator, anti-windup, always-on mode, prime function, pump enable gating after 2 CKPS pulses |
| [docs/sensors.md](docs/sensors.md) | Sensor reading — TPS linear + calibration, FPS linear, IAT/ET NTC thermistor lookup, battery voltage divider |
| [docs/serial_protocol.md](docs/serial_protocol.md) | Serial protocol — packet frame format, CRC-8/SMBUS, all command IDs with payload layouts, sensor data packet structure |
| [docs/eeprom_map.md](docs/eeprom_map.md) | EEPROM layout — full address map (122 bytes used), per-section magic byte strategy, save function reference |
| [docs/testing.md](docs/testing.md) | Test framework — Ceedling/Unity setup, mock layer architecture, ISR testability, per-module test coverage |
| [pc_app/docs/implementation_summary.md](pc_app/docs/implementation_summary.md) | PC application — architecture, threading model, GUI layout, module responsibilities, connection behaviour |
| [pc_app/docs/connection.md](pc_app/docs/connection.md) | PC app connection — SerialWorker lifecycle, startup read sequence, error handling, sync warning |
| [pc_app/docs/serial_protocol.md](pc_app/docs/serial_protocol.md) | PC app protocol — packet format, CRC, all command IDs, payload layouts, data classes |
| [pc_app/docs/injection_map.md](pc_app/docs/injection_map.md) | PC app map editor — grid editing, live cursor, axis breakpoint editor, read/write flow |
| [pc_app/docs/tune_file.md](pc_app/docs/tune_file.md) | PC app tune files — JSON format, load/save, auto-load, sync warning integration |
| [pc_app/docs/data_logging.md](pc_app/docs/data_logging.md) | PC app data logging — CSV format, Start/Stop log button, log viewer app |