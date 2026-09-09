# 固件代码结构与维护指南

本文描述当前实际运行的 V1.0 固件。业务逻辑集中在 `User/app_*.c`，器件细节位于 `mydrivers`；维护功能时先改应用层接口，不要让 BLE、Flash 或算法直接操作彼此的内部变量。

## 1. 推荐阅读顺序

1. `app_config.h`：统一查看采样、窗口、任务栈、功耗和按键配置。
2. `app_main.c`：启动顺序和模块依赖。
3. `app_device.c`：设备状态、RTC 和采集调度策略。
4. `app_ppg.c`：传感器电源、FIFO/DMA、Ring Buffer 和算法作业。
5. `app_spo2.c`：算法执行、心率稳定、结果补充和投递。
6. `app_record.c`：Flash 布局、记录生命周期和历史同步。
7. `app_ble.c`、`app_protocol.c`、`app_packet.c`：控制命令、发送优先级和数据包。
8. `app_power.c`：按键、低功耗锁和 Standby。

## 2. 总体数据流

```text
BLE连接 + CCCD订阅 / record_enable
              |
              v
app_device RTC绝对时间状态机
              |
              v
app_ppg 控制电源 -> IPA1322 FIFO中断 -> DMA -> 768帧Ring
              |                              |
              | Raw(可选)                    | PPG_Job
              v                              v
          app_ble                        app_spo2
                                             |
                               +-------------+-------------+
                               |                           |
                         实时结果队列                 app_record队列
                               |                           |
                               +----------> app_ble <------+
                                              |
                                   Notify / 历史分片同步
```

线程均使用静态栈、静态消息队列或信号量。没有常驻超级轮询，等待阶段阻塞或进入低功耗。

## 3. 模块边界

### app_device：状态与调度唯一入口

`app_device_status_t` 保存输出模式、周期、记录开关、RTC、连接/订阅、传感器、运动和错误。合法输出模式：

- `APP_OUTPUT_RAW_ONLY`
- `APP_OUTPUT_ALGO_ONLY`
- `APP_OUTPUT_RAW_ALGO`

`sample_period_sec=1` 表示连续采集；周期模式只接受 `>=15 s`。设备记录开启时强制 15 s，不允许外部改周期。

周期采集窗口固定 10 s。`next_sample_time` 按“上一个窗口起点 + 周期”推进，算法耗时不会累积到周期中。RTC 500 ms 唤醒仅驱动状态机；真正关机的 Standby 不启用周期唤醒。

### app_ppg：采集链路所有者

采集开启时才依次上电并初始化 CW2015、SC7A20、IPA1322；等待 `SENSOR_POWER_STABLE_MS` 后开始采样。周期等待时关闭光学 AFE，保留加速度计低功耗运动检测；既没有订阅也没有设备记录时关闭全部传感器。

PB3 FIFO 水线中断只唤醒任务，FIFO 读取和 DMA 等待在任务上下文执行。双波长 Tag 配对成功后写入 768 帧 Ring。FIFO 溢出、DMA 异常或 Tag 断流会使当前代数失效，算法不会拼接断流前后数据。

连续模式每 75 帧创建一次 600 帧算法作业；周期模式在窗口累计 750 帧后创建一次作业。Raw 模式每两个样本组成一个 20 B 包。

### app_spo2：算法任务

收到 `PPG_Job` 后复制同一代数的最后 600 帧，执行 `SpO2_Calculate()`，再附加 RTC 时间、SC7A20 运动结果、质量等级与体温预留值。结果分别投递给 BLE 和记录模块。

算法模块使用通用 R 表，`SPO2_CALIBRATION_CONFIRMED=0`；这不是医疗标定结果。体温 V1 固定为 0。

### app_record：Flash 记录

记录区固定为末尾 200 KiB：

```text
0x08000000 ~ 0x0804DFFF  应用 IROM1，312 KiB
0x0804E000 ~ 0x0804E7FF  Metadata A，2 KiB
0x0804E800 ~ 0x0804EFFF  Metadata B，2 KiB
0x0804F000 ~ 0x0807FFFF  数据，196 KiB
```

每条 `app_record_t` 恰好 16 B，包含时间、序号、SpO2、HR、运动、质量、体温预留、标志与 CRC16。容量为 12,544 条，在 15 s 周期下约 52.27 小时。

开始记录由低优先级 `record` 线程异步逐页擦除，并在页间让出 CPU；擦除期间状态可查询。两页元数据用于冗余，会话启动先写不可变字段，停止/关机补写计数与结束时间。启动时逐条校验 CRC 恢复写指针。Flash 满后停止写入、保留已有数据并上报错误。

历史同步读取指定序号后的记录，经独立低优先级队列发送；断开后上位机以最后序号继续。

### app_ble：协议栈与优先级

BLE 任务不计算算法，也不擦除 Flash。发送顺序固定：

1. 控制应答、状态和错误；
2. 实时 Raw；
3. 实时算法结果；
4. 一条历史记录。

每轮发送后继续驱动厂商协议栈，避免历史同步饿死 BLE 控制。只有物理连接且已订阅 Notify 才发送和触发实时采集。

控制帧由 `app_protocol` 统一编码/解码和 CRC16 校验；固定实时包由 `app_packet` 构造。不要在 `app_ble` 里手写新的偏移常量。

### app_power：功耗与按键

PA0 开机和运行期关机均要求连续按住 2 s。开机确认点亮 PA6，松开后进入业务启动；关机确认立即熄灯，停止采集，等待 BLE/算法退出并刷新记录元数据，然后进入 Standby。

`PM_BLE`、`PM_PPG`、`PM_ALGO` 等锁表示当前不能深睡。业务模块只通过 `app_power_lock/unlock` 声明需求，不直接决定全局睡眠状态。

## 4. 协议维护规则

应用控制帧最大 20 B：`AA55 + version + command + sequence + length + payload + CRC16`，payload 最大 10 B。超过 10 B 的状态、记录信息和历史记录采用 `{part,total,8字节数据}` 分片。

固定实时包：

- `A5 01`：算法结果、质量、RTC、SpO2、HR、PI、体温预留。
- `A5 02`：电量和最近结果序号。
- `A5 04`：两组 Raw 红/红外样本。

修改协议时必须同步更新 `tool/protocol.py` 与 `tool/test_suite.py`。记录结构已经持久化；若改变 `app_record_t`，必须提升 `RECORD_FORMAT_VERSION` 并提供旧格式迁移或明确废弃策略。

## 5. RAM 设计

主要静态占用来自算法工作区、PPG Ring、算法窗口和任务栈。当前优化：

- FFT 的红/红外计算复用两个 2048-float 缓冲区，取消第三个缓冲区：约省 8 KiB。
- RT-Thread heap 由 24 KiB 调为 8 KiB；应用线程和 IPC 均静态创建：省 16 KiB。
- Ring Entry 不再重复保存可由位置推导的序号：省约 3 KiB。
- 删除 Raw VOFA 全局镜像和电量包中的调试镜像。

不要随意扩大 `PPG_RING_SIZE`、`PPG_WINDOW` 或任务栈。变更后以 Keil map/size 结果和最长路径栈测试为依据，而不是凭感觉缩栈。

## 6. 错误与必要观测量

`APP_ERR_*` 区分 RTC 未同步、传感器初始化、FIFO 溢出、Flash、协议、模式和周期错误。设备状态命令可读最后错误。

仍保留的全局计数器用于现场定位时序和丢数，例如 FIFO 溢出、DMA 超时、BLE Notify 拒绝、算法窗口复制失败。它们是低成本诊断计数，不是每帧波形镜像；确认产品稳定后可通过统一诊断开关裁剪，不应零散删除。

## 7. 修改检查清单

- 是否保持 BLE 控制不被算法/Flash 阻塞？
- 是否保持 ISR 只做最少工作，不在 ISR 里写 Flash？
- 周期是否仍以 RTC 绝对起点推进？
- 断开 BLE 后，`record_enable=1` 是否仍采集和记录？
- Flash 地址是否仍完全位于 `0x0804E000~0x0807FFFF`？
- 新缓冲区是否有明确所有者、生命周期和容量上限？
- 固件协议、上位机解析、测试和文档是否一起更新？
- 是否完成 Keil 构建、Python 测试、算法对照和真机回归？
