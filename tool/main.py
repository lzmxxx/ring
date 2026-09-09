"""
SpO2-Ring BLE 上位机 (N32WB452 + IPA1322)
Application Entry Point with QAsync Event Loop Integration.
"""

import sys
import os
import asyncio
from PyQt5.QtWidgets import QApplication, QMessageBox
from PyQt5.QtCore import Qt
from qasync import QEventLoop

# Add tool directory to module path
current_dir = os.path.dirname(os.path.abspath(__file__))
if current_dir not in sys.path:
    sys.path.insert(0, current_dir)

from ble_client import BLEManager
from ui.main_window import MainWindow
from protocol import (
    CMD_SET_OUTPUT_MODE, CMD_SET_SAMPLE_PERIOD, CMD_START_RECORD,
    CMD_STOP_RECORD, CMD_SYNC_RECORD, CMD_ERASE_RECORD, CMD_GET_RECORD_INFO,
)
import struct


def main():
    # Enable High DPI scaling
    QApplication.setAttribute(Qt.AA_EnableHighDpiScaling, True)
    QApplication.setAttribute(Qt.AA_UseHighDpiPixmaps, True)

    app = QApplication(sys.argv)
    app.setApplicationName("SpO2-Ring Host Computer")
    app.setQuitOnLastWindowClosed(True)

    loop = QEventLoop(app)
    asyncio.set_event_loop(loop)

    ble_manager = BLEManager()
    window = MainWindow(ble_manager)

    # Wire asynchronous actions to UI buttons via direct task scheduling
    def on_scan_clicked():
        window.btn_scan.setEnabled(False)
        window.btn_scan.setText("扫描中...")
        loop.create_task(ble_manager.start_scan(timeout=4.0))

    def on_connect_clicked():
        if ble_manager.is_connected:
            loop.create_task(ble_manager.disconnect())
        else:
            selected_addr = window.combo_devices.currentData()
            if not selected_addr:
                text = window.combo_devices.currentText()
                if "(" in text and ")" in text:
                    selected_addr = text.split("(")[1].split(")")[0].strip()
                elif text and not text.startswith("请点击"):
                    selected_addr = text.strip()

            if not selected_addr:
                window._append_log("请先点击[扫描设备]或在下拉框中选择目标戒指！", "warn")
                return

            loop.create_task(ble_manager.connect(selected_addr))

    def on_sim_clicked():
        if ble_manager.is_simulation:
            loop.create_task(ble_manager.stop_simulation())
            window.btn_sim.setText("▶ 模拟演示")
            window.btn_sim.setStyleSheet("""
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
        else:
            loop.create_task(ble_manager.start_simulation())
            window.btn_sim.setText("⏹ 停止模拟")
            window.btn_sim.setStyleSheet("""
                QPushButton {
                    background-color: #4C1D95;
                    color: #FDF2F8;
                    border: 1px solid #7C3AED;
                    border-radius: 5px;
                    padding: 6px 12px;
                    font-size: 12px;
                }
                QPushButton:hover {
                    background-color: #5B21B6;
                }
            """)

    window.btn_scan.clicked.connect(lambda checked=False: on_scan_clicked())
    window.btn_connect.clicked.connect(lambda checked=False: on_connect_clicked())
    window.btn_sim.clicked.connect(lambda checked=False: on_sim_clicked())

    async def apply_device_mode():
        await ble_manager.send_command(CMD_SET_OUTPUT_MODE, bytes((window.combo_mode.currentData(),)))
        await ble_manager.send_command(CMD_SET_SAMPLE_PERIOD, struct.pack('<H', window.combo_period.currentData()))

    async def start_device_record():
        await ble_manager.sync_time()
        await ble_manager.send_command(CMD_START_RECORD)

    async def sync_device_history():
        if window._history_writer is None:
            window.start_history_export()
        await ble_manager.send_command(CMD_GET_RECORD_INFO)
        await ble_manager.send_command(CMD_SYNC_RECORD, struct.pack('<H', window.history_next_sequence & 0xFFFF))

    window.btn_apply_mode.clicked.connect(lambda checked=False: loop.create_task(apply_device_mode()))
    window.btn_device_start.clicked.connect(lambda checked=False: loop.create_task(start_device_record()) if QMessageBox.question(window,"开始设备记录","开始新的记录将清除设备中的历史数据，是否继续？",QMessageBox.Yes|QMessageBox.No)==QMessageBox.Yes else None)
    window.btn_device_stop.clicked.connect(lambda checked=False: loop.create_task(ble_manager.send_command(CMD_STOP_RECORD)))
    window.btn_device_sync.clicked.connect(lambda checked=False: loop.create_task(sync_device_history()))
    window.btn_device_erase.clicked.connect(lambda checked=False: loop.create_task(ble_manager.send_command(CMD_ERASE_RECORD)) if QMessageBox.question(window,"清空设备记录","此操作不可恢复，是否清空？",QMessageBox.Yes|QMessageBox.No)==QMessageBox.Yes else None)

    # Clean cleanup on window close
    def close_event_handler(event):
        if ble_manager.is_connected:
            loop.create_task(ble_manager.disconnect())
        if window.recorder.is_recording:
            window.recorder.stop()
        window._close_history_export()
        loop.stop()
        event.accept()

    window.closeEvent = close_event_handler
    window.show()

    with loop:
        loop.run_forever()


if __name__ == "__main__":
    main()
