"""
BLE Client for SpO2 Ring (N32WB452 + IPA1322)
Handles device scanning, connection lifecycle, CCC notification subscription,
and raw packet reception using Bleak and PyQt5 Signals.
"""

import asyncio
import logging
from typing import Optional, List, Dict
from PyQt5.QtCore import QObject, pyqtSignal

from bleak import BleakScanner, BleakClient
from bleak.backends.device import BLEDevice
from protocol import (
    ProtocolDecoder, SpO2Packet, BatteryPacket, AccelerationPacket, AlgorithmPacket,
    RawPPGPacket, AckPacket, ErrorPacket, DeviceStatusPacket, RecordInfoPacket,
    HistoryRecordPacket, build_command, CMD_TIME_SYNC, CMD_GET_DEVICE_STATUS, CMD_GET_RECORD_INFO,
)

logger = logging.getLogger("BLEClient")

TARGET_SERVICE_UUID_16 = "fee7"
TARGET_CHAR_UUID_16 = "fff5"
TARGET_CHAR_FALLBACK_16 = "fec1"


class BLEManager(QObject):
    # Signals for Qt UI integration
    device_discovered = pyqtSignal(str, str, int)  # name, address, rssi
    scan_finished = pyqtSignal(int)               # count of devices found
    connection_status_changed = pyqtSignal(str, str) # state ("disconnected", "connecting", "connected"), message
    packet_received = pyqtSignal(object)          # SpO2Packet instance
    battery_received = pyqtSignal(object)         # BatteryPacket instance
    acceleration_received = pyqtSignal(object)    # AccelerationPacket instance
    algorithm_received = pyqtSignal(object)       # AlgorithmPacket instance
    log_message = pyqtSignal(str, str)            # text, level ("info", "warn", "error", "success")
    notify_state_changed = pyqtSignal(bool, str)  # is_notifying, char_uuid
    raw_received = pyqtSignal(object)
    ack_received = pyqtSignal(object)
    error_received = pyqtSignal(object)
    device_status_received = pyqtSignal(object)
    record_info_received = pyqtSignal(object)
    history_received = pyqtSignal(object)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.client: Optional[BleakClient] = None
        self.connected_device: Optional[BLEDevice] = None
        self.connected_char_uuid: Optional[str] = None
        self.is_scanning: bool = False
        self.auto_reconnect: bool = False
        self._target_address: Optional[str] = None
        self._discovered_devices: Dict[str, BLEDevice] = {}
        self._sim_task: Optional[asyncio.Task] = None
        self.is_simulation: bool = False
        self._decoder = ProtocolDecoder()
        self._command_sequence: int = 0

    @property
    def is_connected(self) -> bool:
        return self.client is not None and self.client.is_connected

    async def start_scan(self, timeout: float = 4.0):
        """Scans for nearby BLE devices and emits signals as they are discovered."""
        if self.is_scanning:
            return

        self.is_scanning = True
        self._discovered_devices.clear()
        self.log_message.emit("正在扫描周围低功耗蓝牙 (BLE) 设备...", "info")

        def detection_callback(device: BLEDevice, advertisement_data):
            name = device.name or advertisement_data.local_name or "Unknown"
            addr = device.address
            rssi = advertisement_data.rssi
            if addr not in self._discovered_devices:
                self._discovered_devices[addr] = device
                self.device_discovered.emit(name, addr, rssi)

        try:
            scanner = BleakScanner(detection_callback=detection_callback)
            await scanner.start()
            await asyncio.sleep(timeout)
            await scanner.stop()
            self.log_message.emit(f"扫描完成，共发现 {len(self._discovered_devices)} 个设备", "success")
        except Exception as e:
            self.log_message.emit(f"蓝牙扫描异常: {e}", "error")
        finally:
            self.is_scanning = False
            self.scan_finished.emit(len(self._discovered_devices))

    async def connect(self, address_or_device: str):
        """Connect to a BLE peripheral by address or name."""
        if self.is_simulation:
            await self.stop_simulation()

        if self.is_connected:
            await self.disconnect()

        self._target_address = address_or_device
        self.connection_status_changed.emit("connecting", f"正在连接设备: {address_or_device}...")
        self.log_message.emit(f"尝试连接蓝牙目标: {address_or_device}...", "info")

        def on_disconnected(client):
            self.connection_status_changed.emit("disconnected", "设备已断开连接")
            self.log_message.emit("BLE 连接已断开", "warn")
            self.notify_state_changed.emit(False, "")
            if self.auto_reconnect and self._target_address:
                asyncio.create_task(self._reconnect_loop())

        try:
            self.client = BleakClient(address_or_device, disconnected_callback=on_disconnected)
            await self.client.connect(timeout=10.0)

            if not self.client.is_connected:
                raise RuntimeError("无法建立 BLE 连接")

            self.connection_status_changed.emit("connected", f"已连接至 {address_or_device}")
            self.log_message.emit(f"成功连接至设备: {address_or_device}", "success")

            # Discover notify characteristic
            notify_char = await self._find_notify_characteristic()
            if notify_char:
                self.connected_char_uuid = notify_char.uuid
                self.log_message.emit(f"找到 Notify 特征: {notify_char.uuid}，正在写入 CCC 订阅...", "info")
                await self.client.start_notify(notify_char.uuid, self._on_notification)
                self.notify_state_changed.emit(True, notify_char.uuid)
                self.log_message.emit("已成功启用通知 (CCC 0x0001)，等待血氧结果包...", "success")
                await self.sync_time()
                await self.send_command(CMD_GET_DEVICE_STATUS)
                await self.send_command(CMD_GET_RECORD_INFO)
            else:
                self.log_message.emit("未找到匹配的 Notify 特征通道，请检查固件 GATT 设置！", "warn")

        except Exception as e:
            self.connection_status_changed.emit("disconnected", f"连接失败: {e}")
            self.log_message.emit(f"连接失败: {e}", "error")
            if self.client:
                try:
                    await self.client.disconnect()
                except Exception:
                    pass
                self.client = None

    async def _find_notify_characteristic(self):
        """Finds the best characteristic supporting NOTIFY."""
        if not self.client or not self.client.services:
            return None

        # 1. Match FFF5 or FEC1 characteristic under FEE7 service
        for service in self.client.services:
            svc_uuid_lower = service.uuid.lower()
            if TARGET_SERVICE_UUID_16 in svc_uuid_lower:
                for char in service.characteristics:
                    char_uuid_lower = char.uuid.lower()
                    if (TARGET_CHAR_UUID_16 in char_uuid_lower or TARGET_CHAR_FALLBACK_16 in char_uuid_lower) and "notify" in char.properties:
                        return char

        # 2. Match any characteristic with notify under FEE7 service
        for service in self.client.services:
            if TARGET_SERVICE_UUID_16 in service.uuid.lower():
                for char in service.characteristics:
                    if "notify" in char.properties:
                        return char

        # 3. Fallback: Any characteristic matching FFF5 in the whole device
        for service in self.client.services:
            for char in service.characteristics:
                if TARGET_CHAR_UUID_16 in char.uuid.lower() and "notify" in char.properties:
                    return char

        # 4. Fallback: Any characteristic with notify
        for service in self.client.services:
            for char in service.characteristics:
                if "notify" in char.properties:
                    return char

        return None

    def _on_notification(self, characteristic, data: bytearray):
        """Bleak notification callback receiving raw 20-byte bytes."""
        pkt = self._decoder.feed(bytes(data))
        if isinstance(pkt, BatteryPacket): self.battery_received.emit(pkt)
        elif isinstance(pkt, AccelerationPacket): self.acceleration_received.emit(pkt)
        elif isinstance(pkt, AlgorithmPacket): self.algorithm_received.emit(pkt)
        elif isinstance(pkt, SpO2Packet): self.packet_received.emit(pkt)
        elif isinstance(pkt, RawPPGPacket): self.raw_received.emit(pkt)
        elif isinstance(pkt, AckPacket): self.ack_received.emit(pkt)
        elif isinstance(pkt, ErrorPacket): self.error_received.emit(pkt)
        elif isinstance(pkt, DeviceStatusPacket): self.device_status_received.emit(pkt)
        elif isinstance(pkt, RecordInfoPacket): self.record_info_received.emit(pkt)
        elif isinstance(pkt, HistoryRecordPacket): self.history_received.emit(pkt)
        elif pkt is None and not bytes(data).startswith(b'\xAA\x55'):
            self.log_message.emit(f"收到无法识别的报文: {bytes(data).hex().upper()}", "warn")

    async def send_command(self, command: int, payload: bytes = b''):
        if not self.is_connected or not self.connected_char_uuid:
            raise RuntimeError("设备尚未连接并订阅")
        self._command_sequence = (self._command_sequence + 1) & 0xFFFF
        frame = build_command(command, self._command_sequence, payload)
        await self.client.write_gatt_char(self.connected_char_uuid, frame, response=False)
        return self._command_sequence

    async def sync_time(self):
        import datetime
        import struct
        now = datetime.datetime.now().astimezone()
        timezone_min = int(now.utcoffset().total_seconds() // 60)
        await self.send_command(CMD_TIME_SYNC, struct.pack('<Ih', int(now.timestamp()), timezone_min))
        self.log_message.emit(f"已自动同步设备 RTC（时区 {timezone_min:+d} 分钟）", "success")

    async def disconnect(self):
        """Disconnect active BLE connection."""
        self.auto_reconnect = False
        if self._sim_task and not self._sim_task.done():
            self._sim_task.cancel()

        if self.client:
            try:
                if self.connected_char_uuid and self.client.is_connected:
                    await self.client.stop_notify(self.connected_char_uuid)
            except Exception:
                pass
            try:
                await self.client.disconnect()
            except Exception:
                pass
            self.client = None

        self.connected_char_uuid = None
        self.connection_status_changed.emit("disconnected", "已断开连接")
        self.log_message.emit("BLE 客户端已断开", "info")
        self.notify_state_changed.emit(False, "")

    async def _reconnect_loop(self):
        """Background retry loop for auto-reconnect."""
        retry_count = 0
        while self.auto_reconnect and not self.is_connected and retry_count < 10:
            retry_count += 1
            self.log_message.emit(f"正在尝试自动重连 ({retry_count}/10)...", "info")
            await asyncio.sleep(2.0)
            try:
                await self.connect(self._target_address)
                if self.is_connected:
                    return
            except Exception:
                pass

    # ================= Simulated Test Mode =================
    async def start_simulation(self):
        """Starts synthetic data generation for testing UI without real hardware."""
        if self.is_connected:
            await self.disconnect()

        self.is_simulation = True
        self.connection_status_changed.emit("connected", "已开启数据模拟演示模式")
        self.log_message.emit("已启动模拟数据流（~1.0Hz 仿真固件输出）", "success")
        self.notify_state_changed.emit(True, "0000fff5-demo")
        self._sim_task = asyncio.create_task(self._simulation_loop())

    async def stop_simulation(self):
        self.is_simulation = False
        if self._sim_task and not self._sim_task.done():
            self._sim_task.cancel()
            self._sim_task = None
        self.connection_status_changed.emit("disconnected", "模拟模式已停止")
        self.notify_state_changed.emit(False, "")

    async def _simulation_loop(self):
        import math
        import random
        import struct

        seq = 100
        base_spo2 = 98.2
        base_hr = 72.0
        t = 0.0

        try:
            while self.is_simulation:
                await asyncio.sleep(1.0)  # ~1 update per second matching 75 frames update
                t += 1.0
                seq += 1

                # Generate gentle fluctuations
                spo2 = round(base_spo2 + 0.6 * math.sin(t * 0.1) + random.uniform(-0.2, 0.2), 2)
                spo2 = max(90.0, min(100.0, spo2))
                
                hr = round(base_hr + 4.0 * math.sin(t * 0.15) + random.uniform(-1.0, 1.0), 1)
                hr_time = round(hr + random.uniform(-1.5, 1.5), 1)
                hr_fft = round(hr + random.uniform(-1.0, 1.0), 1)
                
                pi = round(1.85 + 0.3 * math.sin(t * 0.2) + random.uniform(-0.05, 0.05), 2)
                ratio = round(0.5420 + 0.015 * math.cos(t * 0.1) + random.uniform(-0.002, 0.002), 4)

                valid = True
                calibrated = False  # 0 matches firmware current status

                moving = (int(t) // 8) % 2 == 1
                flags = ((0x01 if valid else 0x00) |
                         (0x02 if calibrated else 0x00) |
                         (0x04 if moving else 0x00))
                spo2_raw = int(spo2 * 100)
                hr_raw = int(hr * 10)
                pi_raw = int(pi * 100)
                ratio_raw = int(ratio * 10000)
                hr_time_raw = int(hr_time * 10)
                hr_fft_raw = int(hr_fft * 10)

                packet_bytes = struct.pack(
                    '<BBBB I H H H H H H',
                    0xA5, 0x01, flags, 0x00, seq,
                    spo2_raw, hr_raw, pi_raw, ratio_raw,
                    hr_time_raw, hr_fft_raw
                )

                self._on_notification("0000fff5-demo", packet_bytes)
                if seq % 30 == 0:
                    battery = bytearray(20)
                    struct.pack_into('<BBBBHI', battery, 0, 0xA5, 0x02, 0x01,
                                     87, 3980, seq)
                    self._on_notification("0000fff5-demo", battery)
        except asyncio.CancelledError:
            pass
