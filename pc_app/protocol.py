"""
Serial protocol implementation matching the Arduino firmware exactly.
Packet format: [0xAA][LEN][CMD][DATA...][CRC8]
  LEN  = 1 + len(payload)  (CMD byte + payload bytes)
  CRC8 = CRC-8/SMBUS (poly 0x07, init 0x00) over CMD + DATA bytes
"""

import struct
from typing import List, Optional, Tuple

# ── Constants ────────────────────────────────────────────────────────────────

PKT_START = 0xAA
SERIAL_BAUD = 115200

CMD_READ_SENSORS   = 0x01
CMD_WRITE_MAP      = 0x02
CMD_WRITE_PID      = 0x03
CMD_WRITE_PRESSURE = 0x04
CMD_PUMP_PRIME     = 0x05
CMD_PUMP_SET       = 0x0D  # payload: 1 byte (1=on, 0=off)
CMD_PUMP_MODE      = 0x0E  # payload: 1 byte (0=PID, 1=always_on)
CMD_ACK            = 0x06
CMD_NACK           = 0x07
CMD_WRITE_IAT_CORR = 0x08
CMD_WRITE_ET_CORR  = 0x09
CMD_READ_MAP       = 0x0A
CMD_WRITE_AXIS     = 0x0B
CMD_READ_AXIS      = 0x0C

RPM_BINS = 12
TPS_BINS = 5
IAT_BINS = 10
ET_BINS  = 10

RPM_BREAKPOINTS = [500, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 10000, 13000, 16000]
TPS_BREAKPOINTS = [0, 250, 500, 750, 1000]   # per-mille integers (0–1000)

# ── Data classes ─────────────────────────────────────────────────────────────

class SensorData:
    def __init__(self, rpm=0, tps=0.0, fps_bar=0.0, iat_degc=0.0,
                 et_degc=0.0, pump_active=False, bat_v=0.0):
        self.rpm = rpm
        self.tps = tps             # 0.0 – 1.0
        self.fps_bar = fps_bar
        self.iat_degc = iat_degc
        self.et_degc = et_degc
        self.pump_active = pump_active
        self.bat_v = bat_v         # battery voltage in V


class PIDParams:
    def __init__(self, kp=20.0, ki=1.0, kd=0.5):
        self.kp = kp
        self.ki = ki
        self.kd = kd


class PressureConfig:
    def __init__(self, low_bar=2.0, high_bar=3.0, threshold_rpm=3000):
        self.low_bar = low_bar
        self.high_bar = high_bar
        self.threshold_rpm = threshold_rpm


# ── CRC ──────────────────────────────────────────────────────────────────────

def crc8_smbus(data: bytes) -> int:
    """CRC-8/SMBUS: poly=0x07, init=0x00, no reflection, no final XOR."""
    crc = 0x00
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x07) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


# ── Packet build / parse ─────────────────────────────────────────────────────

def build_packet(cmd: int, payload: bytes = b'') -> bytes:
    """Build a complete packet to send to the ECU."""
    length = 1 + len(payload)          # cmd byte + payload
    crc_data = bytes([cmd]) + payload
    crc = crc8_smbus(crc_data)
    return bytes([PKT_START, length, cmd]) + payload + bytes([crc])


def parse_packet(data: bytes) -> Optional[Tuple[int, bytes]]:
    """
    Validate and parse a raw packet buffer (without the start byte and LEN,
    i.e. data = CMD + payload + CRC).
    Returns (cmd, payload) or None on CRC mismatch.
    """
    if len(data) < 2:
        return None
    cmd_and_payload = data[:-1]
    received_crc = data[-1]
    if crc8_smbus(cmd_and_payload) != received_crc:
        return None
    cmd = cmd_and_payload[0]
    payload = cmd_and_payload[1:]
    return cmd, payload


# ── Encode helpers ────────────────────────────────────────────────────────────

def encode_map(inj_map: List[List[int]]) -> bytes:
    """Pack 12×5 injection map to 120 bytes (uint16 big-endian, row-major)."""
    buf = bytearray()
    for row in inj_map:
        for val in row:
            buf += struct.pack('>H', int(val))
    return bytes(buf)


def encode_pid(params: PIDParams) -> bytes:
    """Pack Kp, Ki, Kd as 3 × float32 big-endian (12 bytes)."""
    return struct.pack('>fff', params.kp, params.ki, params.kd)


def encode_pressure(cfg: PressureConfig) -> bytes:
    """Pack low_bar, high_bar (float32) + threshold_rpm (uint16) big-endian (10 bytes)."""
    return struct.pack('>ffH', cfg.low_bar, cfg.high_bar, cfg.threshold_rpm)


def encode_corrections(values: List[float]) -> bytes:
    """Pack 10 × uint16 Q8.8 big-endian (20 bytes). 1.0 = 256."""
    raw = [round(v * 256) for v in values]
    return struct.pack('>' + 'H' * len(raw), *raw)


def encode_axis(rpm_pts: List[int], tps_pts: List[float]) -> bytes:
    """Pack 12 × uint16 RPM + 5 × uint16 TPS per-mille big-endian (34 bytes)."""
    tps_raw = [round(v * 1000) for v in tps_pts]
    return struct.pack('>' + 'H' * RPM_BINS, *rpm_pts) + \
           struct.pack('>' + 'H' * TPS_BINS, *tps_raw)


# ── Decode helpers ────────────────────────────────────────────────────────────

def decode_map(payload: bytes) -> List[List[int]]:
    """Unpack 120-byte map payload to 12×5 list of uint16 pulse widths."""
    if len(payload) < RPM_BINS * TPS_BINS * 2:
        raise ValueError(f"Map payload too short: {len(payload)}")
    result = []
    for r in range(RPM_BINS):
        row = []
        for t in range(TPS_BINS):
            idx = (r * TPS_BINS + t) * 2
            row.append(struct.unpack_from('>H', payload, idx)[0])
        result.append(row)
    return result


def decode_axis(payload: bytes) -> Tuple[List[int], List[float]]:
    """Unpack 34-byte axis payload to (rpm_pts, tps_pts). TPS values returned as 0.0–1.0."""
    if len(payload) < RPM_BINS * 2 + TPS_BINS * 2:
        raise ValueError(f"Axis payload too short: {len(payload)}")
    rpm_pts = list(struct.unpack_from('>' + 'H' * RPM_BINS, payload, 0))
    tps_raw = list(struct.unpack_from('>' + 'H' * TPS_BINS, payload, RPM_BINS * 2))
    tps_pts = [v / 1000.0 for v in tps_raw]
    return rpm_pts, tps_pts


def decode_sensor_data(payload: bytes) -> SensorData:
    """Unpack 13-byte sensor response payload."""
    if len(payload) < 13:
        raise ValueError(f"Sensor payload too short: {len(payload)}")
    rpm, tps_raw, fps_raw, iat_raw, et_raw, pump_active, bat_raw = struct.unpack('>HHHhhBH', payload[:13])
    return SensorData(
        rpm=rpm,
        tps=tps_raw / 1000.0,
        fps_bar=fps_raw / 100.0,
        iat_degc=iat_raw / 10.0,
        et_degc=et_raw / 10.0,
        pump_active=bool(pump_active),
        bat_v=bat_raw / 100.0,
    )


# ── Nearest-bin lookup (for interactive cursor) ───────────────────────────────

def nearest_rpm_bin(rpm: int, axis: Optional[List[int]] = None) -> int:
    pts = axis if axis is not None else RPM_BREAKPOINTS
    return min(range(len(pts)), key=lambda i: abs(pts[i] - rpm))


def nearest_tps_bin(tps: float, axis: Optional[List[float]] = None) -> int:
    pts = axis if axis is not None else [v / 1000.0 for v in TPS_BREAKPOINTS]
    return min(range(len(pts)), key=lambda i: abs(pts[i] - tps))
