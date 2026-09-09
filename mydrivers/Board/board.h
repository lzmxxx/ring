/**
 * @file    board.h
 * @brief   RT-Thread 板级支持包头文件
 * @details 负责系统上电后的底层硬件初始化入口声明，包括中断分组、时钟、系统节拍及动态内存堆。
 */

#ifndef __BOARD_H__
#define __BOARD_H__

#include "n32wb452.h"
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  RT-Thread 板级硬件底层初始化
 * @details 由 RT-Thread 启动流程在调度器启动前调用。完成以下配置：
 *          1. NVIC 中断优先级分组（Group 2: 2位抢占，2位响应）；
 *          2. 系统主频切换至 64MHz（HSE 32MHz + PLL x2）；
 *          3. 配置 SysTick 产生 RT-Thread 系统心跳（默认 1000Hz）；
 *          4. 初始化 RT-Thread 动态内存堆空间（24KB）；
 *          5. 启动 Cortex-M4 DWT 周期计数器以支持微秒级精准延时。
 * @note   此时调度器尚未就绪，严禁在本函数内部调用任何可能引起任务阻塞或依赖 IPC 的函数。
 */
void rt_hw_board_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_H__ */
