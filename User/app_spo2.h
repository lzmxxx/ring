/**
 * @file    app_spo2.h
 * @brief   血氧算法应用任务接口定义
 * @details 负责创建并管理独立的 SpO2 算法计算工作线程，消费 PPG 环形缓冲数据并派发解算结果。
 */

#ifndef __APP_SPO2_H__
#define __APP_SPO2_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化并启动血氧心率算法应用线程
 * @return 0 成功，-1 失败
 * @note   线程优先级配置为 10（低于采集线程与 BLE 实时任务），栈空间 4096 字节。
 */
int app_spo2_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_SPO2_H__ */
