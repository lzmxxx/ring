# SpO2-Ring BLE 上位机

上位机使用 Python、PyQt5、PyQtGraph、Bleak 和 qasync，实现戒指的实时显示、配置、设备端记录和历史数据导出。

## 启动

双击 `run.bat`，或在工程根目录执行：

```powershell
D:\software\anaconda3\envs\pyqt\python.exe tool\main.py
```

连接 `SpO2-Ring` 后，程序会订阅 FEE7 服务的 FFF5 Notify 特征，自动发送 RTC 时间与时区，随后读取设备状态和记录信息。

## 界面功能

- 扫描、连接、断开和自动重连。
- Raw、算法、Raw+算法三种输出模式。
- 连续、15 s、30 s、自定义周期选择。
- 设备端开始/停止记录；开始新记录前弹窗确认，因为该操作会擦除旧记录。
- 从指定记录序号继续历史同步，断开后可续传；历史数据单独保存为 CSV。
- 实时 SpO2、HR、PI、信号质量、运动状态、电量、RTC 时间和 Raw 红光/红外波形。
- 本机实时 CSV 录制与无硬件模拟演示。

## 20 字节实时数据包

所有多字节字段均为小端。

### A5 01 算法结果

| 偏移 | 字段 |
|---|---|
| 0..1 | `A5 01` |
| 2 | flags：bit0 有效、bit1 已标定、bit2 运动 |
| 3 | 信号质量 0..100 |
| 4..7 | 采样序号 `uint32` |
| 8..11 | 设备 RTC Unix 时间 `uint32` |
| 12..13 | SpO2 ×100 |
| 14..15 | HR ×10 |
| 16..17 | PI ×100 |
| 18..19 | 体温 ×100，V1 固定 0 |

### A5 02 电量

`flags、capacity、voltage_mV、latest_sequence`，其余字节保留为 0。

### A5 04 Raw PPG

每包合并两组红光/红外样本，携带第一样本序号、75 Hz 采样率和 RTC 时间。为适配默认 20 B ATT 通知，固件将 20 bit ADC 值算术右移 4 bit 写入 `int16`，上位机左移 4 bit 恢复量级；因此低 4 bit 不传输。

## 控制帧

格式为：

```text
AA 55 | version | command | sequence(u16) | length(u16) | payload(0..10) | CRC16-CCITT(u16)
```

命令：`01 TIME_SYNC`、`10 SET_OUTPUT_MODE`、`11 SET_SAMPLE_PERIOD`、`20 START_RECORD`、`21 STOP_RECORD`、`22 GET_RECORD_INFO`、`23 SYNC_RECORD`、`24 ERASE_RECORD`、`30 GET_DEVICE_STATUS`、`40 SET_MOTION_PARAMETER`、`80 ACK/EVENT`、`81 ERROR`。

状态、记录信息和一条历史记录超过单帧有效载荷时分片发送；`ProtocolDecoder` 按命令和序号重组，并再次校验历史记录自身 CRC。

## 测试

在 `tool` 目录运行：

```powershell
D:\software\anaconda3\envs\pyqt\python.exe -m unittest -v test_suite.py
```

当前 9 项测试覆盖结果/电量解析、控制帧 CRC、分片状态重组、统计和 CSV。真实 BLE 连接仍需在 Windows 真机环境验收。
