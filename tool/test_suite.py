"""
Automated Unit Tests for SpO2 BLE Host Computer
Verifies Protocol parsing, Statistics, DataRecorder, and BLE Mock.
"""

import os
import struct
import tempfile
import unittest
from protocol import (
    parse_packet, ProtocolStats, ProtocolDecoder, SpO2Packet, BatteryPacket,
    AccelerationPacket, AlgorithmPacket, DeviceStatusPacket, build_command,
    CMD_TIME_SYNC, CMD_GET_DEVICE_STATUS, crc16_ccitt,
)
from data_recorder import DataRecorder


class TestSpO2Protocol(unittest.TestCase):
    def test_pack_and_parse_valid(self):
        # Header A5 01, flags=0x01 (valid), reserved=0, seq=100
        # spo2=9850 (98.5%), hr=720 (72.0 BPM), pi=210 (2.10%), ratio=5420 (0.5420), hr_time=715 (71.5), hr_fft=725 (72.5)
        raw_bytes = struct.pack('<BBBBIIHHHh', 0xA5, 0x01, 0x05, 88, 100,
                                1700000000, 9850, 720, 210, 0)
        self.assertEqual(len(raw_bytes), 20)

        ok, pkt, err = parse_packet(raw_bytes, recv_time=1700000000.123)
        self.assertTrue(ok)
        self.assertIsNotNone(pkt)
        self.assertEqual(pkt.seq, 100)
        self.assertTrue(pkt.valid)
        self.assertFalse(pkt.calibrated)
        self.assertTrue(pkt.moving)
        self.assertAlmostEqual(pkt.spo2, 98.5, places=2)
        self.assertAlmostEqual(pkt.hr, 72.0, places=1)
        self.assertAlmostEqual(pkt.pi, 2.10, places=2)
        self.assertEqual(pkt.device_timestamp, 1700000000)
        self.assertEqual(pkt.quality, 88)
        self.assertAlmostEqual(pkt.hr_time, 72.0, places=1)

    def test_application_command_crc(self):
        raw = build_command(CMD_TIME_SYNC, 7, struct.pack('<Ih', 1700000000, 480))
        self.assertEqual(len(raw), 16)
        self.assertEqual(struct.unpack_from('<H', raw, len(raw) - 2)[0], crc16_ccitt(raw[:-2]))

    def test_segmented_device_status(self):
        decoder = ProtocolDecoder()
        joined = struct.pack('<BBHBBhII', 3, 2, 15, 0x1F, 0, 480, 0x20260909, 1700000000)
        def response(part):
            payload = bytes((part, 2)) + joined[part * 8:(part + 1) * 8]
            return build_command(CMD_GET_DEVICE_STATUS, 9, payload)
        self.assertIsNone(decoder.feed(response(0)))
        packet = decoder.feed(response(1))
        self.assertIsInstance(packet, DeviceStatusPacket)
        self.assertEqual(packet.sample_period, 15)

    def test_invalid_header_or_size(self):
        # Short packet
        ok, pkt, err = parse_packet(b'\xA5\x01\x00')
        self.assertFalse(ok)
        self.assertIn("length", err)

        # Wrong header
        bad_hdr = b'\x5A\x01' + b'\x00' * 18
        ok, pkt, err = parse_packet(bad_hdr)
        self.assertFalse(ok)
        self.assertIn("header", err)

    def test_battery_packet(self):
        raw = bytearray(20)
        struct.pack_into('<BBBBHI', raw, 0, 0xA5, 0x02, 0x01, 87, 3980, 130)
        ok, pkt, err = parse_packet(bytes(raw), recv_time=1700000001.0)
        self.assertTrue(ok, err)
        self.assertIsInstance(pkt, BatteryPacket)
        self.assertTrue(pkt.valid)
        self.assertEqual(pkt.capacity, 87)
        self.assertEqual(pkt.voltage_mv, 3980)
        self.assertEqual(pkt.seq, 130)

    def test_acceleration_packet(self):
        raw = bytearray(20)
        struct.pack_into('<BBBxIhhh', raw, 0, 0xA5, 0x03, 0x01,
                         131, -125, 42, 998)
        ok, pkt, err = parse_packet(bytes(raw), recv_time=1700000002.0)
        self.assertTrue(ok, err)
        self.assertIsInstance(pkt, AccelerationPacket)
        self.assertEqual((pkt.x_mg, pkt.y_mg, pkt.z_mg), (-125, 42, 998))

    def test_independent_algorithm_packet(self):
        raw = struct.pack('<BBBBIHHHHHH', 0xA5, 0x05, 0x07, 5, 600,
                          9423, 8969, 9200, 6920, 8147, 7543)
        ok, pkt, err = parse_packet(raw, recv_time=1700000003.0)
        self.assertTrue(ok, err)
        self.assertIsInstance(pkt, AlgorithmPacket)
        self.assertTrue(pkt.time_valid and pkt.fft_valid and pkt.dst_valid)
        self.assertEqual(pkt.pair_count, 5)
        self.assertAlmostEqual(pkt.spo2_time, 94.23, places=2)
        self.assertAlmostEqual(pkt.spo2_fft, 89.69, places=2)
        self.assertAlmostEqual(pkt.spo2_dst, 92.00, places=2)

    def test_statistics(self):
        stats = ProtocolStats()
        pkt1 = SpO2Packet(1.0, "00:00:01", "A5 01...", 1, True, False, 98.0, 70.0, 2.0, 0.5, 70.0, 70.0)
        pkt2 = SpO2Packet(2.0, "00:00:02", "A5 01...", 2, True, False, 98.0, 70.0, 2.0, 0.5, 70.0, 70.0)
        pkt4 = SpO2Packet(4.0, "00:00:04", "A5 01...", 4, False, False, 95.0, 80.0, 1.5, 0.6, 80.0, 80.0) # Lost seq 3

        stats.update(pkt1)
        stats.update(pkt2)
        stats.update(pkt4)

        self.assertEqual(stats.total_packets, 3)
        self.assertEqual(stats.valid_packets, 2)
        self.assertEqual(stats.invalid_packets, 1)
        self.assertEqual(stats.lost_packets, 1)
        self.assertAlmostEqual(stats.loss_rate, 25.0, places=1)

    def test_data_recorder(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            recorder = DataRecorder(default_dir=tmpdir)
            file_path = recorder.start()
            self.assertTrue(recorder.is_recording)
            self.assertTrue(os.path.exists(file_path))

            pkt = SpO2Packet(1700000000.0, "12:00:00.000", "A5 01 01...", 1, True, False, 98.5, 72.0, 2.1, 0.54, 71.8, 72.5)
            recorder.write(pkt)
            battery = BatteryPacket(1700000001.0, "12:00:01.000", "A5 02...", 30, True, 86, 3970)
            recorder.write_battery(battery)
            self.assertEqual(recorder.recorded_count, 2)

            saved = recorder.stop()
            self.assertFalse(recorder.is_recording)
            self.assertEqual(saved, file_path)

            with open(file_path, "r", encoding="utf-8-sig") as f:
                lines = f.readlines()
            self.assertEqual(len(lines), 3)  # Header + SpO2 + battery
            self.assertIn("Timestamp_Local", lines[0])
            self.assertIn("98.50", lines[1])
            self.assertIn("Battery", lines[2])


if __name__ == "__main__":
    unittest.main()
