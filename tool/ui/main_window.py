"""
Main Window for SpO2-Ring BLE Host Computer.
Combines BLE Manager, Dashboard Cards, Real-time Charts, Data Recording, and Packet Logs.
"""

import os
import sys
import subprocess
import csv
import time
from PyQt5.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QComboBox, QCheckBox, QTabWidget, QTableWidget, QTableWidgetItem,
    QTextEdit, QSplitter, QHeaderView, QStatusBar, QMessageBox, QFileDialog
)
from PyQt5.QtCore import Qt, pyqtSlot, QTimer
from PyQt5.QtGui import QIcon, QColor, QFont

from ble_client import BLEManager
from protocol import (
    SpO2Packet, BatteryPacket, AccelerationPacket, AlgorithmPacket, ProtocolStats,
    AckPacket, ErrorPacket, DeviceStatusPacket, RecordInfoPacket, HistoryRecordPacket, RawPPGPacket,
)
from data_recorder import DataRecorder
from ui.dashboard import Dashboard
from ui.chart_view import ChartView


class MainWindow(QMainWindow):
    def __init__(self, ble_manager: BLEManager):
        super().__init__()
        self.ble = ble_manager
        self.stats = ProtocolStats()
        self.recorder = DataRecorder(default_dir=os.path.join(os.path.dirname(os.path.dirname(__file__)), "recordings"))
        self.history_next_sequence = 0
        self._history_file = None
        self._history_writer = None

        self.setWindowTitle("SpO2-Ring BLE 上位机 (N32WB452 + IPA1322)")
        self.resize(1200, 820)
        self.setMinimumSize(960, 680)

        # Apply dark theme styling
        self._apply_theme()

        # Build UI
        self._init_ui()

        # Connect BLE Signals
        self._connect_signals()

        # GUI timer for 1Hz updates (recording duration, etc.)
        self.timer = QTimer(self)
        self.timer.timeout.connect(self._on_second_tick)
        self.timer.start(1000)

    def _apply_theme(self):
        self.setStyleSheet("""
            QMainWindow {
                background-color: #0F1218;
            }
            QWidget {
                color: #F1F5F9;
                font-family: 'Segoe UI', 'Microsoft YaHei', sans-serif;
            }
            QTabWidget::pane {
                border: 1px solid #2D3345;
                background-color: #161922;
                border-radius: 6px;
            }
            QTabBar::tab {
                background-color: #1E222D;
                color: #94A3B8;
                padding: 8px 20px;
                margin-right: 4px;
                border-top-left-radius: 4px;
                border-top-right-radius: 4px;
                font-weight: bold;
                font-size: 13px;
            }
            QTabBar::tab:selected {
                background-color: #2563EB;
                color: #FFFFFF;
            }
            QTabBar::tab:hover:!selected {
                background-color: #2A303F;
            }
            QTableWidget {
                background-color: #161922;
                alternate-background-color: #1E222D;
                border: 1px solid #2D3345;
                gridline-color: #2D3345;
                font-size: 12px;
                selection-background-color: #2563EB;
            }
            QHeaderView::section {
                background-color: #1E222D;
                color: #94A3B8;
                padding: 4px 6px;
                border: 1px solid #2D3345;
                font-weight: bold;
                font-size: 12px;
            }
            QTextEdit {
                background-color: #12141A;
                color: #CBD5E1;
                border: 1px solid #2D3345;
                border-radius: 4px;
                font-family: 'Consolas', 'Courier New', monospace;
                font-size: 12px;
            }
            QStatusBar {
                background-color: #12151D;
                color: #64748B;
                border-top: 1px solid #2D3345;
                font-size: 12px;
            }
        """)

    def _init_ui(self):
        central = QWidget(self)
        self.setCentralWidget(central)
        root_layout = QVBoxLayout(central)
        root_layout.setContentsMargins(14, 12, 14, 12)
        root_layout.setSpacing(10)

        # 1. Top BLE Control Bar
        control_bar = self._build_control_bar()
        root_layout.addLayout(control_bar)

        # 2. Device work-mode and on-device record controls
        root_layout.addLayout(self._build_device_control_bar())

        # 3. Digital Telemetry Cards (Dashboard)
        self.dashboard = Dashboard()
        root_layout.addWidget(self.dashboard)

        # 3. Main Center Area: Tabs (Waveforms vs Log/Packets)
        self.tabs = QTabWidget()

        # Tab 1: Real-time Trends Chart
        self.chart_view = ChartView()
        self.tabs.addTab(self.chart_view, "📈 实时波形趋势")

        # Tab 2: Packet Data Stream & Log
        packet_tab = self._build_packet_tab()
        self.tabs.addTab(packet_tab, "📋 原始报文与运行日志")

        root_layout.addWidget(self.tabs, stretch=1)

        # 4. Bottom Data Recording Bar
        record_bar = self._build_record_bar()
        root_layout.addLayout(record_bar)

        # 5. Status Bar
        self._build_status_bar()

    def _build_device_control_bar(self) -> QHBoxLayout:
        bar = QHBoxLayout()
        bar.setSpacing(8)
        self.combo_mode = QComboBox()
        self.combo_mode.addItem("仅原始 PPG", 0x01)
        self.combo_mode.addItem("仅算法结果", 0x02)
        self.combo_mode.addItem("原始 + 算法", 0x03)
        self.combo_mode.setCurrentIndex(1)
        self.combo_period = QComboBox()
        self.combo_period.addItem("连续 / 1s", 1)
        self.combo_period.addItem("15s 周期", 15)
        self.combo_period.addItem("30s 周期", 30)
        self.combo_period.addItem("60s 周期", 60)
        for label, widget in (("工作模式", self.combo_mode), ("采集周期", self.combo_period)):
            bar.addWidget(QLabel(label + ":")); bar.addWidget(widget)
        self.btn_apply_mode = QPushButton("应用模式")
        self.btn_device_start = QPushButton("开始设备记录")
        self.btn_device_stop = QPushButton("停止设备记录")
        self.btn_device_sync = QPushButton("同步历史")
        self.btn_device_erase = QPushButton("清空设备记录")
        for button in (self.btn_apply_mode,self.btn_device_start,self.btn_device_stop,self.btn_device_sync,self.btn_device_erase):
            button.setStyleSheet("background:#1E293B;color:#E2E8F0;border:1px solid #475569;border-radius:4px;padding:5px 10px;")
            bar.addWidget(button)
        bar.addStretch()
        self.lbl_device_info = QLabel("设备状态: 等待连接")
        self.lbl_device_info.setStyleSheet("color:#94A3B8;font-size:12px;")
        bar.addWidget(self.lbl_device_info)
        return bar

    def _build_control_bar(self) -> QHBoxLayout:
        bar = QHBoxLayout()
        bar.setSpacing(10)

        # Brand / Title
        title_box = QVBoxLayout()
        title_box.setSpacing(2)
        app_title = QLabel("SpO2-Ring 上位机")
        app_title.setStyleSheet("font-size: 16px; font-weight: 800; color: #38BDF8;")
        app_sub = QLabel("FEE7:FFF5 20B 算法流")
        app_sub.setStyleSheet("font-size: 11px; color: #64748B;")
        title_box.addWidget(app_title)
        title_box.addWidget(app_sub)
        bar.addLayout(title_box)

        bar.addSpacing(15)

        # Device Scan Dropdown
        self.combo_devices = QComboBox()
        self.combo_devices.setMinimumWidth(280)
        self.combo_devices.setStyleSheet("""
            QComboBox {
                background-color: #1E222D;
                color: #FFFFFF;
                border: 1px solid #334155;
                border-radius: 5px;
                padding: 6px 12px;
                font-size: 13px;
            }
            QComboBox::drop-down {
                border: none;
            }
        """)
        self.combo_devices.addItem("请点击扫描查找设备...", "")
        bar.addWidget(self.combo_devices)

        # Scan Button
        self.btn_scan = QPushButton("🔍 扫描设备")
        self.btn_scan.setStyleSheet("""
            QPushButton {
                background-color: #1E293B;
                color: #F8FAFC;
                border: 1px solid #475569;
                border-radius: 5px;
                padding: 6px 14px;
                font-weight: bold;
                font-size: 13px;
            }
            QPushButton:hover {
                background-color: #334155;
            }
        """)
        bar.addWidget(self.btn_scan)

        # Connect / Disconnect Button
        self.btn_connect = QPushButton("⚡ 连接戒指")
        self.btn_connect.setStyleSheet("""
            QPushButton {
                background-color: #059669;
                color: #FFFFFF;
                border: none;
                border-radius: 5px;
                padding: 6px 18px;
                font-weight: bold;
                font-size: 13px;
            }
            QPushButton:hover {
                background-color: #10B981;
            }
        """)
        bar.addWidget(self.btn_connect)

        # Auto Reconnect Checkbox
        self.chk_reconnect = QCheckBox("自动重连")
        self.chk_reconnect.setStyleSheet("color: #94A3B8; font-size: 12px;")
        bar.addWidget(self.chk_reconnect)

        # Simulation Mode Toggle
        self.btn_sim = QPushButton("▶ 模拟演示")
        self.btn_sim.setStyleSheet("""
            QPushButton {
                background-color: #312E81;
                color: #C7D2FE;
                border: 1px solid #4338CA;
                border-radius: 5px;
                padding: 6px 12px;
                font-size: 12px;
            }
            QPushButton:hover {
                background-color: #3730A3;
            }
        """)
        bar.addWidget(self.btn_sim)

        bar.addStretch()

        # Connection Status Badge
        self.lbl_status_badge = QLabel(" 未连接 ")
        self.lbl_status_badge.setStyleSheet("""
            background-color: #334155;
            color: #94A3B8;
            font-size: 12px;
            font-weight: bold;
            padding: 5px 12px;
            border-radius: 12px;
        """)
        bar.addWidget(self.lbl_status_badge)

        return bar

    def _build_packet_tab(self) -> QWidget:
        widget = QWidget()
        layout = QVBoxLayout(widget)
        layout.setContentsMargins(6, 6, 6, 6)
        layout.setSpacing(6)

        splitter = QSplitter(Qt.Vertical)

        # Top: Table of recent packets
        table_container = QWidget()
        tbl_layout = QVBoxLayout(table_container)
        tbl_layout.setContentsMargins(0, 0, 0, 0)
        tbl_layout.setSpacing(4)

        tbl_header = QHBoxLayout()
        tbl_title = QLabel("最新血氧报文 (显示近 100 帧)")
        tbl_title.setStyleSheet("color: #94A3B8; font-weight: bold; font-size: 12px;")
        tbl_header.addWidget(tbl_title)
        tbl_header.addStretch()

        self.btn_clear_table = QPushButton("清空表格")
        self.btn_clear_table.setStyleSheet("background-color: #334155; color: white; border: none; padding: 2px 8px; border-radius: 3px; font-size: 11px;")
        self.btn_clear_table.clicked.connect(self._clear_packet_table)
        tbl_header.addWidget(self.btn_clear_table)
        tbl_layout.addLayout(tbl_header)

        self.packet_table = QTableWidget()
        self.packet_table.setColumnCount(11)
        self.packet_table.setHorizontalHeaderLabels([
            "时间戳", "Seq", "有效性", "标定", "SpO2 (%)", "时域心率 (BPM)", "频域心率 (BPM)",
            "PI (%)", "R 比值", "运动状态", "原始报文 (Hex)"
        ])
        self.packet_table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeToContents)
        self.packet_table.horizontalHeader().setSectionResizeMode(10, QHeaderView.Stretch)
        self.packet_table.setAlternatingRowColors(True)
        self.packet_table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.packet_table.setSelectionBehavior(QTableWidget.SelectRows)
        tbl_layout.addWidget(self.packet_table)

        splitter.addWidget(table_container)

        # Bottom: Log viewer
        log_container = QWidget()
        log_layout = QVBoxLayout(log_container)
        log_layout.setContentsMargins(0, 0, 0, 0)
        log_layout.setSpacing(4)

        log_header = QHBoxLayout()
        log_title = QLabel("系统运行日志")
        log_title.setStyleSheet("color: #94A3B8; font-weight: bold; font-size: 12px;")
        log_header.addWidget(log_title)
        log_header.addStretch()

        self.btn_clear_log = QPushButton("清空日志")
        self.btn_clear_log.setStyleSheet("background-color: #334155; color: white; border: none; padding: 2px 8px; border-radius: 3px; font-size: 11px;")
        self.btn_clear_log.clicked.connect(lambda: self.log_text.clear())
        log_header.addWidget(self.btn_clear_log)
        log_layout.addLayout(log_header)

        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)
        log_layout.addWidget(self.log_text)

        splitter.addWidget(log_container)
        splitter.setSizes([320, 160])

        layout.addWidget(splitter)
        return widget

    def _build_record_bar(self) -> QHBoxLayout:
        bar = QHBoxLayout()
        bar.setSpacing(12)

        self.btn_record = QPushButton("🔴 开始录制数据 (CSV)")
        self.btn_record.setStyleSheet("""
            QPushButton {
                background-color: #DC2626;
                color: #FFFFFF;
                border: none;
                border-radius: 5px;
                padding: 6px 16px;
                font-weight: bold;
                font-size: 13px;
            }
            QPushButton:hover {
                background-color: #EF4444;
            }
        """)
        self.btn_record.clicked.connect(self._toggle_recording)
        bar.addWidget(self.btn_record)

        self.lbl_record_info = QLabel("录制状态: 未开始")
        self.lbl_record_info.setStyleSheet("color: #94A3B8; font-size: 12px;")
        bar.addWidget(self.lbl_record_info)

        bar.addStretch()

        self.btn_open_folder = QPushButton("📁 打开保存文件夹")
        self.btn_open_folder.setStyleSheet("""
            QPushButton {
                background-color: #1E293B;
                color: #E2E8F0;
                border: 1px solid #475569;
                border-radius: 5px;
                padding: 5px 12px;
                font-size: 12px;
            }
            QPushButton:hover {
                background-color: #334155;
            }
        """)
        self.btn_open_folder.clicked.connect(self._open_recording_folder)
        bar.addWidget(self.btn_open_folder)

        return bar

    def _build_status_bar(self):
        sb = self.statusBar()
        self.status_lbl_packets = QLabel("总接收: 0 帧")
        self.status_lbl_fps = QLabel("速率: 0.0 Hz")
        self.status_lbl_loss = QLabel("丢包率: 0.0%")
        self.status_lbl_valid = QLabel("有效率: 0.0%")
        self.status_lbl_notify = QLabel("Notify: 未订阅")

        for lbl in [self.status_lbl_packets, self.status_lbl_fps, self.status_lbl_loss, self.status_lbl_valid, self.status_lbl_notify]:
            lbl.setStyleSheet("padding: 0 10px; color: #94A3B8;")
            sb.addPermanentWidget(lbl)

    def _connect_signals(self):
        self.ble.device_discovered.connect(self._on_device_discovered)
        self.ble.scan_finished.connect(self._on_scan_finished)
        self.ble.connection_status_changed.connect(self._on_connection_status_changed)
        self.ble.packet_received.connect(self._on_packet_received)
        self.ble.battery_received.connect(self._on_battery_received)
        self.ble.acceleration_received.connect(self._on_acceleration_received)
        self.ble.algorithm_received.connect(self._on_algorithm_received)
        self.ble.log_message.connect(self._append_log)
        self.ble.notify_state_changed.connect(self._on_notify_state_changed)
        self.ble.ack_received.connect(self._on_ack_received)
        self.ble.error_received.connect(self._on_error_received)
        self.ble.device_status_received.connect(self._on_device_status)
        self.ble.record_info_received.connect(self._on_record_info)
        self.ble.history_received.connect(self._on_history_record)
        self.ble.raw_received.connect(self._on_raw_received)

        self.chk_reconnect.toggled.connect(lambda checked: setattr(self.ble, "auto_reconnect", checked))

    def _on_device_discovered(self, name: str, address: str, rssi: int):
        display_text = f"{name} ({address}) [{rssi} dBm]"
        # Check if already added
        for i in range(self.combo_devices.count()):
            if self.combo_devices.itemData(i) == address:
                self.combo_devices.setItemText(i, display_text)
                return

        # Auto select SpO2-Ring if found
        is_target = ("spo2" in name.lower()) or ("ring" in name.lower()) or ("33:22:33:db:30:a0" in address.lower())
        self.combo_devices.addItem(display_text, address)
        if is_target:
            self.combo_devices.setCurrentIndex(self.combo_devices.count() - 1)
            self._append_log(f"⭐ 发现目标血氧戒指设备: {display_text}", "success")

    def _on_scan_finished(self, count: int):
        self.btn_scan.setEnabled(True)
        self.btn_scan.setText("🔍 扫描设备")

    def _on_connection_status_changed(self, state: str, message: str):
        if state == "connected":
            self.btn_connect.setText("🔌 断开连接")
            self.btn_connect.setStyleSheet("""
                background-color: #DC2626;
                color: #FFFFFF;
                border: none;
                border-radius: 5px;
                padding: 6px 18px;
                font-weight: bold;
                font-size: 13px;
            """)
            self.lbl_status_badge.setText(" 已连接 ")
            self.lbl_status_badge.setStyleSheet("""
                background-color: #059669;
                color: #FFFFFF;
                font-size: 12px;
                font-weight: bold;
                padding: 5px 12px;
                border-radius: 12px;
            """)
        elif state == "connecting":
            self.btn_connect.setText("连接中...")
            self.lbl_status_badge.setText(" 连接中 ")
            self.lbl_status_badge.setStyleSheet("""
                background-color: #D97706;
                color: #FFFFFF;
                font-size: 12px;
                font-weight: bold;
                padding: 5px 12px;
                border-radius: 12px;
            """)
        else: # disconnected
            self.btn_connect.setText("⚡ 连接戒指")
            self.btn_connect.setStyleSheet("""
                background-color: #059669;
                color: #FFFFFF;
                border: none;
                border-radius: 5px;
                padding: 6px 18px;
                font-weight: bold;
                font-size: 13px;
            """)
            self.lbl_status_badge.setText(" 未连接 ")
            self.lbl_status_badge.setStyleSheet("""
                background-color: #334155;
                color: #94A3B8;
                font-size: 12px;
                font-weight: bold;
                padding: 5px 12px;
                border-radius: 12px;
            """)
            self.status_lbl_notify.setText("Notify: 未订阅")

    def _on_notify_state_changed(self, is_notifying: bool, uuid: str):
        if is_notifying:
            short_uuid = uuid.split("-")[0] if "-" in uuid else uuid
            self.status_lbl_notify.setText(f"Notify: 活跃 ({short_uuid})")
            self.status_lbl_notify.setStyleSheet("padding: 0 10px; color: #10B981; font-weight: bold;")
        else:
            self.status_lbl_notify.setText("Notify: 未订阅")
            self.status_lbl_notify.setStyleSheet("padding: 0 10px; color: #94A3B8;")

    def _on_packet_received(self, pkt: SpO2Packet):
        # 1. Update stats
        self.stats.update(pkt)

        # 2. Update telemetry cards
        self.dashboard.update_telemetry(pkt, self.stats)

        # 3. Update real-time curves
        self.chart_view.append_data(pkt)

        # 4. Record to CSV if active
        if self.recorder.is_recording:
            self.recorder.write(pkt)

        # 5. Insert row in packet table
        self._add_packet_table_row(pkt)

        # 6. Update status bar metrics
        self.status_lbl_packets.setText(f"总接收: {self.stats.total_packets} 帧")
        self.status_lbl_fps.setText(f"速率: {self.stats.fps:.1f} Hz")
        self.status_lbl_loss.setText(f"丢包率: {self.stats.loss_rate:.1f}%")
        self.status_lbl_valid.setText(f"有效率: {self.stats.valid_rate:.1f}%")

    def _on_battery_received(self, pkt: BatteryPacket):
        self.dashboard.update_battery(pkt)
        if self.recorder.is_recording:
            self.recorder.write_battery(pkt)

    def _on_acceleration_received(self, pkt: AccelerationPacket):
        self.dashboard.update_acceleration(pkt)

    def _on_algorithm_received(self, pkt: AlgorithmPacket):
        self.dashboard.update_algorithms(pkt)

    def _on_raw_received(self, pkt: RawPPGPacket):
        self.chart_view.append_raw(pkt)

    def _on_ack_received(self, pkt: AckPacket):
        event_names = {1:"正在擦除",2:"记录区就绪",3:"记录已开始",4:"记录已停止",5:"记录已清空",6:"Flash已满",7:"同步结束"}
        if pkt.event:
            name = event_names.get(pkt.event, f"事件{pkt.event}")
            self._append_log(f"设备事件: {name}，值={pkt.event_value}", "success" if pkt.event not in (1,6) else "warn")
            if pkt.event == 7:
                self._close_history_export()
        else:
            self._append_log(f"命令 0x{pkt.command:02X} 已确认", "success")

    def _on_error_received(self, pkt: ErrorPacket):
        self._append_log(f"设备拒绝命令 0x{pkt.command:02X}，错误码={pkt.error}", "error")

    def _on_device_status(self, pkt: DeviceStatusPacket):
        rtc = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(pkt.device_time)) if pkt.device_time else "未同步"
        self.lbl_device_info.setText(f"状态={pkt.state} 模式={pkt.output_mode} 周期={pkt.sample_period}s RTC={rtc} 错误={pkt.error}")
        mode_index = self.combo_mode.findData(pkt.output_mode)
        period_index = self.combo_period.findData(pkt.sample_period)
        if mode_index >= 0: self.combo_mode.setCurrentIndex(mode_index)
        if period_index >= 0: self.combo_period.setCurrentIndex(period_index)

    def _on_record_info(self, pkt: RecordInfoPacket):
        percent = (100.0 * pkt.flash_used / pkt.flash_total) if pkt.flash_total else 0.0
        self.history_next_sequence = min(self.history_next_sequence, pkt.record_count)
        self.lbl_device_info.setText(f"记录 {pkt.record_count} 条 | Session {pkt.session_id} | Flash {percent:.1f}%")
        self._append_log(f"设备记录信息: {pkt.record_count} 条，Flash {pkt.flash_used}/{pkt.flash_total} B", "info")

    def start_history_export(self):
        os.makedirs(self.recorder.output_dir, exist_ok=True)
        path = os.path.join(self.recorder.output_dir, f"SpO2_Ring_History_{time.strftime('%Y%m%d_%H%M%S')}.csv")
        self._history_file = open(path, "w", newline="", encoding="utf-8-sig")
        self._history_writer = csv.writer(self._history_file)
        self._history_writer.writerow(["Timestamp","DateTime","SpO2","HR","Motion","Quality","Temperature","Sequence","Flags"])
        self.history_next_sequence = 0
        self._append_log(f"历史同步保存至: {path}", "success")
        return path

    def _on_history_record(self, pkt: HistoryRecordPacket):
        if self._history_writer is None: self.start_history_export()
        dt = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(pkt.timestamp))
        self._history_writer.writerow([pkt.timestamp,dt,pkt.spo2,pkt.heart_rate,"Moving" if pkt.motion else "Static",pkt.quality,f"{pkt.temperature:.2f}",pkt.sequence,pkt.flags])
        self.history_next_sequence = pkt.sequence + 1
        if self.history_next_sequence % 20 == 0: self._history_file.flush()
        self.lbl_device_info.setText(f"历史同步中: 已接收 {self.history_next_sequence} 条")

    def _close_history_export(self):
        if self._history_file:
            self._history_file.flush(); self._history_file.close()
            self._history_file = None; self._history_writer = None

    def _add_packet_table_row(self, pkt: SpO2Packet):
        # Limit rows to 100
        if self.packet_table.rowCount() >= 100:
            self.packet_table.removeRow(0)

        row = self.packet_table.rowCount()
        self.packet_table.insertRow(row)

        valid_str = "有效" if pkt.valid else "无效"
        calib_str = "已标定" if pkt.calibrated else "未标定"

        items = [
            pkt.time_str,
            str(pkt.seq),
            valid_str,
            calib_str,
            f"{round(pkt.spo2)}",
            f"{round(pkt.hr_time)}",
            f"{round(pkt.hr_fft)}",
            f"{pkt.pi:.2f}",
            f"{pkt.ratio:.4f}",
            "运动" if pkt.moving else "静止",
            pkt.raw_hex
        ]

        for col, text in enumerate(items):
            item = QTableWidgetItem(text)
            item.setTextAlignment(Qt.AlignCenter if col < 10 else Qt.AlignLeft | Qt.AlignVCenter)
            if col == 2:
                item.setForeground(QColor("#00E676") if pkt.valid else QColor("#FF5252"))
            elif col == 4 and pkt.valid:
                item.setForeground(QColor("#00E676") if pkt.spo2 >= 95 else QColor("#FF9100"))
            elif col == 5 and pkt.valid:
                item.setForeground(QColor("#FF5252"))
            elif col == 6 and pkt.valid:
                item.setForeground(QColor("#42A5F5"))
            self.packet_table.setItem(row, col, item)

        self.packet_table.scrollToBottom()

    def _clear_packet_table(self):
        self.packet_table.setRowCount(0)

    def _append_log(self, text: str, level: str = "info"):
        color_map = {
            "info": "#94A3B8",
            "warn": "#FBBF24",
            "error": "#F87171",
            "success": "#34D399"
        }
        color = color_map.get(level, "#CBD5E1")
        import time
        t_str = time.strftime("%H:%M:%S")
        html_msg = f"<span style='color: #64748B;'>[{t_str}]</span> <span style='color: {color};'>{text}</span>"
        self.log_text.append(html_msg)

    def _toggle_recording(self):
        if not self.recorder.is_recording:
            filepath = self.recorder.start()
            self.btn_record.setText("⏹ 停止录制")
            self.btn_record.setStyleSheet("""
                background-color: #334155;
                color: #FFFFFF;
                border: 1px solid #64748B;
                border-radius: 5px;
                padding: 6px 16px;
                font-weight: bold;
                font-size: 13px;
            """)
            self.lbl_record_info.setText(f"录制中: 0 帧 | 文件: {os.path.basename(filepath)}")
            self._append_log(f"已启动数据录制，输出至: {filepath}", "success")
        else:
            filepath = self.recorder.stop()
            self.btn_record.setText("🔴 开始录制数据 (CSV)")
            self.btn_record.setStyleSheet("""
                background-color: #DC2626;
                color: #FFFFFF;
                border: none;
                border-radius: 5px;
                padding: 6px 16px;
                font-weight: bold;
                font-size: 13px;
            """)
            self.lbl_record_info.setText("录制已保存")
            self._append_log(f"数据录制已停止，文件已安全保存: {filepath}", "info")

    def _open_recording_folder(self):
        folder = self.recorder.output_dir
        os.makedirs(folder, exist_ok=True)
        if sys.platform == "win32":
            os.startfile(folder)
        else:
            subprocess.Popen(["xdg-open", folder])

    def _on_second_tick(self):
        if self.recorder.is_recording:
            dur = int(self.recorder.duration)
            m, s = divmod(dur, 60)
            h, m = divmod(m, 60)
            time_str = f"{h:02d}:{m:02d}:{s:02d}"
            cnt = self.recorder.recorded_count
            self.lbl_record_info.setText(f"录制中: {cnt} 帧 | 时长: {time_str}")
