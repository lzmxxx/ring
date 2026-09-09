/**
 * @file    app_ble.h
 * @brief   蓝牙 BLE 应用任务与服务接口定义
 * @details 负责 N32WB452 蓝牙协议栈初始化、连接回调、GATT 特征通知（Notify）以及数据包队列解耦管理。
 */

#ifndef __APP_BLE_H__
#define __APP_BLE_H__

#include <rtthread.h>
#include "SpO2.h"
#include "app_record.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 全局就绪标志 */
extern volatile rt_bool_t BLE_Ready;

typedef enum
{
    APP_BLE_EVENT_RECORD_ERASING = 1U,
    APP_BLE_EVENT_RECORD_READY,
    APP_BLE_EVENT_RECORD_STARTED,
    APP_BLE_EVENT_RECORD_STOPPED,
    APP_BLE_EVENT_RECORD_ERASED,
    APP_BLE_EVENT_FLASH_FULL,
    APP_BLE_EVENT_SYNC_END
} app_ble_event_t;

/**
 * @brief  初始化并启动 BLE 应用任务
 * @return 0 成功，-1 失败
 * @note   线程优先级配置为 4（高实时性任务），栈空间 2048 字节。
 */
int app_ble_init(void);

/**
 * @brief  唤醒 BLE 任务处理协议栈事件或发送队列
 * @note   可安全在 ISR、定时器或其它任务上下文中调用，通过释放信号量触发 `ble_thread` 运行。
 */
void ble_wakeup(void);

/**
 * @brief  手机端 CCC (Client Characteristic Configuration) 订阅状态变更通知
 * @param  enabled RT_TRUE 开启通知，RT_FALSE 关闭通知
 */
void ble_subscription_changed(rt_bool_t enabled);

/**
 * @brief  查询当前是否允许发送 Notify 数据包
 * @return RT_TRUE 允许发送（已连接且已订阅），RT_FALSE 不允许
 */
rt_bool_t ble_notifications_enabled(void);

/**
 * @brief  投递最新的血氧心率算法解算结果到 BLE 发送队列
 * @param  result 算法解算结果指针
 * @note   采用丢旧保新策略：若队列已满，弹出最旧项并推入最新项，确保蓝牙始终上报最新生理读数。
 */
void app_ble_post(const SpO2_Result *result);
void app_ble_post_raw(int32_t red, int32_t ir, uint32_t sequence);
void app_ble_post_event(app_ble_event_t event, uint32_t value);
void app_ble_post_error(uint8_t command, uint8_t error);
rt_bool_t app_ble_post_history(const app_record_t *record);

/**
 * @brief  停止 BLE 任务与通知发送（系统准备关机前调用）
 */
void app_ble_stop(void);

/**
 * @brief  查询 BLE 协议栈是否处于空闲态（可进入低功耗）
 * @return RT_TRUE 空闲，RT_FALSE 正在收发或有未处理事件
 */
rt_bool_t app_ble_idle(void);

/**
 * @brief  查询当前是否处于蓝牙连接状态
 * @return RT_TRUE 已与主机建立连接，RT_FALSE 未连接
 */
rt_bool_t app_ble_connected(void);

/**
 * @brief  查询当前是否允许系统进入深度 STOP0 停机模式
 * @return RT_TRUE 允许（空闲且未处于连接态），RT_FALSE 不允许
 * @note   连接状态下保持当前系统时钟和 BLE 通信，仅进入普通 Sleep。
 */
rt_bool_t app_ble_stop_allowed(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_BLE_H__ */
