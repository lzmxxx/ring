"""
CSV Data Recorder for SpO2 Ring BLE Host Computer
Logs real-time telemetry packets with millisecond timestamps.
"""

import os
import csv
import time
from typing import Optional
from protocol import SpO2Packet, BatteryPacket


class DataRecorder:
    def __init__(self, default_dir: str = "recordings"):
        self.output_dir = default_dir
        self.is_recording: bool = False
        self.file_path: Optional[str] = None
        self._file = None
        self._writer = None
        self.recorded_count: int = 0
        self.start_time: Optional[float] = None

    def start(self, custom_path: Optional[str] = None) -> str:
        """Start a new recording session. Returns the absolute file path."""
        if self.is_recording:
            self.stop()

        if custom_path:
            self.file_path = os.path.abspath(custom_path)
            os.makedirs(os.path.dirname(self.file_path), exist_ok=True)
        else:
            os.makedirs(self.output_dir, exist_ok=True)
            time_tag = time.strftime("%Y%m%d_%H%M%S")
            filename = f"SpO2_Ring_Log_{time_tag}.csv"
            self.file_path = os.path.abspath(os.path.join(self.output_dir, filename))

        self._file = open(self.file_path, mode="w", newline="", encoding="utf-8-sig")
        self._writer = csv.writer(self._file)
        
        # Header row
        self._writer.writerow([
            "Timestamp_Local",
            "Epoch_Time_s",
            "Packet_Type",
            "Seq",
            "Valid",
            "Calibrated",
            "SpO2_%",
            "HR_BPM",
            "PI_%",
            "Ratio_R",
            "HR_Time_BPM",
            "HR_FFT_BPM",
            "Motion_State",
            "Battery_%",
            "Battery_mV",
            "Raw_Hex"
        ])
        self._file.flush()

        self.is_recording = True
        self.recorded_count = 0
        self.start_time = time.time()
        return self.file_path

    def write(self, pkt: SpO2Packet):
        """Append a packet to the CSV if recording is active."""
        if not self.is_recording or self._writer is None:
            return

        lt = time.localtime(pkt.timestamp)
        ms = int((pkt.timestamp - int(pkt.timestamp)) * 1000)
        dt_str = f"{lt.tm_year:04d}-{lt.tm_mon:02d}-{lt.tm_mday:02d} {lt.tm_hour:02d}:{lt.tm_min:02d}:{lt.tm_sec:02d}.{ms:03d}"

        self._writer.writerow([
            dt_str,
            f"{pkt.timestamp:.3f}",
            "SpO2",
            pkt.seq,
            1 if pkt.valid else 0,
            1 if pkt.calibrated else 0,
            f"{pkt.spo2:.2f}",
            f"{pkt.hr:.1f}",
            f"{pkt.pi:.2f}",
            f"{pkt.ratio:.4f}",
            f"{pkt.hr_time:.1f}",
            f"{pkt.hr_fft:.1f}",
            "Moving" if pkt.moving else "Still",
            "",
            "",
            pkt.raw_hex
        ])
        self.recorded_count += 1
        
        # Periodic flush
        if self.recorded_count % 5 == 0:
            self._file.flush()

    def write_battery(self, pkt: BatteryPacket):
        if not self.is_recording or self._writer is None:
            return
        lt = time.localtime(pkt.timestamp)
        ms = int((pkt.timestamp - int(pkt.timestamp)) * 1000)
        dt_str = f"{lt.tm_year:04d}-{lt.tm_mon:02d}-{lt.tm_mday:02d} {lt.tm_hour:02d}:{lt.tm_min:02d}:{lt.tm_sec:02d}.{ms:03d}"
        self._writer.writerow([
            dt_str, f"{pkt.timestamp:.3f}", "Battery", pkt.seq,
            1 if pkt.valid else 0, "", "", "", "", "", "", "", "",
            pkt.capacity if pkt.valid else "", pkt.voltage_mv if pkt.valid else "",
            pkt.raw_hex
        ])
        self.recorded_count += 1

    def stop(self) -> Optional[str]:
        """Stop current recording session. Returns file path if recorded."""
        if not self.is_recording:
            return None

        saved_path = self.file_path
        self.is_recording = False
        if self._file:
            try:
                self._file.flush()
                self._file.close()
            except Exception:
                pass
            self._file = None
            self._writer = None

        return saved_path

    @property
    def duration(self) -> float:
        """Recording duration in seconds."""
        if not self.is_recording or self.start_time is None:
            return 0.0
        return time.time() - self.start_time
