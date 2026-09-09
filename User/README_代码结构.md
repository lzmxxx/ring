# 血氧戒指固件代码阅读指南

本文只描述当前工程实际执行的代码。建议第一次阅读时严格按照本文顺序打开文件，不要从 `IPA1322.c`、BLE 厂商库或算法内部开始看，否则很容易被寄存器和协议栈细节淹没。

## 1. 先建立整体概念

整个程序可以看成一条事件驱动流水线：

```text
复位
  ↓
RT-Thread启动并创建main线程
  ↓
main初始化硬件并创建4个业务任务
  ↓
手机连接BLE
  ↓
PPG任务启动IPA1322
  ↓
PB3水线中断 → DMA读取FIFO → 连续FIR → 环形缓冲区
  ↓ 每75帧产生一个作业
SpO2任务复制最近600帧并计算
  ↓ 每秒产生一个结果
BLE任务发送结果、算法、加速度和电池数据包
  ↓
没有任务需要运行时，由idle_hook自动进入Sleep或STOP0
```

这里没有一个永远轮询所有功能的超级 `while (1)`。每个任务都阻塞在自己的信号量或消息队列上，只有中断或其他任务提交工作时才被唤醒。这和 FreeRTOS 中使用 `xSemaphoreTake(..., portMAX_DELAY)`、`xQueueReceive(..., portMAX_DELAY)` 的设计相同。

## 2. 推荐阅读顺序

按照下面顺序读，能从业务流程逐步走到底层：

1. `app_config.h`：先知道主频、采样率、窗口、任务栈和优先级。
2. `app_main.c`：看整个系统按什么顺序启动。
3. `app_ppg.c`：看原始 PPG 如何从硬件进入 RAM。
4. `app_spo2.c`：看什么时候计算、如何处理失败窗口。
5. `SpO2.c` 和 `SpO2_advanced.c`：看连续 FIR、时域和 FFT 算法。
6. `app_ble.c`：看 BLE 任务如何被唤醒以及发送顺序。
7. `app_packet.c`：看 20 字节数据包每个字节的意义。
8. `interrupt.c`：看硬件中断如何分发到各任务。
9. `app_power.c`：最后看 Sleep、STOP0、长按关机和唤醒恢复。
10. `mydrivers/<芯片>/`：需要改寄存器或硬件参数时再进入具体驱动。

## 3. 工程目录和分层

| 目录 | 职责 | 阅读时如何处理 |
| --- | --- | --- |
| `User/` | 应用流程、任务、数据包和中断分发 | 日常修改主要看这里 |
| `mydrivers/Board/` | 系统时钟、SysTick、RT-Thread 堆、DWT 延时 | 改主频或系统时基时看 |
| `mydrivers/Power/` | PA11/PC13 负载开关和 PA6 LED | 改硬件上下电时看 |
| `mydrivers/MyI2C/` | IPA1322 使用的 I2C1、DMA 和备用软件 I2C | 排查 PPG 总线时看 |
| `mydrivers/MyI2C2/` | CW2015 使用的硬件 I2C2 | 排查电量计总线时看 |
| `mydrivers/MySPI2/` | SC7A20 使用的硬件 SPI2 | 排查加速度计总线时看 |
| `mydrivers/IPA1322/` | 光学 AFE 配置、FIFO 和原始数据解析 | 调 LED、电路增益、采样时序时看 |
| `mydrivers/CW2015/` | 电量计探测、Profile、QuickStart、SOC/电压读取 | 调电池模型时看 |
| `mydrivers/SC7A20/` | 三轴读取和芯片内部运动中断 | 调运动阈值时看 |
| `mydrivers/SpO2/` | 连续 FIR、时域血氧/心率、CMSIS-DSP FFT | 调算法时看 |
| `RTOS/` | RT-Thread 内核 | 一般不修改 |
| `BLE_Driver/` | 厂商 BLE 协议栈和 GATT 底层 | 一般不修改 |
| `Start/`、`Library/` | 启动文件、CMSIS 和 N32 标准外设库 | 一般不修改 |

依赖方向固定为：

```text
标准外设库 → 总线驱动 → 芯片驱动 → User应用任务
```

例如 `app_ppg.c` 调用 `IPA1322.c`，`IPA1322.c` 再调用 `bsp_hw_i2c.c`。底层 I2C 驱动不会反过来调用 IPA1322 或应用任务。

## 4. 上电后第一段代码在哪里

### 4.1 从复位到 RT-Thread

执行链如下：

```text
startup_n32wb452.s
  → SystemInit()
  → C运行库入口
  → RT-Thread的$Sub$$main()
  → rtthread_startup()
  → rt_hw_board_init()
  → 创建main、timer、idle线程
  → 启动调度器
  → main_thread_entry()
  → User/app_main.c中的main()
```

因此，`User/app_main.c` 中的 `main()` 已经运行在线程环境中，可以使用 `rt_thread_mdelay()`、信号量和消息队列。它不是传统裸机程序中复位后直接执行的 main。

### 4.2 `rt_hw_board_init()` 做什么

文件：`mydrivers/Board/board.c`

执行顺序：

1. 配置 NVIC 为 Priority Group 2。
2. 调用 `SetSysClock_HSE_PLL()`，使用 32 MHz 外部晶振和 PLL 建立运行主频。
3. 当前 `APP_SYSCLK_HZ` 为 64 MHz，所以 HSE 32 MHz × 2 得到 64 MHz。
4. 配置 SysTick 为 1000 Hz，即正常运行时每 1 ms 产生一个 RT-Thread tick。
5. 初始化 24 KB RT-Thread 堆 `system_heap`。
6. 使能 Cortex-M4 DWT 周期计数器，供底层微秒延时使用。

若 HSE 或 PLL 失败，`Clock_PllFailure` 会记录失败阶段，避免永久卡在等待循环中。

## 5. `app_main.c` 的每一步

`app_main.c` 是全工程最先阅读的业务文件，目前只保留四段。

### 5.1 开机确认

```c
if (!app_power_boot_check())
{
    app_power_enter_standby();
    return 0;
}
```

- 普通上电或调试器复位没有 Standby 标志，直接允许启动。
- 从 Standby 被 PA0 唤醒时，要求按键连续保持高电平 1500 ms。
- 1500 ms 内松开会被视为误触，立即重新进入 Standby。
- 确认成功后等待按键释放，防止本次开机长按又触发运行期关机。

### 5.2 初始化板级硬件和两个低速传感器

```c
bsp_power_init();
app_power_init();
g_cw2015_init_status = CW2015_Init();
(void)SC7A20_Init();
```

- `bsp_power_init()`：PA11 打开 Sensor_1V8，PC13 打开 IPA1322_VDD，PA6 点亮板载 LED。配置输出前先把输出寄存器置高，避免负载开关出现低电平毛刺。
- `app_power_init()`：创建关机任务、按键软定时器、低功耗锁，并把 `idle_hook()` 注册到 RT-Thread 空闲线程。
- `CW2015_Init()`：初始化 I2C2、扫描地址、检查电池曲线、执行 QuickStart，并等待有效 SOC。
- `SC7A20_Init()`：初始化 SPI2、验证 WHO_AM_I、配置芯片内部运动判断和 PA9 中断。

CW2015 初始化失败不会阻止其他功能启动，状态保存在 `g_cw2015_init_status` 并随电池包上传。SC7A20 当前也不会作为系统启动的致命条件。

### 5.3 创建业务任务

```c
app_ppg_init();
app_spo2_init();
app_ble_init();
```

调用顺序有明确依赖：PPG 任务先创建 `spo2_job_mq`，然后 SpO2 任务才能等待该队列；SpO2 任务准备好后再启动 BLE 任务。任一任务初始化返回非零，程序调用 `app_power_off()` 进入关机流程。

### 5.4 释放启动锁

`app_power_init()` 一开始把 `PM_SYSTEM` 置为 1，启动阶段禁止进入深度低功耗。所有驱动和任务创建成功后，`app_power_unlock(PM_SYSTEM)` 将其释放。随后 main 线程返回退出，系统主要由四个业务任务和 idle 线程运行。

## 6. 任务是怎样分配的

RT-Thread 数字越小优先级越高。所有参数集中在 `app_config.h`。

| 任务 | 入口函数 | 优先级 | 静态栈 | 平时阻塞在哪里 | 被谁唤醒 |
| --- | --- | ---: | ---: | --- | --- |
| `power` | `power_thread_entry` | 3 | 768 B | `power_sem` | 长按关机软定时器 |
| `ble` | `ble_thread_entry` | 4 | 2048 B | `work_sem` | BLE IRQ、RTC、订阅变化、算法结果 |
| `ppg` | `ppg_thread_entry` | 6 | 1536 B | `fifo_sem` | PB3 FIFO 中断或 BLE 连接变化 |
| `spo2` | `spo2_thread_entry` | 10 | 4096 B | `spo2_job_mq` | PPG 每累计 75 帧提交一次作业 |
| `main` | `main_thread_entry` | 10 | 2048 B | 不长期存在 | RT-Thread 启动时创建，完成初始化后退出 |
| `idle` | RT-Thread 内部入口 | 最低 | 内核配置 | 无就绪任务时运行 | 调度器自动执行 |

这样安排的原因：

- 关机任务最高，确保长按确认后尽快停止所有外设。
- BLE 次高，避免协议栈事件和连接时序被算法阻塞。
- PPG 高于算法，优先搬走硬件 FIFO，防止溢出。
- SpO2 计算量最大但实时期限最宽，每秒完成一次即可，所以优先级最低。

## 7. 任务之间如何传递工作

### 7.1 IPC 总表

| 对象 | 类型 | 生产者 | 消费者 | 作用 |
| --- | --- | --- | --- | --- |
| `fifo_sem` | 信号量 | PB3 EXTI3、BLE 连接回调 | PPG 任务 | 通知“有 FIFO 数据”或“连接状态变了” |
| `dma_sem` | 信号量 | DMA1 Channel 7 ISR | PPG 任务 | 通知 I2C1 DMA 已经搬运完成 |
| `spo2_job_mq` | 消息队列，深度 4 | PPG 任务 | SpO2 任务 | 传递窗口末尾序号和 epoch |
| `result_mq` | 消息队列，深度 4 | SpO2 任务 | BLE 任务 | 传递完整 `SpO2_Result` |
| `work_sem` | 信号量 | BLE/RTC ISR、订阅回调、SpO2 任务 | BLE 任务 | 通知 BLE 处理协议栈或发送数据 |
| `power_sem` | 信号量 | 按键软定时器 | power 任务 | 通知执行最终关机 |

信号量只表达“有事情要处理”，消息队列同时携带数据。ISR 不做 I2C、SPI、算法或 BLE 发送，只清硬件标志并释放信号量。

### 7.2 对照 FreeRTOS

| RT-Thread | FreeRTOS 中的近似概念 |
| --- | --- |
| `rt_thread_init()` + `rt_thread_startup()` | `xTaskCreateStatic()` |
| `rt_sem_take(..., RT_WAITING_FOREVER)` | `xSemaphoreTake(..., portMAX_DELAY)` |
| `rt_sem_release()` | `xSemaphoreGive()` |
| `rt_mq_send()` | `xQueueSend()` |
| `rt_mq_recv()` | `xQueueReceive()` |
| `rt_thread_mdelay()` | `vTaskDelay()` |
| `rt_enter_critical()` | `taskENTER_CRITICAL()` |
| `rt_thread_idle_sethook()` | Idle Hook |

## 8. 从 BLE 连接到开始采样

系统启动后，PPG 任务已经存在，但 `ppg_requested = RT_FALSE`，IPA1322 不进行连续转换。

手机连接时，BLE 协议栈调用 `bt_event_callback(BT_EVENT_CONNECTED)`：

1. `gBT_STS` 改成已连接。
2. `notify_enabled` 先保持关闭，等待手机写 CCC 订阅通知。
3. 调用 `app_ppg_set_connected(RT_TRUE)`。
4. `app_ppg_set_connected()` 设置 `ppg_requested = RT_TRUE` 并释放 `fifo_sem`。
5. PPG 任务从信号量唤醒，发现尚未采样，进入 `start_sampling()`。

连接和订阅是两个状态。建立连接会启动采样；只有手机完成 Notify 订阅后，`ble_notifications_enabled()` 才返回真并允许发送数据。

## 9. PPG 采集任务逐步说明

文件：`app_ppg.c`

### 9.1 初始化阶段 `app_ppg_init()`

1. 创建 `fifo_sem`。
2. 创建 `dma_sem`。
3. 创建深度为 4 的 `spo2_job_mq`。
4. 清除采样请求和流式采样状态。
5. 打开 PA11/PC13 传感器电源并等待 50 ms。
6. 用静态线程控制块和 1536 B 静态栈创建 `ppg` 任务。

此时只准备资源，不启动 IPA1322 连续转换。

### 9.2 启动采样 `start_sampling()`

1. 调用 `invalidate_window()`，丢弃旧窗口并重置连续 FIR。
2. 最多尝试 3 次 `IPA1322_Init()`，失败间隔 100 ms。
3. 配置 PB3 为上拉输入、EXTI3 下降沿中断。
4. 调用 `IPA1322_Start()` 清 FIFO、使能水线/溢出中断并启动转换。
5. 设置 `PPG_Streaming = RT_TRUE`。
6. 如果 PB3 此时已经是低电平，主动释放一次 `fifo_sem`，避免错过启动瞬间的下降沿。

### 9.3 IPA1322 当前硬件配置

- I2C1：PB6 SCL、PB7 SDA，100 kHz。
- 芯片 7 位地址：`0x48`。
- 905 nm：Slot 0。
- 660 nm：Slot 1。
- Slot 2、3：关闭。
- 原始 ADC：20 位有符号数据，每个 FIFO entry 3 字节，顶部包含 Tag 和 Slot ID。
- 帧率：`4 MHz / (122 × 437) ≈ 75.028 Hz`。
- ADC OSR：1024，对应约 256 µs 积分。
- TIA：250 kΩ，补偿电容配置 0（2.5 pF）。
- 芯片内部 IIR：开启，Ratio 为 1。
- FIFO 深度：64 entries。
- 水线：剩余空间 8 时触发，即约累计 56 entries；两个 entry 组成一帧双波长数据，所以约 28 帧触发一次搬运。
- INTB：开漏、低有效，连接 PB3。
- 当前源码中的 LED0/LED1 实际赋值都是 40 mA；代码旁边的“60 mA”注释与赋值不一致，应以赋值 `40U` 为准。

### 9.4 PB3 中断到 DMA 完成

1. IPA1322 FIFO 达到水线，把 PB3 拉低。
2. `EXTI3_IRQHandler()` 清 EXTI3 挂起位并调用 `app_ppg_irq()`。
3. `app_ppg_irq()` 增加 `PPG_FifoIrqCount`，释放 `fifo_sem`。
4. PPG 任务唤醒并获取 `PM_FIFO` 锁，禁止搬运期间进入低功耗。
5. `read_fifo_once()` 先读取芯片中断标志；Bit 5 表示 FIFO 溢出。
6. 读取 FIFO entry 数量。
7. 获取 `PM_DMA` 锁。
8. 从 FIFO 数据寄存器 `0x14` 启动 I2C1 RX DMA，长度是 `entries × 3` 字节。
9. PPG 任务在 `dma_sem` 上等待，超时为 20 ms。
10. DMA1 Channel 7 完成中断调用 `app_ppg_dma_irq()`，结束 I2C 事务并释放 `dma_sem`。
11. PPG 任务恢复运行，释放 `PM_DMA` 锁并解析 FIFO。

如果 DMA 启动失败或 20 ms 超时，代码会终止 DMA、清空 FIFO、增加 `PPG_Gaps` 并作废当前算法窗口。

### 9.5 原始数据配对

`IPA1322_ParseFIFO()` 对每个 3 字节 entry 做以下处理：

1. 解析 Tag，要求数据 Tag 为 1。
2. 解析 Slot ID，只接受 Slot 0 和 Slot 1。
3. 拼接 20 位 ADC 原始码并执行符号扩展，结果保存在 `int32_t` 中，不右移、不压缩为 16 位。
4. Slot 0 暂存为 905 nm 红外数据。
5. 紧随其后的 Slot 1 作为 660 nm 红光数据，与红外组成一个 `IPA1322_Data`。

如果 entry 足够但无法组成有效双通道数据，视为波长 Tag 失步，当前窗口会被作废。

### 9.6 连续 FIR 和环形缓冲区

每得到一帧双波长数据，`store_pairs()` 依次执行：

1. 调用 `SpO2_FilterSample()`，把一个 660 nm 和一个 905 nm 原始样本送入常驻 FIR。
2. 保存原始 `red/ir` 和滤波后 `red_fir/ir_fir`。
3. `PPG_TotalFrames` 加 1，作为严格递增的全局采样序号。
4. 将帧写入 768 项环形缓冲区。
5. 更新用于内存观察的最新原始值和序号。
6. `pending_frames` 每累计 75 帧，提交一个 `PPG_Job`。

FIR 是 151 taps、75 Hz、0.3–5 Hz 带通。FIR 实例和状态数组常驻 RAM，每次只输入一个采样点。8 秒窗口滑动时不会重新初始化 FIR，因此窗口之间没有重复冲激响应。

只有以下情况会调用 `SpO2_FilterReset()`：

- 新的一次采集会话开始。
- FIFO 溢出。
- DMA 失败或超时。
- 双波长 Tag 失步。
- 主动停止采样。

首次样本会填充 FIR 历史状态，以减小整条零状态历史造成的启动阶跃。151 taps 的线性相位滤波器仍有约 1 秒群延迟，这是滤波器自身的正常延迟。

### 9.7 8 秒窗口和 1 秒步进

- `PPG_WINDOW = 600`：每次算法使用最近约 8 秒数据。
- `PPG_STEP = 75`：每新增约 1 秒数据提交一次作业。
- `PPG_RING_SIZE = 768`：比计算窗口多 168 帧，给算法复制留出回绕余量。

`PPG_Job` 不复制波形，只携带：

- `end_seq`：这个窗口最后一帧的序号。
- `epoch`：当前连续数据段的代数。

开始采样后的前 7 个一秒作业还没有 600 帧历史，`app_ppg_copy()` 会拒绝这些作业。当前 `app_spo2.c` 在尚无历史有效结果时仍会上传零初始化结果，因此连接后前约 7 秒可能看到零值；第一个完整计算窗口约在第 8 秒产生。

### 9.8 为什么需要 epoch

只检查序号不能判断窗口中间是否发生过断流。每次 `invalidate_window()` 都会让 `current_epoch` 加 1。算法任务复制窗口时要求作业 epoch 与当前 epoch 相同，否则拒绝复制，从而避免把故障前后的数据拼成一个看似连续的 8 秒窗口。

## 10. SpO2 算法任务逐步说明

文件：`app_spo2.c`

任务主循环只有一个入口：永久等待 `spo2_job_mq`。

收到作业后：

1. 若系统正在关机，直接丢弃。
2. 获取 `PM_ALGO` 锁，禁止计算期间进入低功耗。
3. 清空本次结果并写入作业末尾序号。
4. 调用 `app_ppg_copy()` 复制对应的 600 帧。
5. 复制成功后调用 `SpO2_Calculate()`。
6. 对时域心率调用 `stabilize_heart_rate()`。
7. 保存本次完整结果作为失败窗口的备用值。
8. 更新全局快照 `SpO2_Latest`。
9. 调用 `app_ble_post()` 把结果放入 BLE 队列。
10. 释放 `PM_ALGO` 锁。

复制失败时不会重新发送全零结果。如果此前已有完整结果，则保持上一组数值，只更新序列号；`SpO2_CopyFailures` 同时加 1。

### 10.1 时域路径

1. FIR 输出每 3 点抽取一次，由约 75 Hz 降为约 25 Hz，用于 MSPTDfast 初步寻找波峰和波谷。
2. 候选点会回到 75 Hz FIR 数据附近再搜索，提高峰谷位置精度。
3. 相邻两个峰之间寻找最低谷，形成一个搏动。
4. 搏动 AC 是峰值减谷值；DC 是该搏动区间原始 ADC 的中值。
5. 幅度保留范围为中值的 0.25–3.5 倍，周期最长 3 秒。
6. 周期跨度差异过大时逐个剔除距离中值最远的搏动。
7. 每搏重采样为 100 点，与中值模板比较，相关系数低于 0.65 的搏动被剔除。
8. 至少两个有效搏动时，用平均峰间距计算 `hr_time`。
9. 对红光和红外的逐搏 AC/DC 计算 R，再对多个搏动做去极值平均，得到 `ratio_time` 和 `spo2_time`。

时域有效标志要求：红光和红外各至少两个有效搏动、至少两个配对 R、心率在 30–200 BPM、血氧查表结果大于 0。

### 10.2 FFT 路径

1. 分别用原始红光和红外除以各自 DC，得到相对交流分量。
2. 去均值、去线性趋势并乘 Hann 窗。
3. 零填充到 2048 点，调用 CMSIS-DSP `arm_rfft_fast_f32()`。
4. 在 0.5 Hz 到 200 BPM 对应频带内搜索红外峰值。
5. 峰值幅度与频带幅度中值之比必须至少为 1.5。
6. 使用峰值及相邻两个频点的对数幅值做抛物线插值，得到 `hr_fft`。
7. 在主峰附近积分红光和红外能量，得到 `ratio_fft` 和 `spo2_fft`。

DST 已从目标固件中删除；结构体和 BLE 包中对应位置仅为兼容旧协议保留为 0。

### 10.3 主心率稳定器

BLE 主结果中的 `hr` 由 `hr_time` 稳定后得到：

- 候选范围：30–200 BPM。
- 连续两个窗口变化不超过 5 BPM 后才确认。
- 确认后使用 `0.7 × 旧值 + 0.3 × 新值` 平滑。
- 突变候选重新开始两窗口确认，确认期间保持上次心率。
- 无效窗口最多保持上次确认值 10 个窗口，约 10 秒。

`result.valid` 当前固定为 1，表示本周期已经产生结果。最终有效性由上位机结合 `time_valid`、`fft_valid` 和原始波形判断。

## 11. BLE 任务逐步说明

文件：`app_ble.c`

### 11.1 初始化

BLE 任务启动后自行初始化协议栈：

- 设备名：`SpO2-Ring`。
- 固定地址：`33:22:33:DB:30:A0`。
- 服务 UUID 配置值：`0xFEE7`。
- 特征配置由 `USER_IDX_WRITE_NOTIFY_CHAR/VAL` 对应的厂商 GATT 表建立。
- 调用 `hlt_rtc_init()` 准备 RTC 唤醒。
- 设置 `BLE_Ready = RT_TRUE` 后开始事件循环。

修改手机端 UUID 时，需要同时检查 `app_ble.c` 的服务/特征赋值以及 `BLE_Driver/profile/src/user.c` 的属性表，不能只改其中一个位置。

### 11.2 BLE 事件循环

每次 `work_sem` 唤醒后：

1. 获取 `PM_BLE` 锁。
2. 调用 `process_stack_events()` 推进 BLE 协议栈并处理射频 IRQ/RTC 监控事件。
3. 如果手机刚订阅 Notify，立即尝试发送一次电池状态包。
4. 从 `result_mq` 非阻塞取出所有待发算法结果。
5. 对每个结果调用 `send_result_group()`。
6. 再次推进协议栈直到空闲。
7. 设置 `flag_bt_enter_sleep = 1`，表示 BLE 任务已允许睡眠。
8. 释放 `PM_BLE` 锁。
9. 永久阻塞等待下一次 `work_sem`。

`run_stack_until_idle()` 每次最多执行 64 轮 `bt_run_thread()`，防止协议栈长期占住 CPU。

### 11.3 结果队列满时怎么办

`result_mq` 深度为 4。如果算法结果放入失败，`app_ble_post()` 先取出最旧结果，再放入最新结果。这样 BLE 拥堵时优先保证手机看到最新生理状态，不会无限积压旧数据。

### 11.4 每秒发送顺序

每个算法结果依次发送：

1. `A5 01` 主结果包。
2. 主结果发送成功后发送 `A5 05` 时域/FFT 独立算法包。
3. 发送 `A5 03` 三轴加速度包。
4. 每成功发送 30 个主结果包，再发送一次 `A5 02` 电池包。

当前这个目标工程不发送原始 PPG 波形包。它发送计算结果、算法分路结果、三轴加速度和电池状态。

## 12. 四种 BLE 数据包

文件：`app_packet.c`

所有包固定 20 字节，小端序，多余字段先清零。

### 12.1 `A5 01` 主结果

| 字节 | 内容 |
| --- | --- |
| 0–1 | `A5 01` |
| 2 bit0 | 本周期算法结果已经产生 |
| 2 bit1 | 已完成标定 |
| 2 bit2 | 自上次主结果包之后发生过运动中断 |
| 3 | 保留 0 |
| 4–7 | PPG 末帧序号 `seq` |
| 8–9 | `spo2 × 100` |
| 10–11 | 稳定后的 `hr × 10` |
| 12–13 | `PI × 100` |
| 14–15 | 主 `R × 10000` |
| 16–17 | 时域 `hr_time × 10` |
| 18–19 | FFT `hr_fft × 10` |

运动位在打包时调用 `SC7A20_GetAndClearMotion()`，因此它表达“从上一个 A5 01 到当前 A5 01 之间是否至少发生一次运动事件”。读取后软件标志清零。

### 12.2 `A5 02` 电池

| 字节 | 内容 |
| --- | --- |
| 0–1 | `A5 02` |
| 2 | 本次 CW2015 读取是否成功 |
| 3 | SOC 整数百分比 |
| 4–5 | 电压 mV |
| 6–9 | 最新序号 |
| 10 | I2C ACK 状态，0 成功 |
| 11 | CW2015 版本寄存器原始值 |
| 12 | 启动初始化结果，0 成功 |
| 16 | 实际命中的 7 位 I2C 地址 |
| 17 | I2C2 扫描到的设备总数 |

### 12.3 `A5 03` 加速度

| 字节 | 内容 |
| --- | --- |
| 0–1 | `A5 03` |
| 2 | 三轴读取是否成功 |
| 4–7 | 对应结果序号 |
| 8–9 | X，`int16_t`，单位 mg |
| 10–11 | Y，`int16_t`，单位 mg |
| 12–13 | Z，`int16_t`，单位 mg |

### 12.4 `A5 05` 独立算法

| 字节 | 内容 |
| --- | --- |
| 0–1 | `A5 05` |
| 2 bit0 | 时域算法有效 |
| 2 bit1 | FFT 算法有效 |
| 3 | 时域配对搏动数量 |
| 4–7 | 对应结果序号 |
| 8–9 | 时域 `spo2_time × 100` |
| 10–11 | FFT `spo2_fft × 100` |
| 12–13 | 旧 DST 血氧槽位，固定 0 |
| 14–15 | 时域 `ratio_time × 10000` |
| 16–17 | FFT `ratio_fft × 10000` |
| 18–19 | 旧 DST R 槽位，固定 0 |

## 13. CW2015 电量计流程

硬件：PB10 SCL、PB11 SDA，I2C2 100 kHz。

`CW2015_Init()`：

1. 扫描 `0x08–0x77` 的全部 7 位地址。
2. 优先寻找默认地址 `0x62`。
3. 如果没有 `0x62` 且总线上只发现一个设备，使用这个唯一地址，避免把版本号当地址判断条件。
4. 读取版本寄存器，仅检查通信是否成功，不固定要求某个版本值。
5. 切换到 NORMAL 模式。
6. 检查 64 字节电池 Profile 和 UPDATE 标志；不匹配则重新写入、回读校验并软复位。
7. 主动执行一次 QuickStart，然后切回 NORMAL。
8. 最多等待 3 秒，直到 SOC 整数值不大于 100。
9. 超时仍无有效 SOC 时让 CW2015 进入 SLEEP 并返回失败。

每次发送电池包时，`CW2015_ReadData()` 读取 SOC，并连续读取 3 次电压取中值；电压按 305 µV/LSB 换算，合理范围限定为 2500–5000 mV。

## 14. SC7A20 运动和三轴流程

硬件连接：

- PB12：CS。
- PB13：SPI2 SCLK。
- PB14：SPI2 MISO。
- PB15：SPI2 MOSI。
- PA9：INT1。
- PA8/INT2 当前未使用。

SPI2 使用 Mode 0、8 bit、MSB first、软件片选。当前 64 MHz 配置下 PCLK1 为 32 MHz，SPI2 分频 16，通信约 2 MHz。

SC7A20 配置：

- WHO_AM_I 必须为 `0x11`。
- `CTRL1 = 0x3F`：25 Hz、低功耗、XYZ 开启。
- `CTRL2 = 0x01`：高通滤波接入 INT1 判断。
- `CTRL3 = 0x40`：AOI1 路由到 INT1。
- `INT1_CFG = 0x2A`：XYZ 正向高阈值任一满足即触发。
- 阈值 `8 × 16 mg = 128 mg`。
- 持续时间 `2 × 40 ms = 80 ms`。
- INT1 不锁存，高有效；PA9 使用下拉输入和上升沿 EXTI9。

初始化时先关闭 INT1，等待高通 300 ms 收敛，再读取 REFERENCE 建立基线并清残留状态，最后开启 INT1 并回读关键寄存器。

PA9 中断只执行 `motion_pending = 1`。BLE 主结果包打包时读取并清零该标志。三轴数据另行通过 SPI 连续读取 6 字节，在低功耗 ±2 g 模式下取每轴高 8 位并乘 16，得到 mg。

## 15. 中断入口逐个说明

所有 MCU 中断入口集中在 `interrupt.c`。

| 中断 | 硬件来源 | ISR 中的操作 | 唤醒对象 |
| --- | --- | --- | --- |
| `SysTick_Handler` | Cortex-M SysTick，正常时 1 ms | `rt_tick_increase()` | 超时任务和软件定时器 |
| `EXTI0_IRQHandler` | PA0 按键上升沿 | 清标志、计数、启动关机计时器 | key 软件定时器 |
| `EXTI3_IRQHandler` | PB3 IPA1322 INTB 下降沿 | 清标志、增加计数、释放 `fifo_sem` | PPG 任务 |
| `DMA1_Channel7_IRQHandler` | I2C1 RX DMA 完成/错误 | 结束或中止 DMA、释放 `dma_sem` | PPG 任务 |
| `EXTI9_5_IRQHandler` | PA9 运动；PA5/6/7 BLE 监控 | 置运动位或通知 BLE monitor | BLE 任务/运动标志 |
| `EXTI15_10_IRQHandler` | PB14 BLE 射频核事件 | 置 `flag_bt_irq`、释放 `work_sem` | BLE 任务 |
| `RTC_WKUP_IRQHandler` | RTC Wakeup | 清 RTC/EXTI 标志、置 `wakeup_flag` | BLE 任务和睡眠退出 |

每个 ISR 都用 `rt_interrupt_enter()` 和 `rt_interrupt_leave()` 包裹，使内核正确处理嵌套和中断退出后的任务切换。

## 16. Low Power 的完整实现

### 16.1 总开关

`app_config.h`：

```c
#define APP_LOW_POWER_ENABLE 1U
```

- 设为 1：允许连接态 Sleep 和未连接 STOP0。
- 设为 0：`g_pm_runtime_sleep_disabled` 为 1，idle hook 不执行 WFI/STOP0，CPU 全速保持运行，适合调试和连续 SWD 读内存。

运行主频由 `APP_SYSCLK_HZ` 单独配置，当前为 64 MHz。时钟驱动仍支持 48/64/96/128 MHz。

### 16.2 为什么需要低功耗锁

系统可能在任何任务阻塞后立刻进入 idle hook。为了避免 I2C、DMA、算法或 BLE 操作进行到一半时睡眠，各模块在工作前获取引用计数锁，工作后释放。

| 锁 | 持有者 | 保护内容 |
| --- | --- | --- |
| `PM_FIFO` | PPG 任务 | FIFO 检查、读取和解析阶段 |
| `PM_DMA` | PPG 任务 | I2C1 DMA 传输阶段 |
| `PM_ALGO` | SpO2 任务 | 600 帧复制和算法计算 |
| `PM_BLE` | BLE 任务 | 协议栈处理和 Notify 发送 |
| `PM_SYSTEM` | main/关机流程 | 系统启动和关机的整体一致性 |

锁是引用计数而不是单 bit。相同模块重复获取后必须对应释放相同次数。增减操作在短临界区内进行，避免中断并发破坏计数。

### 16.3 idle hook 什么时候运行

只有调度器找不到任何更高优先级就绪任务时，RT-Thread 才运行 idle 线程并调用 `idle_hook()`。因此进入低功耗本身不需要单独的睡眠任务。

选择逻辑：

```text
APP_LOW_POWER_ENABLE == 0 ?
  是 → 直接返回，CPU保持运行
  否 → BLE已连接？
         是 → connected_ble_sleep()
         否 → 满足STOP0条件？
                是 → enter_stop0_once()
                否 → shallow_sleep_once()
```

### 16.4 浅 Sleep

`shallow_sleep_once()` 清除 `SLEEPDEEP` 后执行 `WFI`。SysTick 保持开启，因此最迟约 1 ms 就会唤醒。它用于当前暂时不满足深睡条件、但又没有任务要运行的短空隙。

### 16.5 BLE 连接态 Sleep

连接状态需要保持 BLE 链路，所以不进入 STOP0。进入前检查：

- 没有正在关机。
- 所有低功耗锁为 0。
- PPG 没有正在执行 DMA，并且 PB3 INTB 为高电平。
- BLE 任务已经处理完事件并设置 `flag_bt_enter_sleep = 1`。
- PA0 按键没有按下。
- BLE monitor 低功耗握手成功。

条件满足后：

1. 进入 RT-Thread 临界区并再次检查，防止检查后到睡眠前状态变化。
2. 记录 RTC 时间，单位为 1/256 秒。
3. 关闭 SysTick 并清除其挂起位。
4. 保持 `SLEEPDEEP = 0`，执行普通 `WFI`。
5. 由 BLE、PPG、RTC 或其他允许的中断唤醒。
6. 读取 RTC 差值并补偿 RT-Thread tick。
7. 重新配置并开启 SysTick。

PPG 在连接态仍由 IPA1322 硬件持续采样；MCU 可以在两次 FIFO 水线中断之间 Sleep。FIFO 达到水线后 PB3 会唤醒 MCU 搬运数据。

### 16.6 未连接 STOP0

进入 STOP0 必须同时满足：

- 不在关机流程。
- 所有低功耗锁为 0。
- PPG 已停止，或 DMA 空闲且 PB3 为高。
- BLE 协议栈空闲。
- BLE 没有连接。
- PA0 没有按下。

执行流程：

1. 进入临界区并再次检查所有条件。
2. 记录 RTC 时间。
3. 从 64 MHz HSE PLL 切换到内部 8 MHz HSI。
4. 关闭 SysTick、清挂起位并释放 BLE 接口 GPIO。
5. 关闭 STOP 调试保持功能。
6. 与 BLE monitor 完成睡眠握手。
7. 再检查 BLE IRQ、RTC 标志、锁、PPG 和按键。
8. 调用 `PWR_EnterStopState(...WFI)` 进入 STOP0。
9. 外部中断或 RTC 唤醒后重新配置 64 MHz HSE PLL。
10. 恢复 BLE 硬件接口、I2C1、I2C2、SPI2 和 SC7A20 PA9 中断。
11. 用 RTC 差值补偿 RT-Thread tick。
12. 恢复 SysTick。

STOP0 前不会切断 PA11/PC13 传感器电源；真正切断传感器电源只发生在关机进入 Standby 前。

### 16.7 RTC tick 补偿

Sleep/STOP0 会关闭 SysTick，否则 1 ms 中断会立刻把 MCU 唤醒。代码使用 RTC 256 Hz 亚秒计数记录睡眠前后时间，将经过时间换算为 RT-Thread tick，并调用 `rt_timer_check()` 处理睡眠期间已经到期的软件定时器。

`tick_fraction` 保存 256 Hz 到 1000 Hz 换算无法整除的余数，避免多次短睡眠产生累计时间漂移。

### 16.8 长按关机和 Standby

运行时按下 PA0：

1. EXTI0 调用 `app_power_key_irq()`。
2. 启动周期 20 ms 的软定时器。
3. 提前松开则取消，`g_key_cancel_count` 加 1。
4. 持续 2000 ms 后设置 `app_shutdown` 并释放 `power_sem`。
5. power 任务醒来调用 `app_power_off()`。
6. 停止 PPG，停止 BLE，拉低 PA11/PC13 切断传感器电源。
7. 外部相关引脚改为模拟输入，减少数字输入漏电。
8. 等待按键释放，关闭 SysTick 和普通中断。
9. 只保留 PA0 WKUP，进入 Standby。

Standby 唤醒相当于一次新的启动，程序重新从复位流程开始。

## 17. 常用状态变量怎么读

### PPG

| 变量 | 意义 |
| --- | --- |
| `PPG_TotalFrames` | 累计成功写入环形缓冲的双波长帧数 |
| `PPG_Gaps` | FIFO 溢出、DMA 故障或配对失步次数 |
| `PPG_QueueDrops` | SpO2 作业队列满次数 |
| `PPG_FifoIrqCount` | PB3 水线中断次数 |
| `PPG_FifoReadCount` | DMA 成功读取批次数 |
| `PPG_Streaming` | IPA1322 是否正在连续采样 |
| `PPG_InitAttempts/Failures` | IPA1322 初始化尝试/失败次数 |
| `g_ppg_red_660nm/g_ppg_ir_905nm` | 最新原始 20 位符号 ADC 值的浮点快照 |

### 算法

| 变量 | 意义 |
| --- | --- |
| `SpO2_Latest` | 最新完整算法结果快照 |
| `SpO2_CopyFailures` | 600 帧窗口复制失败次数 |

### BLE

| 变量 | 意义 |
| --- | --- |
| `BLE_ConnectCount/DisconnectCount` | 连接/断开累计次数 |
| `BLE_NotifyAttempts` | Notify 尝试次数 |
| `BLE_NotifyQueued` | 协议栈接受的 Notify 数量 |
| `BLE_NotifyRejected` | 协议栈拒绝的 Notify 数量 |
| `BLE_BusyAfterRun` | 执行 64 轮后协议栈是否仍忙 |

### 低功耗

| 变量 | 意义 |
| --- | --- |
| `g_pm_sleep_count` | 普通浅 Sleep 次数 |
| `g_pm_ble_sleep_count` | 连接态关闭 SysTick 的 Sleep 次数 |
| `g_pm_connected_sleep_attempts` | 实际尝试连接态 Sleep 的次数 |
| `g_pm_stop_count` | 真正进入 STOP0 并返回的次数 |
| `g_pm_stop_last_rtc_units` | 最近一次 STOP0 时长，单位 1/256 秒 |
| `g_pm_runtime_sleep_disabled` | 1 表示运行期低功耗被关闭 |
| `g_pm_connected_sleep_block_mask` | 最近一次连接态 Sleep 被阻止的原因位图 |

连接态阻塞位：`0x01` 运行期禁睡、`0x02` 关机、`0x04` 有锁、`0x08` PPG 忙、`0x10` BLE 忙、`0x20` 按键按下、`0x40` BLE monitor 握手失败。

## 18. 最常修改的参数

### `User/app_config.h`

- `APP_SYSCLK_HZ`：系统主频，当前 64 MHz。
- `APP_LOW_POWER_ENABLE`：运行期低功耗总开关。
- `PPG_FRAME_TICKS`：IPA1322 帧周期。
- `PPG_WINDOW`：算法窗口，当前 600 帧。
- `PPG_STEP`：算法更新步进，当前 75 帧。
- `APP_*_TASK_PRIORITY`：任务优先级。
- `APP_*_TASK_STACK_SIZE`：任务静态栈。
- `POWER_KEY_ON_HOLD_MS`：待机唤醒开机确认时间。
- `POWER_KEY_OFF_HOLD_MS`：运行期长按关机时间。
- `BATTERY_PACKET_INTERVAL`：电池包间隔。

### `mydrivers/IPA1322/IPA1322.c`

`IPA1322_Init()` 开头的 `IPA1322_Config` 是光学采集集中调参区，包括 LED 电流、ADC OSR、TIA、电容、IIR 和采样时序。

### `mydrivers/SC7A20/SC7A20.h`

- `SC7A20_MOTION_THRESHOLD`：运动阈值，每 LSB 16 mg。
- `SC7A20_MOTION_DURATION`：运动持续时间，每 LSB 40 ms。

## 19. 出现问题时从哪里开始查

### 没有 PPG 数据

依次检查：

1. `PPG_Streaming` 是否为 1。
2. `PPG_InitAttempts` 是否增长，`PPG_InitFailures` 是否增长。
3. `PPG_FifoIrqCount` 是否增长。
4. `PPG_FifoReadCount` 是否增长。
5. `g_hw_i2c_error_count` 是否增长。
6. `PPG_Gaps` 是否持续增长。

### 有 PPG 但心率不稳定

依次检查：

1. 原始 `g_ppg_red_660nm/g_ppg_ir_905nm` 是否饱和或过小。
2. `PPG_Gaps` 是否增长，确保窗口连续。
3. `time_pair_count` 是否至少为 2。
4. `time_valid` 与 `fft_valid` 哪一路失败。
5. 比较 `hr_time` 和 `hr_fft`，判断峰谷检测问题还是频谱问题。

### BLE 断开或不发送

依次检查：

1. `BLE_ConnectCount` 和 `BLE_DisconnectCount`。
2. 手机是否真正写入 CCC 开启 Notify。
3. `BLE_NotifyAttempts/Queued/Rejected`。
4. `BLE_BusyAfterRun` 是否长期为 1。
5. `flag_bt_irq`、`flag_bt_enter_sleep` 和 `g_pm_connected_sleep_block_mask`。

### 功耗高

依次检查：

1. `APP_LOW_POWER_ENABLE` 是否为 1。
2. `g_pm_runtime_sleep_disabled` 是否为 0。
3. 连接态看 `g_pm_ble_sleep_count` 是否增长。
4. 未连接态看 `g_pm_stop_count` 是否增长。
5. 根据 `g_pm_connected_sleep_block_mask` 找到阻塞模块。
6. 检查 `PM_FIFO/PM_DMA/PM_ALGO/PM_BLE/PM_SYSTEM` 是否存在未释放的引用计数。

读懂工程时始终抓住三条主线：数据通过消息队列向后流动，中断只负责唤醒任务，所有任务都阻塞后才由 idle hook 选择低功耗模式。
