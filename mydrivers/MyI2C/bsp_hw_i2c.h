/**
 * @file    bsp_hw_i2c.h
 * @brief   I2C1 硬件外设与 DMA1 接收驱动头文件
 * @details 负责 N32WB452 I2C1 (PB6/PB7) 硬件总线初始化、设备寻址探测、
 *          寄存器读写以及配合 DMA1 Channel 7 的高速非阻塞 FIFO 批量读取。
 */

#ifndef __BSP_HW_I2C_H__
#define __BSP_HW_I2C_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 全局调试与状态统计变量 */
extern volatile uint8_t  g_hw_i2c_last_err;     /**< 最近一次 I2C 错误阶段标识 */
extern volatile uint32_t g_hw_i2c_error_count;  /**< I2C 通信累计错误与总线复位计数 */
extern volatile uint16_t g_hw_i2c_error_status; /**< 发生错误时的 I2C1->STS1 寄存器快照 */

/**
 * @brief  初始化 I2C1 硬件外设及对应 GPIO (PB6 SCL, PB7 SDA)
 * @note   配置为 100kHz 标准模式，开漏复用输出，由板上 1.8V 域 4.7k 上拉电阻提供高电平。
 */
void bsp_hw_i2c_init(void);

/**
 * @brief  探测指定 7 位从机地址是否存在设备应答
 * @param  addr_7bit 7 位从机设备地址（例如 0x48）
 * @return 1 表示收到从机 ACK，0 表示 NACK 或通信超时
 */
uint8_t bsp_hw_i2c_probe_addr(uint8_t addr_7bit);

/**
 * @brief  扫描 I2C1 总线上的所有活动从机地址 (0x08 ~ 0x77)
 * @param  found_addrs 用于存放探测到的有效从机地址数组
 * @param  max_count   数组容量上限
 * @return 探测到的从机设备总数
 */
uint8_t bsp_hw_i2c_scan(uint8_t *found_addrs, uint8_t max_count);

/**
 * @brief  启动 I2C1 DMA 接收流程（从器件内部寄存器开始读取批量数据）
 * @param  reg  要读取的起始寄存器地址（如 IPA1322 FIFO 数据寄存器）
 * @param  data 接收目标缓冲区指针
 * @param  size 接收字节数（必须 >= 2）
 * @return 1 表示成功配置并触发 DMA 读取，0 表示总线忙或参数错误
 * @note   本函数为非阻塞启动。启动后 CPU 让出，由 DMA1_Channel7 自动搬运数据；
 *         传输完成后在中断中触发 `bsp_hw_i2c_dma_finish_isr()` 并释放信号量通知采集线程。
 */
uint8_t bsp_hw_i2c_dma_read_start(uint8_t reg, uint8_t *data, uint16_t size);

/**
 * @brief  DMA1 Channel 7 接收完成中断回调处理
 * @note   在 `DMA1_Channel7_IRQHandler` 中被调用，关闭 DMA、停止总线并清除忙状态标志。
 */
void bsp_hw_i2c_dma_finish_isr(void);

/**
 * @brief  强行终止当前正在进行的 DMA 传输并复位总线
 * @note   用于 DMA 超时或异常错误时的状态清理。
 */
void bsp_hw_i2c_dma_abort(void);

/**
 * @brief  查询当前 DMA 接收通道是否正处于活动忙碌状态
 * @return true 表示 DMA 正在接收，false 表示通道空闲
 */
bool bsp_hw_i2c_dma_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_HW_I2C_H__ */
