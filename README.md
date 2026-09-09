# N32WB452 + IPA1322 RT-Thread 血氧戒指

## 构建

用 Keil MDK5 打开 `MDK-ARM/rt_thread_ipa1322.uvprojx`。工程使用 ARM Compiler 5、Cortex-M4F 和 CMSIS-DSP `arm_cortexM4lf_math.lib`。

## 任务与数据链

- `ble` 优先级4：协议栈、订阅状态和结果 Notify。
- `ppg` 优先级6：PB3 FIFO 水线中断唤醒，I2C1 RX DMA1_CH7 批量读取，配对 LED0/660nm 和 LED1/905nm。
- `spo2` 优先级10：768 Frame Ring，600 Frame/8s 窗，每75个有效 Frame 更新一次。
- Idle：所有电源锁释放且 BLE 允许时进入 STOP0。

按键 PA0 使用板上 R6 10k 外部下拉，GPIO 配置为浮空输入。Standby 唤醒后持续按住1.5s才确认开机，成功后等待释放；运行期每20ms采样，连续按住2s才进入关机流程。

FIFO 溢出、DMA 超时或数据 Tag 无法配对时，当前算法窗立即作废，不会将断流前后的数据拼成一个血氧窗。

## IPA1322 集中调参

只需修改 `mydrivers/IPA1322/IPA1322.c` 中 `IPA1322_Init()` 的“用户调参区”。当前值：

- LED0/LED1：60mA；LED IDAC=0。
- OSR=512，ADC 积分128us，LED 点亮150us。
- TIA=250kOhm，IIR=b01，不抽取。
- 二阶环境光消除；FIFO 仅存每路 `SIGNAL_DATA`。
- 帧率75.03Hz；FIFO 剩余空间水线8，约56 entry 触发。
- `EarlySamplePD=1` 会额外写 `0x23[7]=1` 选择外置 Photodiode，适用于原厂说明的首批10颗；后续样品/量产改为0。

## 血氧算法

`mydrivers/SpO2/SpO2.c` 同时使用 CMSIS-DSP 点积/均值/正余弦函数：

- 时域：约束30~200 BPM的归一化自相关。
- 频域：0.5~3.375Hz 谱峰与主峰占比。
- 时域/频域心率差、红光/红外相关性和 PI 共同作为质量门限。
- SpO2 使用旧 SPCOV2.0 ProSim8 R 表插值；`SPO2_CALIBRATION_CONFIRMED=0` 明确标记该表尚未针对当前戒指光路重新标定。

## BLE 结果包

服务 UUID `0xFEE7`，特征 `0xFFF5`；只在中央设备连接且写 CCC 开启 Notify 后发送。每包20字节，小端：

`A5 01 | flags(1) | reserved(1) | seq(u32) | SpO2*100 | HR*10 | PI*100 | R*10000 | HR_time*10 | HR_freq*10`

`flags.bit0=valid`，`flags.bit1=calibrated`，`flags.bit2=moving`（SC7A20内部判断，1运动、0静止）。当前标定位为0，因此读数只用于算法联调，不应作为医疗测量结果。

每成功发送30个血氧包后追加一个20字节CW2015电量包：

`A5 02 | valid(1) | capacity%(1) | voltage_mV(u16) | latest_spo2_seq(u32) | reserved(10)`
