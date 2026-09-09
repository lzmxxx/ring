/**
 * @file    app_config.h
 * @brief   系统全局应用层宏配置
 * @details 集中管理 PPG 采样率、算法窗口尺寸、环形缓冲区深度、按键长按时间及协议上报周期等。
 */

#ifndef __APP_CONFIG_H__
#define __APP_CONFIG_H__

/*
 * PPG 采样频率与时基配置：
 * IPA1322 使用 4MHz 主时钟，内部固定 122 分频生成 32.786kHz 计数时钟；
 * 437 个 Ticks 对应帧周期 13.328ms，即名义 75.028Hz 采样率。
 */
#define PPG_FRAME_TICKS             437U                                    /**< 单帧计数值 (4MHz / (122 * 437) ≈ 75.028Hz) */
#define PPG_SAMPLE_RATE             (4000000.0f / (122.0f * (float)PPG_FRAME_TICKS)) /**< 真实采样率浮点常数 */

/* 算法滑动分析窗口配置 */
#define PPG_STEP                    75U     /**< 算法分析步进帧数（75 帧对应约 1.0 秒更新一次） */
#define PPG_WINDOW                  600U    /**< 算法单次分析窗口长度（600 帧对应约 8.0 秒历史波形） */
#define PPG_RING_SIZE               768U    /**< 约10.2秒，满足需求中的10秒以上连续缓冲 */

/* RT-Thread 任务配置：数值越小，优先级越高。 */
#define APP_POWER_TASK_STACK_SIZE   768U
#define APP_POWER_TASK_PRIORITY     3U
#define APP_POWER_TASK_TIMESLICE    10U

#define APP_BLE_TASK_STACK_SIZE     2048U
#define APP_BLE_TASK_PRIORITY       4U
#define APP_BLE_TASK_TIMESLICE      5U

#define APP_PPG_TASK_STACK_SIZE     1536U
#define APP_PPG_TASK_PRIORITY       6U
#define APP_PPG_TASK_TIMESLICE      5U

#define APP_SPO2_TASK_STACK_SIZE    4096U
#define APP_SPO2_TASK_PRIORITY      10U
#define APP_SPO2_TASK_TIMESLICE     10U

#define APP_STATE_TASK_STACK_SIZE   768U
#define APP_STATE_TASK_PRIORITY     8U
#define APP_STATE_TASK_TIMESLICE    5U

#define APP_RECORD_TASK_STACK_SIZE  1024U
#define APP_RECORD_TASK_PRIORITY    12U
#define APP_RECORD_TASK_TIMESLICE   5U

#define APP_RESULT_QUEUE_DEPTH      4U
#define APP_PPG_JOB_QUEUE_DEPTH     4U

/* 系统运行配置：修改这里即可测试主频和功耗。 */
#define APP_DEBUG                   0U
#define APP_SYSCLK_HZ               64000000UL
#define APP_LOW_POWER_ENABLE        1U
#define APP_DISABLE_RUNTIME_SLEEP   ((APP_LOW_POWER_ENABLE) ? 0U : 1U)

#if ((APP_SYSCLK_HZ != 48000000UL) && (APP_SYSCLK_HZ != 64000000UL) && (APP_SYSCLK_HZ != 96000000UL) && (APP_SYSCLK_HZ != 128000000UL))
#error "APP_SYSCLK_HZ must be 48000000UL, 64000000UL, 96000000UL, or 128000000UL"
#endif

/* 物理按键 (PA0) 防误触长按时间门限 */
#define POWER_KEY_ON_HOLD_MS        2000U   /**< 待机唤醒后开机确认持续按住时间：2000 毫秒 */
#define POWER_KEY_OFF_HOLD_MS       2000U   /**< 系统运行期确认关机持续按住时间：2000 毫秒 */

/* 生理算法标定状态 */
#define SPO2_CALIBRATION_CONFIRMED  0       /**< 0 表示使用通用理论 R 表尚未经当前戒指光路临床标定，1 表示已标定 */

/* BLE 数据包组合与上报间隔 */
#define BATTERY_PACKET_INTERVAL     30U     /**< 每成功发送 30 个血氧心率包（约30秒），追加发送 1 个电池电量包 */
#define SENSOR_POWER_STABLE_MS      50U
#define PERIOD_ACQUIRE_SECONDS      10U

#endif /* __APP_CONFIG_H__ */
