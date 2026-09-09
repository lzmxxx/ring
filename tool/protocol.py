"""SpO2 Ring BLE protocol decoder and statistics.

The current firmware sends fixed 20-byte A5 result, battery and raw-PPG
packets, plus AA55 control frames protected by CRC16.  A5 acceleration and
independent-algorithm parsing remains read-only compatibility for old captures.
"""

import struct
import time
from dataclasses import dataclass
from typing import Dict, Optional, Tuple, Union


@dataclass
class SpO2Packet:
    timestamp: float          # Local reception timestamp (epoch seconds)
    time_str: str             # Formatted time "HH:MM:SS.mmm"
    raw_hex: str              # Hex representation of 20 bytes
    seq: int                  # Sequence counter
    valid: bool               # Algorithm output validity
    calibrated: bool          # Calibration flag
    spo2: float               # SpO2 in % (e.g. 98.5)
    hr: float                 # Comprehensive Heart Rate (BPM, e.g. 72.0)
    pi: float                 # Perfusion Index in % (e.g. 1.85)
    ratio: float              # R ratio (e.g. 0.5420)
    hr_time: float            # Time-domain Autocorrelation HR (BPM)
    hr_fft: float             # Frequency-domain FFT HR (BPM)
    moving: bool = False      # SC7A20 internal motion decision
    device_timestamp: int = 0 # RTC Unix time reported by the ring
    quality: int = 0          # 0..100 signal quality hint
    temperature: float = 0.0  # reserved, degrees Celsius


@dataclass
class BatteryPacket:
    timestamp: float
    time_str: str
    raw_hex: str
    seq: int
    valid: bool
    capacity: int             # %
    voltage_mv: int           # mV


@dataclass
class AccelerationPacket:
    timestamp: float
    time_str: str
    raw_hex: str
    seq: int
    valid: bool
    x_mg: int
    y_mg: int
    z_mg: int


@dataclass
class AlgorithmPacket:
    timestamp: float
    time_str: str
    raw_hex: str
    seq: int
    time_valid: bool
    fft_valid: bool
    dst_valid: bool
    pair_count: int
    spo2_time: float
    spo2_fft: float
    spo2_dst: float
    ratio_time: float
    ratio_fft: float
    ratio_dst: float


@dataclass
class RawPPGPacket:
    timestamp: float
    time_str: str
    raw_hex: str
    first_seq: int
    sample_rate: int
    device_timestamp: int
    samples: Tuple[Tuple[int, int], ...]


@dataclass
class AckPacket:
    command: int
    sequence: int
    value0: int = 0
    value1: int = 0
    event: int = 0
    event_value: int = 0


@dataclass
class ErrorPacket:
    command: int
    sequence: int
    error: int


@dataclass
class DeviceStatusPacket:
    state: int
    output_mode: int
    sample_period: int
    flags: int
    error: int
    timezone_min: int
    firmware_version: int
    device_time: int


@dataclass
class RecordInfoPacket:
    session_id: int
    record_count: int
    first_timestamp: int
    last_timestamp: int
    flash_used: int
    flash_total: int


@dataclass
class HistoryRecordPacket:
    timestamp: int
    sequence: int
    spo2: int
    heart_rate: int
    motion: int
    quality: int
    temperature: float
    flags: int


class ProtocolStats:
    """Tracks frame counters, packet rates, packet loss, and validity ratio."""
    def __init__(self):
        self.total_packets: int = 0
        self.valid_packets: int = 0
        self.invalid_packets: int = 0
        self.lost_packets: int = 0
        self.last_seq: Optional[int] = None
        self.start_time: Optional[float] = None
        self.last_recv_time: Optional[float] = None
        self.fps: float = 0.0
        self._fps_window = []

    def update(self, packet: SpO2Packet):
        now = packet.timestamp
        if self.start_time is None:
            self.start_time = now

        self.total_packets += 1
        if packet.valid:
            self.valid_packets += 1
        else:
            self.invalid_packets += 1

        # Check packet loss based on seq discontinuity
        if self.last_seq is not None:
            expected = (self.last_seq + 1) & 0xFFFFFFFF
            if packet.seq > expected:
                diff = packet.seq - expected
                if diff < 10000:  # avoid overflow/reset false positive
                    self.lost_packets += diff
        self.last_seq = packet.seq

        # Calculate sliding window FPS
        self._fps_window.append(now)
        # Retain last 3 seconds of timestamps
        while self._fps_window and (now - self._fps_window[0] > 3.0):
            self._fps_window.pop(0)

        if len(self._fps_window) > 1:
            duration = self._fps_window[-1] - self._fps_window[0]
            if duration > 0.1:
                self.fps = (len(self._fps_window) - 1) / duration
            else:
                self.fps = 0.0

        self.last_recv_time = now

    def reset(self):
        self.total_packets = 0
        self.valid_packets = 0
        self.invalid_packets = 0
        self.lost_packets = 0
        self.last_seq = None
        self.start_time = None
        self.last_recv_time = None
        self.fps = 0.0
        self._fps_window.clear()

    @property
    def loss_rate(self) -> float:
        total = self.total_packets + self.lost_packets
        if total == 0:
            return 0.0
        return (self.lost_packets / total) * 100.0

    @property
    def valid_rate(self) -> float:
        if self.total_packets == 0:
            return 0.0
        return (self.valid_packets / self.total_packets) * 100.0


PACKET_SIZE = 20
PACKET_STRUCT = '<BBBB I H H H H H H'


def parse_packet(data: bytes, recv_time: Optional[float] = None) -> Tuple[bool, Optional[Union[SpO2Packet, BatteryPacket, AccelerationPacket, AlgorithmPacket]], str]:
    """
    Parse a raw byte buffer into an SpO2Packet.
    Returns (success: bool, packet: Optional[SpO2Packet], err_msg: str).
    """
    if len(data) != PACKET_SIZE:
        return False, None, f"Incorrect packet length {len(data)} (expected {PACKET_SIZE})"

    if data[0] != 0xA5 or data[1] not in (0x01, 0x02, 0x03, 0x04, 0x05):
        return False, None, f"Invalid packet header/type: {data[:2].hex().upper()}"

    if recv_time is None:
        recv_time = time.time()

    lt = time.localtime(recv_time)
    ms = int((recv_time - int(recv_time)) * 1000)
    time_str = f"{lt.tm_hour:02d}:{lt.tm_min:02d}:{lt.tm_sec:02d}.{ms:03d}"

    if data[1] == 0x02:
        flags, capacity = data[2], data[3]
        voltage_mv = struct.unpack_from('<H', data, 4)[0]
        seq = struct.unpack_from('<I', data, 6)[0]
        return True, BatteryPacket(
            recv_time, time_str, data.hex(' ').upper(), seq,
            bool(flags & 0x01), capacity, voltage_mv,
        ), ""

    if data[1] == 0x03:
        seq = struct.unpack_from('<I', data, 4)[0]
        x_mg, y_mg, z_mg = struct.unpack_from('<hhh', data, 8)
        return True, AccelerationPacket(
            recv_time, time_str, data.hex(' ').upper(), seq,
            bool(data[2] & 0x01), x_mg, y_mg, z_mg
        ), ""

    if data[1] == 0x04:
        first_seq = struct.unpack_from('<I', data, 4)[0]
        device_timestamp = struct.unpack_from('<I', data, 8)[0]
        red0, ir0, red1, ir1 = struct.unpack_from('<hhhh', data, 12)
        return True, RawPPGPacket(
            recv_time, time_str, data.hex(' ').upper(), first_seq, data[3], device_timestamp,
            ((red0 << 4, ir0 << 4), (red1 << 4, ir1 << 4))
        ), ""

    if data[1] == 0x05:
        seq = struct.unpack_from('<I', data, 4)[0]
        values = struct.unpack_from('<HHHHHH', data, 8)
        return True, AlgorithmPacket(
            recv_time, time_str, data.hex(' ').upper(), seq,
            bool(data[2] & 0x01), bool(data[2] & 0x02), bool(data[2] & 0x04),
            data[3], values[0] / 100.0, values[1] / 100.0,
            values[2] / 100.0, values[3] / 10000.0,
            values[4] / 10000.0, values[5] / 10000.0,
        ), ""

    flags, quality = data[2], data[3]
    seq, device_timestamp = struct.unpack_from('<II', data, 4)
    spo2_raw, hr_raw, pi_raw, temperature_raw = struct.unpack_from('<HHHh', data, 12)

    valid = bool(flags & 0x01)
    calibrated = bool(flags & 0x02)
    moving = bool(flags & 0x04)

    spo2 = spo2_raw / 100.0
    hr = hr_raw / 10.0
    pi = pi_raw / 100.0
    ratio = 0.0
    hr_time = hr
    hr_fft = 0.0

    pkt = SpO2Packet(
        timestamp=recv_time,
        time_str=time_str,
        raw_hex=data.hex(' ').upper(),
        seq=seq,
        valid=valid,
        calibrated=calibrated,
        spo2=spo2,
        hr=hr,
        pi=pi,
        ratio=ratio,
        hr_time=hr_time,
        hr_fft=hr_fft,
        moving=moving,
        device_timestamp=device_timestamp,
        quality=quality,
        temperature=temperature_raw / 100.0,
    )
    return True, pkt, ""


SOF = b'\xAA\x55'
PROTOCOL_VERSION = 1
CMD_TIME_SYNC = 0x01
CMD_SET_OUTPUT_MODE = 0x10
CMD_SET_SAMPLE_PERIOD = 0x11
CMD_START_RECORD = 0x20
CMD_STOP_RECORD = 0x21
CMD_GET_RECORD_INFO = 0x22
CMD_SYNC_RECORD = 0x23
CMD_ERASE_RECORD = 0x24
CMD_GET_DEVICE_STATUS = 0x30
CMD_SET_MOTION_PARAMETER = 0x40
CMD_ACK = 0x80
CMD_ERROR = 0x81


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def build_command(command: int, sequence: int, payload: bytes = b'') -> bytes:
    if len(payload) > 10:
        raise ValueError("application payload must fit one 20-byte BLE packet")
    frame = SOF + bytes((PROTOCOL_VERSION, command)) + struct.pack('<HH', sequence & 0xFFFF, len(payload)) + payload
    return frame + struct.pack('<H', crc16_ccitt(frame))


class ProtocolDecoder:
    """Stateful decoder for segmented status, record-info and history frames."""
    def __init__(self):
        self._parts: Dict[Tuple[int, int], Dict[int, bytes]] = {}

    def feed(self, data: bytes):
        if data.startswith(b'\xA5'):
            ok, packet, error = parse_packet(data)
            return packet if ok else ErrorPacket(0, 0, 0xFF)
        if len(data) < 10 or data[:2] != SOF:
            return None
        version, command = data[2], data[3]
        sequence, payload_len = struct.unpack_from('<HH', data, 4)
        if version != PROTOCOL_VERSION or payload_len > 10 or len(data) != payload_len + 10:
            return None
        if crc16_ccitt(data[:-2]) != struct.unpack_from('<H', data, len(data) - 2)[0]:
            return None
        payload = data[8:-2]
        if command == CMD_ACK and len(payload) >= 2:
            if payload[0] == 0xFF and len(payload) == 7:
                return AckPacket(0xFF, sequence, event=payload[2], event_value=struct.unpack_from('<I', payload, 3)[0])
            return AckPacket(payload[0], sequence, payload[2] if len(payload) > 2 else 0, payload[3] if len(payload) > 3 else 0)
        if command == CMD_ERROR and len(payload) >= 2:
            return ErrorPacket(payload[0], sequence, payload[1])
        if command in (CMD_GET_DEVICE_STATUS, CMD_GET_RECORD_INFO, CMD_SYNC_RECORD) and len(payload) >= 2:
            part, total = payload[0], payload[1]
            key = (command, sequence)
            bucket = self._parts.setdefault(key, {})
            bucket[part] = payload[2:]
            if len(bucket) < total:
                return None
            joined = b''.join(bucket[i] for i in range(total))
            del self._parts[key]
            if command == CMD_GET_DEVICE_STATUS and len(joined) == 16:
                state, mode, period, flags, error, timezone = struct.unpack_from('<BBHBBh', joined, 0)
                firmware, device_time = struct.unpack_from('<II', joined, 8)
                return DeviceStatusPacket(state, mode, period, flags, error, timezone, firmware, device_time)
            if command == CMD_GET_RECORD_INFO and len(joined) == 24:
                return RecordInfoPacket(*struct.unpack('<IIIIII', joined))
            if command == CMD_SYNC_RECORD and len(joined) == 16:
                timestamp, record_seq, spo2, hr, motion, quality, temp, flags, _reserved, crc = struct.unpack('<IHBBBBhBBH', joined)
                if crc16_ccitt(joined[:14]) != crc:
                    return ErrorPacket(CMD_SYNC_RECORD, sequence, 0xFE)
                return HistoryRecordPacket(timestamp, record_seq, spo2, hr, motion, quality, temp / 100.0, flags)
        return None
