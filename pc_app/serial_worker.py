"""
Background daemon thread that owns the pyserial port.
- Polls CMD_READ_SENSORS at ~5 Hz.
- Drains a command queue between polls.
- Resolves concurrent.futures.Future objects for write commands.
"""

import threading
import queue
import time
import concurrent.futures
from typing import Callable, List, Optional, Tuple

import serial
import serial.tools.list_ports

from protocol import (
    PKT_START, CMD_READ_SENSORS, CMD_READ_MAP, CMD_READ_AXIS,
    CMD_READ_PUMP_CONFIG, CMD_READ_CORRECTIONS, CMD_READ_ACCEL_PUMP,
    CMD_ACK, CMD_NACK,
    build_packet, parse_packet, decode_sensor_data, decode_map, decode_axis,
    decode_pump_config, decode_corrections, decode_accel_pump, SERIAL_BAUD,
)
from data_model import ECUState


POLL_INTERVAL  = 0.20   # seconds between sensor polls
READ_TIMEOUT   = 0.30   # seconds to wait for sensor response
WRITE_TIMEOUT  = 1.00   # seconds to wait for ACK on write commands
MAX_MISS_COUNT = 5      # consecutive misses before error callback


def list_ports() -> List[str]:
    """Return available serial port names."""
    return [p.device for p in serial.tools.list_ports.comports()]


class SerialWorker(threading.Thread):
    def __init__(
        self,
        port: str,
        baud: int,
        ecu_state: ECUState,
        on_error: Callable[[str], None],
    ):
        super().__init__(daemon=True, name="SerialWorker")
        self._port = port
        self._baud = baud
        self._state = ecu_state
        self._on_error = on_error
        self._cmd_queue: queue.Queue = queue.Queue()
        self._stop_event = threading.Event()
        self._serial: Optional[serial.Serial] = None

    def stop(self) -> None:
        self._stop_event.set()

    def send_command(self, cmd: int, payload: bytes = b'') -> concurrent.futures.Future:
        """Queue a command and return a Future that resolves to (success: bool, cmd_or_error)."""
        fut: concurrent.futures.Future = concurrent.futures.Future()
        self._cmd_queue.put((cmd, payload, fut))
        return fut

    def read_map(self) -> concurrent.futures.Future:
        """Request the injection map from the ECU. Future resolves to (success: bool, error | None)."""
        return self.send_command(CMD_READ_MAP)

    def read_axis(self) -> concurrent.futures.Future:
        """Request axis breakpoints from the ECU. Future resolves to (success: bool, error | None)."""
        return self.send_command(CMD_READ_AXIS)

    # ── Main loop ────────────────────────────────────────────────────────────

    def run(self) -> None:
        try:
            self._serial = serial.Serial(self._port, self._baud, timeout=READ_TIMEOUT)
            time.sleep(0.1)           # let Arduino reset settle (USB-CDC)
            self._serial.reset_input_buffer()
        except serial.SerialException as exc:
            self._on_error(f"Cannot open port: {exc}")
            return

        # Read injection map and axis breakpoints from device before entering the main loop
        try:
            self._serial.write(build_packet(CMD_READ_MAP))
            pkt = self._read_packet(WRITE_TIMEOUT)
            if pkt and pkt[0] == CMD_READ_MAP:
                self._state.buffer_device_map(decode_map(pkt[1]))
        except (serial.SerialException, ValueError):
            pass  # non-fatal; map editor will show default zeros

        try:
            self._serial.write(build_packet(CMD_READ_AXIS))
            pkt = self._read_packet(WRITE_TIMEOUT)
            if pkt and pkt[0] == CMD_READ_AXIS:
                rpm_pts, tps_pts = decode_axis(pkt[1])
                self._state.buffer_device_axis(rpm_pts, tps_pts)
        except (serial.SerialException, ValueError):
            pass  # non-fatal; axis editor will show compile-time defaults

        try:
            self._serial.write(build_packet(CMD_READ_PUMP_CONFIG))
            pkt = self._read_packet(WRITE_TIMEOUT)
            if pkt and pkt[0] == CMD_READ_PUMP_CONFIG:
                pid, pressure, pump_mode = decode_pump_config(pkt[1])
                self._state.pid = pid
                self._state.pressure = pressure
                self._state.pump_mode_always_on = pump_mode
        except (serial.SerialException, ValueError):
            pass  # non-fatal; panels will show default values

        try:
            self._serial.write(build_packet(CMD_READ_CORRECTIONS))
            pkt = self._read_packet(WRITE_TIMEOUT)
            if pkt and pkt[0] == CMD_READ_CORRECTIONS:
                iat_corr, et_corr = decode_corrections(pkt[1])
                self._state.buffer_device_corrections(iat_corr, et_corr)
        except (serial.SerialException, ValueError):
            pass  # non-fatal; panels will show default values

        try:
            self._serial.write(build_packet(CMD_READ_ACCEL_PUMP))
            pkt = self._read_packet(WRITE_TIMEOUT)
            if pkt and pkt[0] == CMD_READ_ACCEL_PUMP:
                self._state.accel_pump = decode_accel_pump(pkt[1])
        except (serial.SerialException, ValueError):
            pass  # non-fatal; panel will show default values

        self._state.config_fresh.set()

        miss_count = 0
        last_poll = 0.0

        while not self._stop_event.is_set():
            now = time.monotonic()

            # ── Sensor poll ──────────────────────────────────────────────────
            if now - last_poll >= POLL_INTERVAL:
                last_poll = now
                try:
                    self._serial.write(build_packet(CMD_READ_SENSORS))
                    pkt = self._read_packet(READ_TIMEOUT)
                    if pkt and pkt[0] == CMD_READ_SENSORS:
                        self._state.update_sensors(decode_sensor_data(pkt[1]))
                        miss_count = 0
                    else:
                        miss_count += 1
                        if miss_count >= MAX_MISS_COUNT:
                            self._on_error("No response from ECU (timeout)")
                            break
                except serial.SerialException as exc:
                    self._on_error(f"Serial error: {exc}")
                    break

            # ── Command queue ────────────────────────────────────────────────
            try:
                cmd, payload, fut = self._cmd_queue.get_nowait()
            except queue.Empty:
                time.sleep(0.01)
                continue

            try:
                self._serial.write(build_packet(cmd, payload))
                pkt = self._read_packet(WRITE_TIMEOUT)
                if pkt is None:
                    fut.set_result((False, "No response"))
                elif cmd == CMD_READ_MAP and pkt[0] == CMD_READ_MAP:
                    try:
                        self._state.update_inj_map(decode_map(pkt[1]))
                        fut.set_result((True, None))
                    except ValueError as exc:
                        fut.set_result((False, str(exc)))
                elif cmd == CMD_READ_AXIS and pkt[0] == CMD_READ_AXIS:
                    try:
                        rpm_pts, tps_pts = decode_axis(pkt[1])
                        self._state.update_axis(rpm_pts, tps_pts)
                        fut.set_result((True, None))
                    except ValueError as exc:
                        fut.set_result((False, str(exc)))
                elif cmd == CMD_READ_ACCEL_PUMP and pkt[0] == CMD_READ_ACCEL_PUMP:
                    try:
                        self._state.accel_pump = decode_accel_pump(pkt[1])
                        fut.set_result((True, None))
                    except ValueError as exc:
                        fut.set_result((False, str(exc)))
                elif pkt[0] == CMD_ACK:
                    fut.set_result((True, None))
                elif pkt[0] == CMD_NACK:
                    fut.set_result((False, "NACK"))
                else:
                    fut.set_result((False, f"Unexpected response cmd=0x{pkt[0]:02X}"))
            except serial.SerialException as exc:
                fut.set_result((False, str(exc)))
                self._on_error(f"Serial error: {exc}")
                break

        if self._serial and self._serial.is_open:
            self._serial.close()
        self._state.connected = False

    # ── Packet reader ────────────────────────────────────────────────────────

    def _read_packet(self, timeout: float) -> Optional[Tuple[int, bytes]]:
        """
        Read one packet from the serial port.
        State machine: wait for 0xAA → read LEN → read LEN bytes → read CRC → validate.
        Returns (cmd, payload) or None on timeout / CRC error.
        """
        ser = self._serial
        deadline = time.monotonic() + timeout

        # Wait for start byte
        while time.monotonic() < deadline:
            b = ser.read(1)
            if not b:
                continue
            if b[0] == PKT_START:
                break
        else:
            return None

        # Read LEN
        b = ser.read(1)
        if not b:
            return None
        length = b[0]
        if length == 0 or length > 130:
            return None

        # Read CMD + payload (length bytes total)
        body = ser.read(length)
        if len(body) < length:
            return None

        # Read CRC
        b = ser.read(1)
        if not b:
            return None
        crc_byte = b[0]

        return parse_packet(body + bytes([crc_byte]))
