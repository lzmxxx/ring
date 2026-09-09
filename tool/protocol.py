"""
SpO2 Ring 20-byte BLE Packet Protocol Decoder & Statistics
Device: N32WB452 + IPA1322 RT-Thread Pulse Oximeter Ring

Packet Format (20 Bytes, Little-Endian):
  Byte 0..1:   Header 0xA5 0x01
  Byte 2:      Flags (bit 0: valid, bit 1: calibrated, bit 2: moving)
  Byte 3:      Reserved (0x00)
  Byte 4..7:   Seq (uint32)
  Byte 8..9:   SpO2 * 100 (uint16, %)
  Byte 10..11: HR * 10 (uint16, BPM)
  Byte 12..13: PI * 100 (uint16, %)
  Byte 14..15: Ratio (R) * 10000 (uint16)
  Byte 16..17: HR_time * 10 (uint16, BPM)
  Byte 18..19: HR_freq (FFT) * 10 (uint16, BPM)

Battery packet A5 02: flags, capacity %, voltage mV and associated SpO2 seq.
Acceleration packet A5 03: valid flag, seq and signed X/Y/Z acceleration in mg.
Independent algorithm packet A5 05: time/FFT/DST SpO2 and R values.
"""

import struct
import time
from dataclasses import dataclass
from typing import Optional, Tuple, Union


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


@dataclass
class BatteryPacket:
    timestamp: float
    time_str: str
    raw_hex: str
    seq: int
    valid: bool
    capacity: int             # %
    voltage_mv: int           # mV
    cw_ack: int = 0xFF
    cw_version: int = 0xFF
    cw_init: int = 0xFF
    imu_id: int = 0xFF
    imu_config: int = 0
    imu_init: int = 0xFF
    cw_address: int = 0xFF
    i2c_device_count: int = 0
    motion_irq_count: int = 0


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

    if data[0] != 0xA5 or data[1] not in (0x01, 0x02, 0x03, 0x05):
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
            data[10], data[11], data[12], data[13], data[14], data[15],
            data[16], data[17], struct.unpack_from('<H', data, 18)[0]
        ), ""

    if data[1] == 0x03:
        seq = struct.unpack_from('<I', data, 4)[0]
        x_mg, y_mg, z_mg = struct.unpack_from('<hhh', data, 8)
        return True, AccelerationPacket(
            recv_time, time_str, data.hex(' ').upper(), seq,
            bool(data[2] & 0x01), x_mg, y_mg, z_mg
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

    try:
        h0, h1, flags, resv, seq, spo2_raw, hr_raw, pi_raw, ratio_raw, hr_time_raw, hr_fft_raw = struct.unpack(
            PACKET_STRUCT, data
        )
    except Exception as e:
        return False, None, f"Unpack error: {e}"

    valid = bool(flags & 0x01)
    calibrated = bool(flags & 0x02)
    moving = bool(flags & 0x04)

    spo2 = spo2_raw / 100.0
    hr = hr_raw / 10.0
    pi = pi_raw / 100.0
    ratio = ratio_raw / 10000.0
    hr_time = hr_time_raw / 10.0
    hr_fft = hr_fft_raw / 10.0

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
    )
    return True, pkt, ""
