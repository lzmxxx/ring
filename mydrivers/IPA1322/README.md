# IPA1322 双通道驱动

按 stm32-modular-mydrivers 风格封装，底层仍使用本项目的 N32WB452 BSP。
这是可编译源码库，不是预编译 .lib；只依赖固定数组，不使用动态内存。

## 调参数

打开 `IPA1322.c` 的 `IPA1322_Init()`，直接修改 `Config.xxx = 数值`。
不用改寄存器表。初始化参数修改后需要重新编译、烧录。

| 参数 | 当前值 | 含义 |
|---|---:|---|
| LED0_mA / LED1_mA | 60 / 60 | 660nm / 905nm 峰值电流，0..120mA |
| ADC_OSR | 512 | 4MHz时积分128us；支持16到2048的2次幂 |
| TIA_kOhm | 250 | 跨阻，支持10/50/100/250/500/1000/2000 |
| TIA_Cap | 0 | 0/1/2/3对应2.5/5/7.5/10pF |
| IIR_Enable / IIR_Ratio | 1 / 1 | 开启IIR，b01，不抽取 |
| EarlySamplePD | 1 | 原厂首批10颗样品修正；后续芯片改0 |
| FrameTicks | 437 | 帧周期约13.33ms，约75.03Hz |
| LightStart_us | 2048 | 每个时隙的亮采样启动时间 |
| SampleGap_us | 552 | 暗0、亮、暗1的等间隔 |
| LEDLead_us | 20 | LED提前亮采样启动的时间 |

保持二阶环境光消除 `Light-(Dark0+Dark1)/2`，LED IDAC为0。
LED点亮时间自动随OSR变化：`LEDLead_us + 2 + ADC_OSR/4`，当前150us；ADC积分128us。
两路映射固定：TS0→LED0/660nm，TS1→LED1/905nm。
配置检查拒绝不支持的值、时间窗口重叠及帧周期过短；不会自动改变用户参数。
20us提前量沿用已有配置，不代表已对所有增益/负载实测充分建立。

## 调用

```c
#include "IPA1322.h"

IPA1322_Data Data[8];
uint8_t Count;

/* 先完成板级电源、时钟和SysTick初始化。 */
if (IPA1322_Init() != 0) { /* 初始化失败，应用层处理 */ }
Count = IPA1322_ReadPairs(Data, 8); /* 定期轮询，返回0..8对 */
/* Data[i].red_signal / ir_signal：IIR后的有符号Signal，不是原始Light ADC。 */
```

`IPA1322_Init` 是初始化，不是中断入口（不是 `INT`）。驱动内部不打印日志。
可选包含 `IPA1322_Debug.h` 读取具体错误阶段，原MKLink诊断符号保留。
原VOFA全局变量名不变，但重编译可能改变RAM地址，必须用最新AXF解析地址。

## 文件与验证

- `IPA1322.c/.h`：公共驱动与参数；`IPA1322_Debug.h`：可选诊断。
- `vt2102*` / `vt2101_int.h`：原厂SDK支持文件，不在应用中直接调参。
- N32总线、时钟、电源仍复用项目BSP；Keil路径已更新。
- `python tools/check_ipa1322_config.py`：默认参数及路径静态回归。
- 编译不能替代硬件回读、实际光电波形和脉宽验证。
