/**
 * @file    CW2015.h
 * @brief   CW2015 电池电量计应用驱动头文件
 * @details 负责电芯建模曲线烧录校验、快速开路电压自校准、电池毫伏级电压采样与 SOC 电量读取。
 */

#ifndef __CW2015_H__
#define __CW2015_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief CW2015 采样数据结构体
 */
typedef struct
{
    uint8_t  Capacity;       /**< 电池剩余容量百分比（单位：%，范围 0 ~ 100） */
    uint16_t CapacityX100;   /**< 电池剩余容量百分比（单位：0.01%，范围 0 ~ 10000） */
    uint16_t VoltageMv;      /**< 电池当前端电压（单位：mV，经 3 样本中值滤波） */
} CW2015_Data;

/* 全局调试状态与扫描结果 */
extern volatile uint8_t g_cw2015_i2c_ack;       /**< 0 表示 I2C2 成功应答，1 表示无应答 */
extern volatile uint8_t g_cw2015_version;       /**< 读取到的芯片版本寄存器值 */
extern volatile uint8_t g_cw2015_address;       /**< 实际通信命中的 7 位从机地址 */
extern volatile uint8_t g_i2c2_device_count;   /**< I2C2 总线扫描发现的设备总数 */
extern volatile uint8_t g_i2c2_addresses[8];   /**< I2C2 扫描发现的设备地址列表 */

/**
 * @brief  初始化 CW2015 电池电量计
 * @return 0 成功初始化（曲线校验通过且进入正常采样），1 初始化失败或未检测到芯片
 * @note   1. 自动扫描 I2C2 地址确认芯片就绪；
 *         2. 检查 64 字节电池曲线是否已更新；若未更新或数据损坏则重新写入并校验；
 *         3. 触发 QuickStart 重新以当前开路电压评估电量；
 *         4. 最多轮询 3 秒等待 SOC 有效值。
 */
uint8_t CW2015_Init(void);

/**
 * @brief  读取芯片版本号
 * @param  Version 输出版本号指针
 * @return 0 成功，1 失败
 */
uint8_t CW2015_ReadVersion(uint8_t *Version);

/**
 * @brief  读取当前电池电压（mV）与剩余电量百分比（SOC）
 * @param  Data 输出数据结构体指针
 * @return 0 读取成功且数据在合理范围（2500mV ~ 5000mV），1 通信失败或数据异常
 * @note   对电压连续采样 3 次取中值，滤除充电或负荷跳变引起的偶然毛刺。
 */
uint8_t CW2015_ReadData(CW2015_Data *Data);

/**
 * @brief  设置低电量硬件告警阈值
 * @param  Percent 告警百分比（1% ~ 31%）
 * @return 0 成功，1 失败或参数超限
 */
uint8_t CW2015_SetAlertThreshold(uint8_t Percent);

/**
 * @brief  查询并清除低电量告警标志位
 * @param  WasActive 输出此前是否触发了告警（1 触发，0 未触发）
 * @return 0 成功，1 失败
 */
uint8_t CW2015_ClearAlert(uint8_t *WasActive);

/**
 * @brief  软重启 CW2015 内部逻辑
 * @return 0 成功，1 失败
 */
uint8_t CW2015_Reset(void);

/**
 * @brief  执行快速启动（Quick Start）
 * @details 强制电量计跳过长时程平滑滤波，立即以当前开路电压（OCV）查找曲线重置 SOC。
 * @return 0 成功，1 失败
 */
uint8_t CW2015_QuickStart(void);

#ifdef __cplusplus
}
#endif

#endif /* __CW2015_H__ */
