/**
 * @file    interrupt.c
 * @brief   系统外部中断与唤醒中断服务函数分发实现
 * 
 * 模块职责：
 * 1. 集中管理 N32WB452 的核心外部中断（EXTI）与 RTC 周期唤醒中断；
 * 2. 严格遵循“短 ISR 准则”：仅清除硬件中断挂起位、记录统计计数、置位轻量标志并唤醒对应的高优先级工作线程；
 * 3. 严禁在任何 ISR 中执行 printf、I2C 访问、耗时循环或阻塞操作。
 * 
 * 中断映射关系：
 * - EXTI0_IRQHandler:   PA0 引脚上升沿中断（物理按键），通知 `app_power` 启动防抖长按计时；
 * - EXTI15_10_IRQHandler: PB14 BLE 射频核事件中断，唤醒 `ble_thread` 处理蓝牙协议栈事件；
 * - EXTI9_5_IRQHandler:   PA9 SC7A20 运动中断（触发 `SC7A20_MotionIRQHandler`）及 PA5/6/7 蓝牙协议栈监控引脚；
 * - RTC_WKUP_IRQHandler:  RTC 内部 Wakeup 唤醒中断（Line 20），唤醒休眠中的系统并更新蓝牙状态监控。
 * 
 * 线程安全与低功耗：
 * - 所有中断函数均包裹 `rt_interrupt_enter()` 与 `rt_interrupt_leave()`，确保 RT-Thread 准确维护中断嵌套计数并在退出时触发上下文切换；
 * - 中断唤醒配合系统 Idle 线程的 STOP0 / Sleep 退出流程，驱动全系统的纯事件驱动低功耗架构。
 */

#include "n32wb452.h"
#include "app_ble.h"
#include "app_power.h"
#include "app_ppg.h"
#include "app_device.h"
#include "ble_monitor.h"
#include "n32wb452_exti.h"
#include "n32wb452_pwr.h"
#include "n32wb452_rtc.h"
#include "SC7A20.h"
#include <rtthread.h>

/* 外部协议栈与标志声明 */
extern void bt_handler(void);
extern __IO uint8_t flag_bt_irq;

/* 中断触发统计计数器（供上位机观察与稳定性压测） */
volatile uint32_t g_key_irq_count         = 0U;
volatile uint32_t g_ble_irq_count         = 0U;
volatile uint32_t g_ble_monitor_irq_count = 0U;
volatile uint32_t g_rtc_irq_count         = 0U;

void EXTI3_IRQHandler(void)
{
    rt_interrupt_enter();

    if (EXTI_GetITStatus(EXTI_LINE3) != RESET)
    {
        EXTI_ClrITPendBit(EXTI_LINE3);
        app_ppg_irq();
    }

    rt_interrupt_leave();
}

void DMA1_Channel7_IRQHandler(void)
{
    rt_interrupt_enter();
    app_ppg_dma_irq();
    rt_interrupt_leave();
}

/**
 * @brief  PA0 / EXTI Line 0 中断服务函数（按键输入）
 * @note   按键按下（上升沿）触发。清除 EXTI0 标志后通知电源管理模块启动长按关机计时器。
 */
void EXTI0_IRQHandler(void)
{
    rt_interrupt_enter();

    if (EXTI_GetITStatus(EXTI_LINE0) != RESET)
    {
        /* 1. 清除 EXTI0 中断挂起标志 */
        EXTI_ClrITPendBit(EXTI_LINE0);
        g_key_irq_count++;

        /* 2. 通知电源管理模块开始按键扫描 */
        app_power_key_irq();
    }

    rt_interrupt_leave();
}

/**
 * @brief  PB14 / EXTI Line 14 中断服务函数（BLE 射频核事件）
 * @note   N32WB452 无线射频从核向主核发送通知的物理通道。
 */
void EXTI15_10_IRQHandler(void)
{
    rt_interrupt_enter();

    if (EXTI_GetITStatus(EXTI_LINE14) != RESET)
    {
        /* 1. 清除 EXTI14 挂起标志 */
        EXTI_ClrITPendBit(EXTI_LINE14);
        g_ble_irq_count++;

        /* 2. 若调度器尚未启动则直接就地处理；调度器启动后置标志并唤醒 ble_thread */
        if (rt_thread_self() == RT_NULL)
        {
            bt_handler();
        }
        else
        {
            flag_bt_irq = 1U;
            ble_wakeup();
        }
    }

    rt_interrupt_leave();
}

/**
 * @brief  PA5~PA9 / EXTI Line 9_5 组合中断服务函数
 * @note   同时处理 PA9 (SC7A20 运动中断) 及 PA5/6/7 (蓝牙物理接口监控线)。
 */
void EXTI9_5_IRQHandler(void)
{
    uint32_t monitor_lines = EXTI_LINE5 | EXTI_LINE6 | EXTI_LINE7;

    rt_interrupt_enter();

    /* 1. 检查 PA9 (SC7A20 INT1 运动中断) */
    if (EXTI_GetITStatus(EXTI_LINE9) != RESET)
    {
        EXTI_ClrITPendBit(EXTI_LINE9);
        SC7A20_MotionIRQHandler();
    }

    /* 2. 检查 PA5/6/7 蓝牙监控引脚状态变化 */
    if ((EXTI->PEND & monitor_lines) != 0U)
    {
        g_ble_monitor_irq_count++;
        ble_monitor_callback();
        EXTI_ClrITPendBit(monitor_lines);
        ble_wakeup();
    }

    rt_interrupt_leave();
}

/**
 * @brief  RTC 周期性 Wakeup 唤醒中断服务函数 (EXTI Line 20)
 * @note   系统休眠期间由 RTC 硬件定时器产生周期性唤醒信号，用于更新系统运行时间与无线连接状态。
 */
void RTC_WKUP_IRQHandler(void)
{
    rt_interrupt_enter();

    if ((RTC_GetFlagStatus(RTC_FLAG_WTF) != RESET) ||
        (RTC_GetITStatus(RTC_INT_WUT) != RESET))
    {
        /* 解锁备份域并清除 RTC 唤醒标志 */
        PWR_BackupAccessEnable(ENABLE);
        RTC_EnableWriteProtection(DISABLE);
        RTC_ClrFlag(RTC_FLAG_WTF);
        RTC_ClrIntPendingBit(RTC_INT_WUT);
        RTC_EnableWriteProtection(ENABLE);

        /* 清除 EXTI Line 20 中断挂起标志 */
        EXTI_ClrITPendBit(EXTI_LINE20);

        g_rtc_irq_count++;
        wakeup_flag = 1U;
        app_device_rtc_wakeup();
        ble_wakeup();
    }

    rt_interrupt_leave();
}
