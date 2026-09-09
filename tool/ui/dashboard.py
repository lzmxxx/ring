"""
Dashboard Widget containing Telemetry Metric Cards for SpO2, HR, PI, R, and Status Badges.
"""

from PyQt5.QtWidgets import (
    QWidget, QHBoxLayout, QVBoxLayout, QLabel, QFrame, QGridLayout
)
from PyQt5.QtCore import Qt
from PyQt5.QtGui import QFont

from protocol import SpO2Packet, BatteryPacket, AccelerationPacket, AlgorithmPacket, ProtocolStats


class MetricCard(QFrame):
    """A sleek card displaying a primary metric, unit, and auxiliary subtitle."""
    def __init__(self, title: str, unit: str = "", primary_color: str = "#00E5FF", parent=None):
        super().__init__(parent)
        self.setFrameShape(QFrame.StyledPanel)
        self.setStyleSheet(f"""
            QFrame {{
                background-color: #1E222D;
                border: 1px solid #2D3345;
                border-radius: 8px;
            }}
            QFrame:hover {{
                border: 1px solid {primary_color};
            }}
        """)
        self.primary_color = primary_color

        layout = QVBoxLayout(self)
        layout.setContentsMargins(14, 10, 14, 10)
        layout.setSpacing(4)

        # Title Label
        self.title_lbl = QLabel(title)
        self.title_lbl.setStyleSheet("color: #8C9BAE; font-size: 13px; font-weight: bold;")
        layout.addWidget(self.title_lbl)

        # Value & Unit Row
        val_row = QHBoxLayout()
        val_row.setSpacing(4)
        self.val_lbl = QLabel("--")
        self.val_lbl.setStyleSheet(f"color: {primary_color}; font-size: 36px; font-weight: 800; font-family: 'Segoe UI', Arial;")
        val_row.addWidget(self.val_lbl)

        self.unit_lbl = QLabel(unit)
        self.unit_lbl.setStyleSheet("color: #7E8B9B; font-size: 14px; font-weight: 600; padding-top: 12px;")
        val_row.addWidget(self.unit_lbl)
        val_row.addStretch()
        layout.addLayout(val_row)

        # Subtitle / Details Label
        self.sub_lbl = QLabel("等待数据...")
        self.sub_lbl.setStyleSheet("color: #64748B; font-size: 12px;")
        layout.addWidget(self.sub_lbl)

    def set_value(self, val_str: str, sub_str: str = "", color: str = None):
        self.val_lbl.setText(val_str)
        if sub_str:
            self.sub_lbl.setText(sub_str)
        if color:
            self.val_lbl.setStyleSheet(f"color: {color}; font-size: 36px; font-weight: 800; font-family: 'Segoe UI', Arial;")


class Dashboard(QWidget):
    """Telemetry cards for SpO2, heart rate, PI/R, battery and status."""
    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QGridLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(12)

        # 1. SpO2 Card
        self.card_spo2 = MetricCard("血氧饱和度 (SpO2)", "%", "#00E676")
        layout.addWidget(self.card_spo2, 0, 0)

        # 2. Heart Rate Card (Time Domain only)
        self.card_hr = MetricCard("时域心率 (HR)", "BPM", "#FF5252")
        layout.addWidget(self.card_hr, 0, 1)

        # 3. PI & R Card
        self.card_pi = MetricCard("灌注指数 (PI) / 比值 (R)", "%", "#FFD600")
        layout.addWidget(self.card_pi, 0, 2)

        self.card_battery = MetricCard("电池状态", "%", "#7CFF6B")
        layout.addWidget(self.card_battery, 0, 3)

        self.card_accel = MetricCard("三轴加速度", "mg", "#B388FF")
        layout.addWidget(self.card_accel, 1, 0, 1, 2)

        # 5. Status & Diagnostics Card
        self.card_status = MetricCard("算法质量与状态", "", "#00B0FF")
        layout.addWidget(self.card_status, 1, 2, 1, 2)

    def update_telemetry(self, pkt: SpO2Packet, stats: ProtocolStats):
        """Update cards when new packet arrives."""
        # 1. SpO2
        if pkt.valid:
            spo2_val = f"{round(pkt.spo2)}"
            color = "#00E676" if pkt.spo2 >= 95.0 else ("#FF9100" if pkt.spo2 >= 90.0 else "#FF1744")
            sub_text = "正常状态" if pkt.spo2 >= 95.0 else ("偏低提醒" if pkt.spo2 >= 90.0 else "低血氧告警")
        else:
            spo2_val = f"{round(pkt.spo2)}*"
            color = "#78909C"
            sub_text = "算法窗正在收敛/运动伪影"
        self.card_spo2.set_value(spo2_val, sub_text, color)

        # 2. Heart Rate (Time Domain primary, Frequency Domain in Subtitle, Rounded Integer)
        hr_val = f"{round(pkt.hr_time)}"
        hr_sub = f"频域 FFT: {round(pkt.hr_fft)} BPM" if pkt.valid else "时域/频域计算中..."
        self.card_hr.set_value(hr_val, hr_sub, "#FF5252" if pkt.valid else "#78909C")

        # 3. PI & Ratio R
        pi_val = f"{pkt.pi:.2f}"
        r_sub = f"R 比值: {pkt.ratio:.4f}  (标准: ~0.4-1.2)"
        self.card_pi.set_value(pi_val, r_sub, "#FFD600")

        # 4. Status
        valid_tag = "有效 [VALID]" if pkt.valid else "无效 [INVALID]"
        calib_tag = "已标定" if pkt.calibrated else "工程未标定(联调)"
        status_val = valid_tag
        status_color = "#00E676" if pkt.valid else "#FF1744"
        motion_tag = "运动" if pkt.moving else "静止"
        diag_sub = f"{calib_tag} | {motion_tag} | 帧号: {pkt.seq} | 速率: {stats.fps:.1f}Hz | 丢包率: {stats.loss_rate:.1f}%"
        self.card_status.set_value(status_val, diag_sub, status_color)

    def update_battery(self, pkt: BatteryPacket):
        if pkt.valid:
            color = "#7CFF6B" if pkt.capacity >= 20 else "#FF9100"
            self.card_battery.set_value(str(pkt.capacity), f"电压: {pkt.voltage_mv / 1000:.3f} V", color)
        else:
            detail = (f"CW异常 ADDR=0x{pkt.cw_address:02X} ACK={pkt.cw_ack} "
                      f"VER=0x{pkt.cw_version:02X} DEV={pkt.i2c_device_count}")
            self.card_battery.set_value("--", detail, "#FF1744")

    def update_acceleration(self, pkt: AccelerationPacket):
        if pkt.valid:
            magnitude = (pkt.x_mg ** 2 + pkt.y_mg ** 2 + pkt.z_mg ** 2) ** 0.5
            value = f"{pkt.x_mg}, {pkt.y_mg}, {pkt.z_mg}"
            self.card_accel.set_value(value, f"X, Y, Z | 合加速度: {magnitude:.0f} mg", "#B388FF")
        else:
            self.card_accel.set_value("--", "SC7A20读取异常", "#FF1744")

    def update_algorithms(self, pkt: AlgorithmPacket):
        """显示MCU独立计算的时域、FFT和DST血氧，不做融合。"""
        def shown(value, valid):
            return f"{value:.2f}%" if valid else f"{value:.2f}%*"
        self.card_spo2.set_value(
            shown(pkt.spo2_time, pkt.time_valid),
            f"FFT {shown(pkt.spo2_fft, pkt.fft_valid)} | "
            f"DST {shown(pkt.spo2_dst, pkt.dst_valid)} | 逐搏N={pkt.pair_count}",
            "#00E676" if pkt.time_valid else "#78909C",
        )
        self.card_pi.set_value(
            f"{pkt.ratio_time:.4f}",
            f"R时域 {pkt.ratio_time:.4f} | FFT {pkt.ratio_fft:.4f} | DST {pkt.ratio_dst:.4f}",
            "#FFD600",
        )

    def reset_display(self):
        self.card_spo2.set_value("--", "等待数据...", "#00E676")
        self.card_hr.set_value("--", "等待数据...", "#FF5252")
        self.card_pi.set_value("--", "R 比值: --", "#FFD600")
        self.card_battery.set_value("--", "等待电量数据...", "#7CFF6B")
        self.card_accel.set_value("--", "等待三轴数据...", "#B388FF")
        self.card_status.set_value("待机", "未连接设备", "#00B0FF")
