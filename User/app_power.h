/**
 * @file    app_power.h
 * @brief   系统电源与多级低功耗管理头文件
 * @details 负责戒指物理按键防误触开机/关机判定、软件低功耗引用锁（PM Lock）、
 *          连接态 Sleep（WFI + RTC 补偿）、未连接态 STOP0（停机休眠）以及待机关机（Standby）控制。
 */

#ifndef __APP_POWER_H__
#define __APP_POWER_H__

#include <rtthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 软件低功耗引用锁枚举
 * @note  任意锁计数值大于 0 时，系统禁止进入深休眠模式以保护关键数据流。
 */
typedef enum
{
    PM_FIFO = 0,    /**< FIFO 读取锁：IPA1322 FIFO 正在搬运 */
    PM_DMA,         /**< DMA 传输锁：I2C DMA 正在硬件移位 */
    PM_ALGO,        /**< 算法运算锁：SpO2 正在进行密集浮点计算 */
    PM_BLE,         /**< 蓝牙活动锁：BLE 正在组包或发送数据 */
    PM_SYSTEM,      /**< 系统主锁：开机初始化与关机保护期间持有 */
    PM_LOCK_COUNT   /**< 锁类型总数 */
} pm_lock_type_t;

/* 全局关机与调试统计状态 */
extern volatile rt_bool_t app_shutdown;                 /**< 系统是否正处于关机状态 */
extern volatile uint32_t  g_pm_stop_count;              /**< 成功进入并退出 STOP0 的总次数 */
extern volatile uint32_t  g_pm_stop_rtc_units;          /**< STOP0 停机累计时间 (以 256Hz 为单位，约 3.9ms) */
extern volatile uint32_t  g_pm_stop_last_rtc_units;     /**< 最近一次 STOP0 休眠时长 */
extern volatile uint32_t  g_pm_sleep_count;             /**< 浅睡眠 (Shallow Sleep WFI) 累计执行次数 */
extern volatile uint32_t  g_pm_stop_monitor_abort;      /**< 因射频监控握手未就绪而放弃进入停机的次数 */
extern volatile uint32_t  g_pm_ble_sleep_count;         /**< 蓝牙连接态睡眠 (Connected Sleep) 累计次数 */
extern volatile uint32_t  g_pm_ble_sleep_rtc_units;     /**< 蓝牙连接态睡眠累计时间 (256Hz 为单位) */
extern volatile uint8_t   g_pm_runtime_sleep_disabled;  /**< 1 临时禁止运行期Sleep/STOP0，供PPG内存采集 */
extern volatile uint32_t  g_pm_connected_sleep_attempts; /**< 已实际执行 Connected Sleep WFI 的次数 */
extern volatile uint8_t   g_pm_connected_sleep_block_mask; /**< 最近一次未进入 Connected Sleep 的阻塞原因位图 */
extern volatile uint32_t  g_key_press_ms;               /**< 当前物理按键持续按下的累计毫秒数 */
extern volatile uint32_t  g_key_cancel_count;           /**< 按键未达到长按门限提前释放的取消次数 */
extern volatile uint32_t  g_key_shutdown_count;         /**< 达到关机门限触发关机的次数 */
extern volatile uint32_t  g_power_off_stage;            /**< 关机执行所处的阶段诊断码 (1~6) */

/**
 * @brief  初始化电源管理模块
 * @note   初始化按键扫描软定时器、关机执行任务，并将 `idle_hook` 挂入 RT-Thread 空闲线程钩子。
 */
void app_power_init(void);

/**
 * @brief  获取指定的低功耗锁（引用计数递增）
 * @param  lock 锁类型枚举（PM_FIFO, PM_DMA, PM_ALGO, PM_BLE, PM_SYSTEM）
 */
void app_power_lock(unsigned lock);

/**
 * @brief  释放指定的低功耗锁（引用计数递减）
 * @param  lock 锁类型枚举
 */
void app_power_unlock(unsigned lock);

/**
 * @brief  Standby 唤醒后的开机有效性校验
 * @return RT_TRUE 确认开机（按键长按持续达到 1.5 秒），RT_FALSE 判定为短时误触
 * @note   若判定为误触，主函数将直接命令系统重新进入 Standby，不唤醒任何传感器。
 */
rt_bool_t app_power_boot_check(void);

/**
 * @brief  配置芯片切入 Standby 待机模式（极低功耗关机，等待 PA0 WKUP 上升沿开机）
 */
void app_power_enter_standby(void);

/**
 * @brief  执行完整优雅关机流程
 * @details 停止数据采集、断开蓝牙、切断传感器 1.8V 负载开关、将所有引脚置为模拟输入，最后切入 Standby。
 */
void app_power_off(void);

/**
 * @brief  按键 EXTI0 中断通知接口
 * @note   在 ISR 中调用，启动消抖与长按关机扫描定时器。
 */
void app_power_key_irq(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_POWER_H__ */
