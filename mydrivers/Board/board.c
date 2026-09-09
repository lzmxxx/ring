/**
 * @file    board.c
 * @brief   RT-Thread 板级基础设施与硬件初始化实现
 *
 * 模块职责：
 * 1. 提供 RT-Thread 启动所需的硬件初始化入口 `rt_hw_board_init`；
 * 2. 分配 8KB 静态内存堆，应用线程、IPC 均使用静态对象；
 * 3. 实现 SysTick 中断服务函数以驱动操作系统时钟节拍；
 * 4. 提供基于 DWT 的硬件级微秒阻塞延时与基于 RT-Thread 的毫秒非阻塞延时。
 *
 * 与其他模块交互：
 * - 调用 `clock.c` 的 `SetSysClock_HSE_PLL()` 配置 APP_SYSCLK_HZ 指定的主频；
 * - 供驱动层（如 I2C/SPI 总线建立）调用 `delay_us()`；
 * - 供全工程业务线程调用 `delay_ms()` 与 `systick_get_ms()`。
 *
 * 中断关联：
 * - 占用 Cortex-M4 内核 SysTick 中断，优先级为最低或适配 RTOS 规则；
 * - NVIC 全局配置为 PriorityGroup_2（2 位抢占优先级，2 位子优先级）。
 *
 * 线程安全与低功耗：
 * - `rt_hw_board_init()` 在系统单线程/调度器未运行时执行；
 * - DWT 延时通过周期计数器差值计算，自然溢出安全；
 * - 戒指休眠（STOP0 或 Connected Sleep）时 SysTick 会被动态关闭与补偿，唤醒后由 power 模块恢复。
 */

#include "board.h"
#include "clock.h"
#include "bsp_systick.h"
#include "app_config.h"
#include "log.h"
#include "misc.h"
#include <rtthread.h>

/* 本工程的应用线程、栈、信号量和消息队列均为静态对象；堆只保留给
 * RT-Thread 通用服务的偶发分配。原 24 KiB 从未被应用层直接使用。 */
#define RT_HEAP_SIZE_BYTES  (8U * 1024U)
static rt_uint8_t system_heap[RT_HEAP_SIZE_BYTES];

/**
 * @brief  板级底层硬件初始化函数
 * @details 由 RT-Thread 内部机制 `rtthread_startup` 显式调用。
 *          初始化 NVIC 分组、系统主时钟、SysTick 时基、RT-Thread 堆及 DWT 计数器。
 */
void rt_hw_board_init(void)
{
    /* 1. NVIC 优先级分组：Group 2（2位抢占优先级：0~3，2位响应优先级：0~3） */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    /* 2. 配置主频：使用 32MHz 外部晶振，PLL 倍频由 APP_SYSCLK_HZ 决定。 */
    SetSysClock_HSE_PLL();
    SystemCoreClockUpdate();

    /* 3. 配置内核 SysTick 定时器，生成 RT-Thread 系统节拍（RT_TICK_PER_SECOND 默认为 1000Hz） */
    SysTick_Config(SystemCoreClock / RT_TICK_PER_SECOND);

    /* 4. 初始化 RT-Thread 内部动态内存堆 */
    rt_system_heap_init(system_heap, system_heap + sizeof(system_heap));

#if (defined(APP_DEBUG) && (APP_DEBUG != 0))
    /* 调试模式下初始化串口/RTT日志输出 */
    log_init();
#endif

    /*
     * 5. 使能 Cortex-M4 DWT (Data Watchpoint and Trace) 周期计数器。
     * 用于微秒级高精度总线时序建立（如软件 I2C、传感器启动建立时间），
     * 不占用硬件定时器资源，且不受中断打断时累积误差的影响。
     */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
}

/**
 * @brief  Cortex-M4 SysTick 中断服务函数
 * @note   为 RT-Thread 提供时基，通知调度器递增 tick 并唤醒超时的睡眠线程。
 */
void SysTick_Handler(void)
{
    /* 进入中断上下文通知内核 */
    rt_interrupt_enter();

    /* 递增操作系统时钟节拍 */
    rt_tick_increase();

    /* 离开中断上下文并触发潜在的上下文切换 */
    rt_interrupt_leave();
}

/**
 * @brief  毫秒级延时函数
 * @param  ms 延时时长（毫秒）
 * @note   调用 RT-Thread 线程级阻塞延时，当前线程挂起并让出 CPU，允许低优先级任务执行。
 *         严禁在中断服务函数（ISR）中调用本函数。
 */
void delay_ms(uint32_t ms)
{
    rt_thread_mdelay(ms);
}

/**
 * @brief  微秒级阻塞延时函数
 * @param  us 延时时长（微秒）
 * @note   采用 DWT 硬件时钟周期计数器忙等待。
 *         即使 DWT 发生 32 位溢出，无符号差值计算依然保证绝对时延正确。
 *         仅用于驱动底层总线时序（<= 几百微秒），禁止用于长时间延时。
 */
void delay_us(uint32_t us)
{
    uint32_t start_cycles = DWT->CYCCNT;
    uint32_t delay_cycles = us * (SystemCoreClock / 1000000U);

    while ((uint32_t)(DWT->CYCCNT - start_cycles) < delay_cycles)
    {
        /* 硬件计数器轮询等待 */
    }
}

/**
 * @brief  获取当前系统启动以来的运行毫秒数
 * @return 当前系统 tick 计数值
 */
uint32_t systick_get_ms(void)
{
    return (uint32_t)rt_tick_get();
}
