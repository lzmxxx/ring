# SpO2-Ring BLE 上位机使用说明

本上位机专为 **N32WB452 + IPA1322 + RT-Thread** 血氧戒指量身定制，使用 Python 3 + PyQt5 + PyQtGraph + Bleak 构建，运行在现有的 Conda `pyqt` 环境下。

---

## 快速启动

### 方式一：双击启动（最便捷）
直接在资源管理器中双击运行 `tool/run.bat` 即可一键启动上位机。

### 方式二：命令行启动
打开命令行并激活 conda 环境：
```powershell
conda activate pyqt
cd tool
python main.py
```
或直接指定 conda python 解释器执行：
```powershell
D:\software\anaconda3\envs\pyqt\python.exe tool\main.py
```

---

## 核心功能

1. **BLE 设备扫描与一键直连**：
   - 点击 **[🔍 扫描设备]**：自动扫描周围低功耗蓝牙设备，并实时展示信号强度 (RSSI)。
   - 自动识别并高亮目标戒指（设备名默认 `SpO2-Ring`，MAC: `33:22:33:DB:30:A0`）。
   - 点击 **[⚡ 连接戒指]**：建立连接后自动枚举 GATT 服务，写入 CCC 描述符订阅通知通道（`FEE7` 服务下的 `FFF5` 特征）。
   - 支持**自动重连**（勾选“自动重连”后断开将自动尝试重新连接）。
2. **模拟演示模式**：
   - 无实体戒指时，可点击 **[▶ 模拟演示]** 体验完整的波形渲染、算法数据变化与数据录制流程。
3. **高帧率多通道动态波形 (PyQtGraph)**：
   - **SpO2 趋势曲线**：标定 95% 正常与 90% 警戒基准线。
   - **时域心率 HR_time 趋势曲线**：纯时域自相关心率 (`HR_time`) 动态追踪与波动观测。
   - **微循环与光路曲线**：实时追踪灌注指数 (`PI`) 与红光/红外光路吸收比值 (`R`)。
   - 支持 30点、60点、120点、300点或全屏时间窗切换，支持一键“暂停波形”与“清空曲线”。
4. **数字指标大卡片仪表盘**：
   - 超大字体显示 SpO2、HR、PI、R 比值。
   - 动态有效性状态检测（`VALID` / `INVALID`）与标定状态标识（`未标定(工程联调)` / `已标定`）。
5. **原始报文流与调试日志**：
   - 支持查看最近 100 帧详细解析表格（时间戳、序号、有效性、标定、各字段、原始 20 字节 Hex 码）。
   - 系统运行与蓝牙底层交互日志输出。
6. **数据录制与 CSV 导出**：
   - 点击 **[🔴 开始录制数据]**：实时将接收到的每个数据包以毫秒精度时间戳追加写入 CSV 文件。
   - 点击 **[📁 打开保存文件夹]**：直接打开 `tool/recordings/` 目录查看与导入历史实验数据（可用 Excel、Origin、MATLAB 分析）。

---

## 协议与报文格式说明

固件端通过 Service `0xFEE7` 下的 Characteristic `0xFFF5` 发送 Notify 结果包（每包 20 字节，小端模式）：

| 字节偏移 | 字段名称 | 格式 | 说明 |
|:---:|:---:|:---:|:---|
| 0 ~ 1 | 帧头 Header | 0xA5, 0x01 | 固定包头标志 |
| 2 | Flags 标志位 | uint8 | bit 0: Valid，bit 1: Calibrated，bit 2: Moving（1运动，0静止） |
| 3 | 保留字 Reserved | uint8 | 0x00 |
| 4 ~ 7 | 流水号 Seq | uint32 (LE) | 帧序号，用于丢包率统计 |
| 8 ~ 9 | SpO2 * 100 | uint16 (LE) | 血氧饱和度（例 9850 表示 98.50%） |
| 10 ~ 11 | HR * 10 | uint16 (LE) | 综合心率（例 720 表示 72.0 BPM） |
| 12 ~ 13 | PI * 100 | uint16 (LE) | 灌注指数（例 210 表示 2.10%） |
| 14 ~ 15 | Ratio * 10000 | uint16 (LE) | R 吸收比值（例 5420 表示 0.5420） |
| 16 ~ 17 | HR_time * 10 | uint16 (LE) | 时域自相关心率（例 718 表示 71.8 BPM） |
| 18 ~ 19 | HR_fft * 10 | uint16 (LE) | 频域 FFT 心率（例 725 表示 72.5 BPM） |

每成功发送 30 个血氧包，固件追加一个 20 字节电量包：

| 字节偏移 | 字段名称 | 格式 | 说明 |
|:---:|:---:|:---:|:---|
| 0 ~ 1 | 帧头/类型 | 0xA5, 0x02 | CW2015 电量包 |
| 2 | Flags | uint8 | bit 0: 电量数据有效 |
| 3 | Capacity | uint8 | 剩余电量百分比 |
| 4 ~ 5 | Voltage | uint16 (LE) | 电池电压，单位 mV |
| 6 ~ 9 | Seq | uint32 (LE) | 对应的最近血氧包序号 |
| 10 ~ 19 | Reserved | - | 保留为 0 |

---

## 文件结构说明

```
tool/
├── main.py              # 上位机启动入口，整合 qasync 与 PyQt5
├── ble_client.py        # BLE 客户端封装（Bleak 异步扫描、连接、特征查找、Notify 订阅、模拟数据）
├── protocol.py          # 20字节私有协议解析、报文结构体与统计类
├── data_recorder.py     # 实时数据记录与 CSV 导出管理
├── test_suite.py        # 自动化单元测试套件
├── run.bat              # Windows 快捷运行批处理
├── requirements.txt     # 依赖包列表
├── recordings/          # 数据录制自动保存目录
└── ui/
    ├── __init__.py
    ├── dashboard.py     # 核心数字指标大卡片仪表盘
    ├── chart_view.py    # PyQtGraph 动态高帧率曲线图组件
    └── main_window.py   # 主窗口界面排版与交互逻辑
```
