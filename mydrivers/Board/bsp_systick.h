/**
 * @file    bsp_systick.h
 * @brief   板级系统节拍与延时接口定义
 * @details 提供基于 RT-Thread 的毫秒延时、基于 DWT 的微秒延时以及系统毫秒时间戳获取接口。
 */

#ifndef __BSP_SYSTICK_H__
#define __BSP_SYSTICK_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  毫秒级延时函数（释放 CPU 控制权）
 * @param  ms 延时毫秒数
 * @note   内部调用 rt_thread_mdelay，会引起当前线程挂起让出 CPU，
 *         严禁在中断服务函数（ISR）或关闭调度锁的上下文中调用。
 */
void delay_ms(uint32_t ms);

/**
 * @brief  微秒级阻塞延时函数（基于 Cortex-M4 DWT 周期计数器）
 * @param  us 延时微秒数
 * @note   本函数为死循环忙等待，专门用于 I2C/SPI 等总线微秒级建立保持时间，
 *         不可用于长时间延时，以免增加功耗和影响高优先级线程实时性。
 */
void delay_us(uint32_t us);

/**
 * @brief  获取当前系统启动以来的运行毫秒数
 * @return 当前系统 tick 换算后的毫秒数
 */
uint32_t systick_get_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SYSTICK_H__ */
