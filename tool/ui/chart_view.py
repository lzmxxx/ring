"""
High-Performance Real-Time Chart View based on PyQtGraph.
Visualizes SpO2, Heart Rate (Total, Time-domain, FFT), PI, and R Ratio trends.
"""

from collections import deque
from PyQt5.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QPushButton, QComboBox, QLabel
from PyQt5.QtCore import Qt
import pyqtgraph as pg

from protocol import SpO2Packet, RawPPGPacket


class ChartView(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)

        # Configure PyQtGraph global options for dark theme
        pg.setConfigOptions(antialias=True, background="#161922", foreground="#94A3B8")

        self.max_points = 120  # Default ~2 minutes at 1Hz
        self.is_paused = False

        # Data buffers
        self.x_data = deque(maxlen=600)
        self.spo2_data = deque(maxlen=600)
        self.hr_data = deque(maxlen=600)
        self.hr_time_data = deque(maxlen=600)
        self.hr_fft_data = deque(maxlen=600)
        self.pi_data = deque(maxlen=600)
        self.ratio_data = deque(maxlen=600)
        self.raw_x = deque(maxlen=1500)
        self.raw_red = deque(maxlen=1500)
        self.raw_ir = deque(maxlen=1500)

        self._init_ui()

    def _init_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(6)

        # Toolbar above plots
        tool_bar = QHBoxLayout()
        tool_bar.setSpacing(10)

        lbl_win = QLabel("趋势时间窗:")
        lbl_win.setStyleSheet("color: #94A3B8; font-size: 12px; font-weight: bold;")
        tool_bar.addWidget(lbl_win)

        self.combo_window = QComboBox()
        self.combo_window.addItems(["30 点 (~30s)", "60 点 (~1min)", "120 点 (~2min)", "300 点 (~5min)", "全部数据"])
        self.combo_window.setCurrentIndex(2)
        self.combo_window.currentIndexChanged.connect(self._on_window_changed)
        self.combo_window.setStyleSheet("""
            QComboBox {
                background-color: #1E222D;
                color: #F8FAFC;
                border: 1px solid #334155;
                border-radius: 4px;
                padding: 3px 8px;
                font-size: 12px;
            }
        """)
        tool_bar.addWidget(self.combo_window)

        self.btn_pause = QPushButton("暂停波形")
        self.btn_pause.setStyleSheet("""
            QPushButton {
                background-color: #334155;
                color: #FFFFFF;
                border: none;
                border-radius: 4px;
                padding: 4px 12px;
                font-size: 12px;
            }
            QPushButton:hover {
                background-color: #475569;
            }
        """)
        self.btn_pause.clicked.connect(self._toggle_pause)
        tool_bar.addWidget(self.btn_pause)

        self.btn_clear = QPushButton("清空曲线")
        self.btn_clear.setStyleSheet("""
            QPushButton {
                background-color: #334155;
                color: #FFFFFF;
                border: none;
                border-radius: 4px;
                padding: 4px 12px;
                font-size: 12px;
            }
            QPushButton:hover {
                background-color: #475569;
            }
        """)
        self.btn_clear.clicked.connect(self.clear_data)
        tool_bar.addWidget(self.btn_clear)

        tool_bar.addStretch()
        layout.addLayout(tool_bar)

        # Plot Graphics Layout Widget
        self.win = pg.GraphicsLayoutWidget()
        self.win.setStyleSheet("border: 1px solid #2D3345; border-radius: 6px;")
        layout.addWidget(self.win)

        # --- PLOT 0: Raw dual-channel PPG ---
        self.plot_raw = self.win.addPlot(row=0, col=0)
        self.plot_raw.setTitle("<span style='color:#38BDF8;font-size:13px;font-weight:bold;'>原始 PPG（660nm / 905nm）</span>")
        self.plot_raw.showGrid(x=True, y=True, alpha=0.25)
        self.plot_raw.addLegend(offset=(10, 10))
        self.curve_raw_red = self.plot_raw.plot(pen=pg.mkPen("#EF4444", width=1.2), name="660nm")
        self.curve_raw_ir = self.plot_raw.plot(pen=pg.mkPen("#38BDF8", width=1.2), name="905nm")

        # --- PLOT 1: SpO2 Trend ---
        self.plot_spo2 = self.win.addPlot(row=1, col=0)
        self.plot_spo2.setTitle("<span style='color: #00E676; font-size: 13px; font-weight: bold;'>血氧饱和度 SpO2 (%)</span>")
        self.plot_spo2.showGrid(x=True, y=True, alpha=0.25)
        self.plot_spo2.setYRange(85, 101, padding=0)
        self.plot_spo2.getAxis("left").setLabel("SpO2", units="%")

        # Threshold guide lines
        line_95 = pg.InfiniteLine(pos=95, angle=0, pen=pg.mkPen("#4CAF50", width=1, style=Qt.DashLine))
        line_90 = pg.InfiniteLine(pos=90, angle=0, pen=pg.mkPen("#FF5252", width=1, style=Qt.DashLine))
        self.plot_spo2.addItem(line_95)
        self.plot_spo2.addItem(line_90)

        self.curve_spo2 = self.plot_spo2.plot(
            pen=pg.mkPen(color="#00E676", width=2.5),
            symbol='o',
            symbolSize=4,
            symbolBrush='#00E676',
            name="SpO2"
        )

        # --- PLOT 2: Time-Domain & Frequency-Domain Heart Rate ---
        self.plot_hr = self.win.addPlot(row=2, col=0)
        self.plot_hr.setTitle("<span style='color: #FF5252; font-size: 13px; font-weight: bold;'>心率 HR</span>")
        self.plot_hr.showGrid(x=True, y=True, alpha=0.25)
        self.plot_hr.setYRange(40, 160, padding=0.1)
        self.plot_hr.getAxis("left").setLabel("心率", units="BPM")
        self.plot_hr.setXLink(self.plot_spo2)  # Synchronize X zoom and pan

        self.plot_hr.addLegend(offset=(10, 10))
        self.curve_hr_time = self.plot_hr.plot(
            pen=pg.mkPen(color="#FF5252", width=2.5),
            symbol='o',
            symbolSize=4,
            symbolBrush='#FF5252',
            name="HR"
        )
        self.curve_hr_fft = self.plot_hr.plot(
            pen=pg.mkPen(color="#42A5F5", width=2.0, style=Qt.DashLine),
            symbol='s',
            symbolSize=3,
            symbolBrush='#42A5F5',
            name="HR_fft (频域 FFT)"
        )

        # --- PLOT 3: PI & R Ratio ---
        self.plot_pi_r = self.win.addPlot(row=3, col=0)
        self.plot_pi_r.setTitle("<span style='color: #FFD600; font-size: 13px; font-weight: bold;'>灌注指数 PI (%) 与 光路比值 R</span>")
        self.plot_pi_r.showGrid(x=True, y=True, alpha=0.25)
        self.plot_pi_r.getAxis("left").setLabel("PI / Ratio")
        self.plot_pi_r.setXLink(self.plot_spo2)

        self.plot_pi_r.addLegend(offset=(10, 10))
        self.curve_pi = self.plot_pi_r.plot(
            pen=pg.mkPen(color="#FFD600", width=2.0),
            name="PI (%)"
        )
        self.curve_ratio = self.plot_pi_r.plot(
            pen=pg.mkPen(color="#64FFDA", width=2.0),
            name="Ratio (R)"
        )

    def append_data(self, pkt: SpO2Packet):
        """Append packet data and update curves."""
        if self.is_paused:
            return

        # Use sequence or relative index for X axis
        x_val = pkt.seq if pkt.seq else (len(self.x_data) + 1)
        self.x_data.append(x_val)
        self.spo2_data.append(round(pkt.spo2))
        self.hr_data.append(round(pkt.hr))
        self.hr_time_data.append(round(pkt.hr))
        self.hr_fft_data.append(round(pkt.hr_fft) if pkt.hr_fft else float('nan'))
        self.pi_data.append(pkt.pi)
        self.ratio_data.append(pkt.ratio)

        self._refresh_plots()

    def append_raw(self, pkt: RawPPGPacket):
        if self.is_paused:
            return
        for index, (red, ir) in enumerate(pkt.samples):
            self.raw_x.append(pkt.first_seq + index)
            self.raw_red.append(red)
            self.raw_ir.append(ir)
        self.curve_raw_red.setData(list(self.raw_x), list(self.raw_red))
        self.curve_raw_ir.setData(list(self.raw_x), list(self.raw_ir))

    def _refresh_plots(self):
        if not self.x_data:
            return

        # Slice data for the selected display window
        pts = self.max_points
        if pts is None or pts >= len(self.x_data):
            xs = list(self.x_data)
            spo2_y = list(self.spo2_data)
            hr_y = list(self.hr_data)
            hr_t_y = list(self.hr_time_data)
            hr_f_y = list(self.hr_fft_data)
            pi_y = list(self.pi_data)
            r_y = list(self.ratio_data)
        else:
            xs = list(self.x_data)[-pts:]
            spo2_y = list(self.spo2_data)[-pts:]
            hr_y = list(self.hr_data)[-pts:]
            hr_t_y = list(self.hr_time_data)[-pts:]
            hr_f_y = list(self.hr_fft_data)[-pts:]
            pi_y = list(self.pi_data)[-pts:]
            r_y = list(self.ratio_data)[-pts:]

        self.curve_spo2.setData(xs, spo2_y)
        self.curve_hr_time.setData(xs, hr_t_y)
        self.curve_hr_fft.setData(xs, hr_f_y)
        self.curve_pi.setData(xs, pi_y)
        self.curve_ratio.setData(xs, r_y)

    def _on_window_changed(self, idx: int):
        mapping = [30, 60, 120, 300, None]
        self.max_points = mapping[idx]
        self._refresh_plots()

    def _toggle_pause(self):
        self.is_paused = not self.is_paused
        if self.is_paused:
            self.btn_pause.setText("继续波形")
            self.btn_pause.setStyleSheet("background-color: #0284C7; color: white; border-radius: 4px; padding: 4px 12px;")
        else:
            self.btn_pause.setText("暂停波形")
            self.btn_pause.setStyleSheet("background-color: #334155; color: white; border-radius: 4px; padding: 4px 12px;")

    def clear_data(self):
        self.x_data.clear()
        self.spo2_data.clear()
        self.hr_data.clear()
        self.hr_time_data.clear()
        self.hr_fft_data.clear()
        self.pi_data.clear()
        self.ratio_data.clear()
        self.raw_x.clear()
        self.raw_red.clear()
        self.raw_ir.clear()
        self.curve_spo2.clear()
        self.curve_hr_time.clear()
        self.curve_hr_fft.clear()
        self.curve_pi.clear()
        self.curve_ratio.clear()
        self.curve_raw_red.clear()
        self.curve_raw_ir.clear()
