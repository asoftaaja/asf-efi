# PC App — Connection and Serial Worker

## Overview

The connection subsystem has three parts: the `ConnectionPanel` GUI widget that lets the user pick a port and connect; the `SerialWorker` daemon thread that owns the `pyserial` port for its entire lifetime; and the sync-warning mechanism in `MainWindow` that detects when device values differ from the loaded tune file.

---

## Connection Lifecycle

### Connecting

1. User selects a port from the `Combobox` (populated by `serial.tools.list_ports`) and clicks **Connect**.
2. `ConnectionPanel._connect()` creates a `SerialWorker` and sets `ECUState.connected = True` before starting the thread. The thread is a daemon so it is killed automatically if the main process exits.
3. `SerialWorker.run()` opens the serial port, waits 100 ms for the Arduino USB-CDC reset to settle, and flushes the input buffer.
4. The worker then reads five configuration blocks from the ECU **before** entering the poll loop:

| Read | Command | State field populated |
|---|---|---|
| Injection map | `CMD_READ_MAP` | `ECUState.device_map_buf` |
| Axis breakpoints | `CMD_READ_AXIS` | `ECUState.device_rpm_axis_buf`, `device_tps_axis_buf` |
| Pump config | `CMD_READ_PUMP_CONFIG` | `ECUState.pid`, `ECUState.pressure`, `pump_mode_always_on` |
| Temperature corrections | `CMD_READ_CORRECTIONS` | `ECUState.device_iat_corr_buf`, `device_et_corr_buf` |
| Accel pump params | `CMD_READ_ACCEL_PUMP` | `ECUState.accel_pump` |

All five reads are wrapped in `try/except` — a failed read is non-fatal and the panel will show default values. After all reads, `ECUState.config_fresh` is set.

5. `MainWindow._on_connect()` enables all tuning panels and schedules `_check_map_loaded()` 1.5 s later to allow the startup reads to complete before checking for sync mismatches.

### Disconnecting

**User-initiated**: clicking **Disconnect** calls `SerialWorker.stop()` (sets the stop event), clears `ECUState.connected`, and immediately calls `_on_disconnect()` on the main thread.

**Error-initiated**: the worker calls `on_error(msg)` on a port failure or after `MAX_MISS_COUNT` (5) consecutive sensor poll misses. The callback is `ConnectionPanel._handle_error()`, which uses `self.after(0, ...)` to schedule `_show_error()` on the main thread, since tkinter can only be called from the main thread.

In both cases `MainWindow._on_disconnect()` disables the tuning panels, stops any active data log, and dismisses the sync warning bar.

---

## SerialWorker Thread

**File:** `serial_worker.py`

### Constants

| Constant | Value | Description |
|---|---|---|
| `POLL_INTERVAL` | 0.20 s | Time between `CMD_READ_SENSORS` polls |
| `READ_TIMEOUT` | 0.30 s | Deadline for sensor response |
| `WRITE_TIMEOUT` | 1.00 s | Deadline for ACK on write commands |
| `MAX_MISS_COUNT` | 5 | Consecutive misses before error callback |

### Poll loop

Each iteration of the main `while` loop:
1. If `POLL_INTERVAL` has elapsed since the last poll: send `CMD_READ_SENSORS` and read the response. On success, call `ECUState.update_sensors()` and reset the miss counter. On miss, increment the counter and trigger an error when `MAX_MISS_COUNT` is reached.
2. Try to drain **one** item from the command queue (`_cmd_queue.get_nowait()`). If the queue is empty, sleep 10 ms and continue.
3. For a dequeued command: write the packet, wait for a response with `WRITE_TIMEOUT`, and resolve the `concurrent.futures.Future` with `(success, error_or_None)`.

Sensor polling and command queue draining interleave — a write command is processed in the gap between two sensor polls.

### Packet reader

`_read_packet(timeout)` implements a state machine:

1. Read bytes until `0xAA` (start byte) is found or the deadline passes.
2. Read 1 byte for `LEN`.
3. Read `LEN` bytes for CMD + payload.
4. Read 1 byte for CRC.
5. Call `parse_packet()` to validate CRC. Returns `(cmd, payload)` or `None`.

### Command queue

`send_command(cmd, payload)` enqueues `(cmd, payload, Future)` and returns the `Future`. GUI panels call this and then poll `fut.done()` with `widget.after(100, ...)` to avoid blocking the main thread.

---

## Sync Warning

When the device map/axis/corrections loaded at startup differ from the values already in `ECUState` (e.g. from a previously loaded tune file), `MainWindow` shows a gold warning bar:

```
⚠  Device values differ from the loaded tune file.
   [Write all to device]  [Load from device]  [Dismiss]
```

`_buffers_match_state()` compares the `device_*_buf` fields against the live state fields. The check runs once at 1.5 s after connect and again when a tune file is loaded while connected.

**Write all to device** — calls `_write_all_to_device()`, which sends map, axis, PID, pressure, corrections, and accel pump in sequence and updates the device buffers to match, clearing the warning.

**Load from device** — copies the `device_*_buf` values into the live state and refreshes all editor panels, clearing the warning.
