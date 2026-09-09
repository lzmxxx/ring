/**
 * @file    IPA1322_Debug.h
 * @brief   IPA1322 调试诊断与状态观测变量声明
 * @details 暴露给上位机（如 MKLink AI Probe、VOFA+、调试器）的运行状态诊断全局变量。
 */

#ifndef __IPA1322_DEBUG_H__
#define __IPA1322_DEBUG_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 初始化结果状态码定义 (g_ipa1322_init_res):
 * 0: 正常成功
 * 1: I2C 从机地址 0x48 无 ACK 应答
 * 2: 芯片 ID 不匹配 (预期 0xAF)
 * 3: 核心配置寄存器写入后回读不一致
 * 4: 启动连续转换命令失败
 * 5: 运行中总线通信失败或 FIFO 溢出
 * 6: 停采命令失败
 * 7: 用户参数非法 (如时序重叠、采样窗口超出一帧周期)
 */
extern volatile uint8_t  g_ipa1322_init_res;

/* 总线扫描与应答状态 */
extern volatile uint8_t  g_i2c_ack_found;           /**< 成功应答的从机地址 */
extern volatile uint8_t  g_i2c_probe_0x48_res;      /**< 0x48 探测结果 (1 成功，0 失败) */
extern volatile uint8_t  g_i2c_scan_count;          /**< I2C 扫描到的设备数 */
extern volatile uint8_t  g_i2c_scan_results[8];     /**< I2C 扫描到的设备地址列表 */

/* 芯片硬件信息与关键控制寄存器镜像 */
extern volatile uint8_t  g_ipa1322_chip_id;         /**< 芯片器件 ID (应为 0xAF) */
extern volatile uint8_t  g_ipa1322_reg19;           /**< 时钟配置寄存器 1 */
extern volatile uint8_t  g_ipa1322_reg1a;           /**< 时钟配置寄存器 2 */
extern volatile uint8_t  g_ipa1322_reg23;           /**< PD 选择与早期采样配置 */
extern volatile uint8_t  g_ipa1322_reg30;           /**< 时隙选择与中断使能寄存器 */
extern volatile uint8_t  g_ipa1322_reg2f;           /**< 运行控制寄存器 (Bit 0 为使能位) */
extern volatile uint8_t  g_ipa1322_fifo_cnt;        /**< 最近一次读取的 FIFO 计数值 */

/* 统计计数器 */
extern volatile uint32_t g_ipa1322_sample_cnt;      /**< 成功解析出的双通道有效数据对累计计数 */
extern volatile uint32_t g_ipa1322_fifo_overflow;   /**< 硬件 FIFO 溢出累计次数 */
extern volatile uint32_t g_ipa1322_bad_tags;        /**< 数据 Tag 异常或配对失步计数 */

/* 寄存器回读校验失败定位 */
extern volatile uint8_t  g_ipa1322_failed_reg;      /**< 回读校验失败的寄存器地址 */
extern volatile uint8_t  g_ipa1322_expected;        /**< 期望写入的数值 */
extern volatile uint8_t  g_ipa1322_actual;          /**< 实际回读的数值 */

/* 最新采样值快照 */
extern volatile int32_t  g_latest_red;              /**< 最新红光 (660nm) 信号值 */
extern volatile int32_t  g_latest_ir;               /**< 最新红外 (905nm) 信号值 */
extern volatile uint8_t  g_ipa1322_regs_dump[0x50]; /**< 全量寄存器快照导出缓冲区 */

#ifdef __cplusplus
}
#endif

#endif /* __IPA1322_DEBUG_H__ */
