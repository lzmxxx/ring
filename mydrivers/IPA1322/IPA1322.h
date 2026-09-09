/**
 * @file    IPA1322.h
 * @brief   IPA1322 (VT2102兼容) 双波长光学 AFE 传感器驱动头文件
 * @details 负责模拟前端时钟时序配置、LED 恒流源驱动、TIA 增益、二阶环境光消除、FIFO 硬件水线与数据包解析。
 */

#ifndef __IPA1322_H__
#define __IPA1322_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 双波长配对 PPG 数据结构体
 * @note  二阶环境光消除 (Dark0 - Light - Dark1)，包含 20 位带符号信号值
 */
typedef struct
{
    int32_t red_signal; /**< LED1: 660nm 红光通道信号值 */
    int32_t ir_signal;  /**< LED0: 905nm 红外通道信号值 */
} IPA1322_Data;

/* 硬件 FIFO 参数规格 */
#define IPA1322_FIFO_DEPTH          64U /**< 芯片内部 FIFO 最大深度 (64 entries) */
#define IPA1322_FIFO_ENTRY_BYTES    3U  /**< 每个 FIFO 样本点占用 3 字节 (24 bits: 4-bit Tag + 20-bit Data) */

/**
 * @brief  初始化 IPA1322 AFE 芯片
 * @return 0 成功，1 失败（详细失败原因见 IPA1322_Debug.h）
 * @note   初始化所有发光/采样时序、TIA 跨阻增益、IIR 滤波器，但暂不开启连续转换。
 */
uint8_t IPA1322_Init(void);

/**
 * @brief  正式启动 IPA1322 连续采样转换与中断使能
 * @return 0 成功，1 失败
 * @note   必须在 MCU 外部中断 (EXTI3) 与采集线程准备就绪后调用，避免丢失第一个水满中断。
 */
uint8_t IPA1322_Start(void);

/**
 * @brief  停止 IPA1322 连续采样并关闭水满/溢出中断
 */
void IPA1322_Stop(void);

/**
 * @brief  清空芯片内部硬件 FIFO 缓存及驱动层配对悬挂状态
 */
void IPA1322_ClearFIFO(void);

/**
 * @brief  获取当前芯片 FIFO 中已积攒的有效样本 entry 数量 (0 ~ 64)
 * @return entry 数量
 */
uint8_t IPA1322_GetFIFOCount(void);

/**
 * @brief  读取并清除中断状态寄存器
 * @return 中断标志字节（Bit 5: FIFO 溢出中断，Bit 6: FIFO 水满中断等）
 */
uint8_t IPA1322_ReadInterruptFlags(void);

/**
 * @brief  解析由 DMA 批量读取的原始 FIFO 字节流，并配对为双通道数据
 * @param  raw       原始 DMA 字节流首地址
 * @param  entries   本次读取的样本条数 (entries)
 * @param  pairs     输出的数据对存储数组
 * @param  max_pairs 允许输出的最大对数
 * @return 成功配对输出的完整 PPG 数据对数
 */
uint8_t IPA1322_ParseFIFO(const uint8_t *raw, uint8_t entries,
                          IPA1322_Data *pairs, uint8_t max_pairs);

/**
 * @brief  阻塞轮询方式直接读取并解析当前 FIFO 数据（备用接口）
 * @param  pairs     输出数据对数组
 * @param  max_pairs 数组容量
 * @return 实际解析输出对数
 */
uint8_t IPA1322_ReadPairs(IPA1322_Data *pairs, uint8_t max_pairs);

#ifdef __cplusplus
}
#endif

#endif /* __IPA1322_H__ */
