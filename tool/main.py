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

    # Clean cleanup on window close
    def close_event_handler(event):
        if ble_manager.is_connected:
            loop.create_task(ble_manager.disconnect())
        if window.recorder.is_recording:
            window.recorder.stop()
        loop.stop()
        event.accept()

    window.closeEvent = close_event_handler
    window.show()

    with loop:
        loop.run_forever()


if __name__ == "__main__":
    main()
