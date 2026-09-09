/**
 * @file    app_power.c
 * @brief   系统电源与多级低功耗管理实现
 * 
 * 模块职责：
 * 1. 负责戒指全生命周期电源模式控制（开机校验 -> 运行 -> 连接态休眠 -> 未连接停机 -> 关机待机）；
 * 2. 物理按键 (PA0) 防误触检测：开机须连续长按 1.5 秒确认，运行期连续长按 2.0 秒确认关机；
 * 3. 软件低功耗引用计数锁（`PM_FIFO`, `PM_DMA`, `PM_ALGO`, `PM_BLE`, `PM_SYSTEM`），
 *    确保关键数据搬运、硬件移位及复杂计算期间系统不被误挂起；
 * 4. 连接态睡眠（Connected Sleep）：保持当前主时钟和 BLE 射频，暂停 SysTick 并通过 RTC 256Hz 亚秒级
 *    时钟实现 RT-Thread 操作系统 tick 的精确补偿，消除高频心跳中断唤醒对功耗的浪费；
 * 5. 未连接停机（STOP0）：系统在空闲时切入 HSI，关闭外部晶振与总线时钟进入 STOP0，
 *    由 EXTI 中断或 RTC 唤醒；唤醒后自动恢复 64MHz HSE PLL 并重置各外设总线；
 * 6. 深度待机关机（Standby）：切断外部 CW3301 负载开关，将所有暴露引脚重置为模拟输入（AIN）
 *    以切断倒灌漏电回路，使芯片整体静态待机电流降至 < 5uA。
 * 
 * 与其他模块交互：
 * - 由 `app_main.c` 调用 `app_power_boot_check()` 与 `app_power_init()`；
 * - 挂载到 RT-Thread 的 `idle_hook` 空闲钩子中执行低功耗策略；
 * - 协调 `app_ppg.c` 与 `app_ble.c` 的空闲与启停。
 * 
 * 中断与实时性考量：
 * - 休眠唤醒后在临界区内完成时钟恢复（HSE PLL 64MHz）及外设重初始化，再开启调度，杜绝未稳时调度崩溃。
 */

#include "app_power.h"
#include "app_config.h"
#include "app_ppg.h"
#include "app_record.h"
#include "app_ble.h"
#include "bsp_power.h"
#include "bsp_hw_i2c.h"
#include "MyI2C2.h"
#include "MySPI2.h"
#include "SC7A20.h"
#include "clock.h"
#include "Interface.h"
#include "Eif_iom.h"
#include "ble_monitor.h"
#include "n32wb452.h"
#include "n32wb452_dbg.h"
#include "n32wb452_exti.h"
#include "n32wb452_gpio.h"
#include "n32wb452_pwr.h"
#include "n32wb452_rcc.h"
#include "n32wb452_rtc.h"
#include "misc.h"
#include <rthw.h>
#include <string.h>

/* 外部协议栈中断与标志 */
extern __IO uint8_t flag_bt_irq;

/* 全局关机与统计变量 */
volatile rt_bool_t app_shutdown             = RT_FALSE;
volatile uint32_t  g_pm_stop_count          = 0U;
volatile uint32_t  g_pm_stop_rtc_units      = 0U;
volatile uint32_t  g_pm_stop_last_rtc_units = 0U;
volatile uint32_t  g_pm_sleep_count         = 0U;
volatile uint32_t  g_pm_stop_monitor_abort  = 0U;
volatile uint32_t  g_pm_ble_sleep_count     = 0U;
volatile uint32_t  g_pm_ble_sleep_rtc_units = 0U;
volatile uint8_t   g_pm_runtime_sleep_disabled = APP_DISABLE_RUNTIME_SLEEP;
volatile uint32_t  g_pm_connected_sleep_attempts;
volatile uint8_t   g_pm_connected_sleep_block_mask;
volatile uint32_t  g_key_press_ms           = 0U;
volatile uint32_t  g_key_cancel_count       = 0U;
volatile uint32_t  g_key_shutdown_count     = 0U;
volatile uint32_t  g_power_off_stage        = 0U;

/* 软件低功耗引用计数数组 */
static volatile uint8_t power_locks[PM_LOCK_COUNT];

/* 关机执行任务与信号量 */
static struct rt_semaphore power_sem;
static struct rt_thread    power_thread;
ALIGN(RT_ALIGN_SIZE) static uint8_t power_stack[APP_POWER_TASK_STACK_SIZE];

/* 按键轮询定时器与时间变量 */
static struct rt_timer    key_timer;
static volatile rt_bool_t key_timing    = RT_FALSE;
static uint16_t           tick_fraction = 0U;

/* 按键消抖与长按扫描周期：20ms */
#define KEY_SCAN_PERIOD_MS  20U

/* g_pm_connected_sleep_block_mask 位定义，供 MKLink 读取定位连接态 Sleep 阻塞点。 */
#define PM_SLEEP_BLOCK_RUNTIME   0x01U
#define PM_SLEEP_BLOCK_SHUTDOWN  0x02U
#define PM_SLEEP_BLOCK_LOCK      0x04U
#define PM_SLEEP_BLOCK_PPG       0x08U
#define PM_SLEEP_BLOCK_BLE       0x10U
#define PM_SLEEP_BLOCK_KEY       0x20U
#define PM_SLEEP_BLOCK_MONITOR   0x40U

/* RTC 亚秒级时基：N32WB452 RTC 亚秒寄存器频率为 256Hz (周期约 3.9ms) */
#define RTC_SUBSECOND_HZ    256U
#define RTC_DAY_UNITS       (24UL * 60UL * 60UL * RTC_SUBSECOND_HZ)

/**
 * @brief  检查所有低功耗锁是否均已完全释放
 * @return RT_TRUE 所有锁计数均为 0，RT_FALSE 仍有锁处于被占用状态
 */
static rt_bool_t locks_clear(void)
{
    for (unsigned idx = 0U; idx < PM_LOCK_COUNT; idx++)
    {
        if (power_locks[idx] != 0U)
        {
            return RT_FALSE;
        }
    }
    return RT_TRUE;
}

/**
 * @brief  获取指定的低功耗引用锁（计数加 1）
 */
void app_power_lock(unsigned lock)
{
    rt_base_t interrupt_level;

    if (lock >= PM_LOCK_COUNT)
    {
        return;
    }

    interrupt_level = rt_hw_interrupt_disable();
    if (power_locks[lock] != 0xFFU)
    {
        power_locks[lock]++;
    }
    rt_hw_interrupt_enable(interrupt_level);
}

/**
 * @brief  释放指定的低功耗引用锁（计数减 1）
 */
void app_power_unlock(unsigned lock)
{
    rt_base_t interrupt_level;

    if (lock >= PM_LOCK_COUNT)
    {
        return;
    }

    interrupt_level = rt_hw_interrupt_disable();
    if (power_locks[lock] != 0U)
    {
        power_locks[lock]--;
    }
    rt_hw_interrupt_enable(interrupt_level);
}

/**
 * @brief  初始化物理按键 GPIO (PA0) 与 EXTI0 中断
 */
static void key_init(void)
{
    GPIO_InitType gpio_init_struct;
    EXTI_InitType exti_init_struct;
    NVIC_InitType nvic_init_struct;

    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA | RCC_APB2_PERIPH_AFIO, ENABLE);

    /* 配置 PA0 为下拉输入模式 */
    GPIO_InitStruct(&gpio_init_struct);
    gpio_init_struct.Pin        = GPIO_PIN_0;
    gpio_init_struct.GPIO_Mode  = GPIO_Mode_IPD;
    gpio_init_struct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(GPIOA, &gpio_init_struct);

    /* 配置 EXTI Line 0 */
    GPIO_ConfigEXTILine(GPIOA_PORT_SOURCE, GPIO_PIN_SOURCE0);
    EXTI_ClrITPendBit(EXTI_LINE0);

    /* 配置为上升沿触发中断 */
    EXTI_InitStruct(&exti_init_struct);
    exti_init_struct.EXTI_Line    = EXTI_LINE0;
    exti_init_struct.EXTI_Mode    = EXTI_Mode_Interrupt;
    exti_init_struct.EXTI_Trigger = EXTI_Trigger_Rising;
    exti_init_struct.EXTI_LineCmd = ENABLE;
    EXTI_InitPeripheral(&exti_init_struct);

    /* 配置 NVIC 抢占优先级 2，子优先级 0 */
    nvic_init_struct.NVIC_IRQChannel                   = EXTI0_IRQn;
    nvic_init_struct.NVIC_IRQChannelPreemptionPriority = 2U;
    nvic_init_struct.NVIC_IRQChannelSubPriority        = 0U;
    nvic_init_struct.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic_init_struct);
}

/**
 * @brief  开机长按有效性校验
 * @details 逻辑流程：
 *          1. 若非 Standby 唤醒（如调试器复位或上电复位），直接放行开机；
 *          2. 若从 Standby 唤醒，持续扫描 1500ms，要求 PA0 必须始终维持高电平；
 *          3. 若中途松开则判定为误触，返回 RT_FALSE（由 main 重新送入待机）；
 *          4. 成功确认开机后，等待按键释放，防止该次长按被系统误认作关机操作。
 */
rt_bool_t app_power_boot_check(void)
{
    GPIO_InitType gpio_init_struct;
    uint32_t      elapsed_ms;

    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA, ENABLE);
    GPIO_InitStruct(&gpio_init_struct);
    gpio_init_struct.Pin        = GPIO_PIN_0;
    gpio_init_struct.GPIO_Mode  = GPIO_Mode_IPD;
    gpio_init_struct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(GPIOA, &gpio_init_struct);
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_PWR, ENABLE);

    /* 检查是否来自待机复位标志位 (PWR_SB_FLAG) */
    if (PWR_GetFlagStatus(PWR_SB_FLAG) == RESET)
    {
        return RT_TRUE;
    }

    /* 连续长按判定 (POWER_KEY_ON_HOLD_MS = 1500ms) */
    for (elapsed_ms = 0U; elapsed_ms < POWER_KEY_ON_HOLD_MS; elapsed_ms += KEY_SCAN_PERIOD_MS)
    {
        if (GPIO_ReadInputDataBit(GPIOA, GPIO_PIN_0) == Bit_RESET)
        {
            return RT_FALSE; /* 1.5 秒内提前松开，判定为碰撞误触 */
        }
        rt_thread_mdelay(KEY_SCAN_PERIOD_MS);
    }

    /* 清除待机与唤醒标志 */
    PWR_ClearFlag(PWR_SB_FLAG | PWR_WU_FLAG);

    /* 必须等待物理按键释放，防止关机计时器被立即触发 */
    while (GPIO_ReadInputDataBit(GPIOA, GPIO_PIN_0) == Bit_SET)
    {
        rt_thread_mdelay(20U);
    }

    return RT_TRUE;
}

/**
 * @brief  配置芯片切入 Standby 待机模式
 */
void app_power_enter_standby(void)
{
    g_power_off_stage = 5U;

    /* 1. 再次确认按键已完全松开 */
    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA, ENABLE);
    while (GPIO_ReadInputDataBit(GPIOA, GPIO_PIN_0) == Bit_SET)
    {
        rt_thread_mdelay(10U);
    }

    /* 2. 屏蔽所有中断，防止 WFI 立即异常退出 */
    g_power_off_stage = 6U;
    SysTick->CTRL     = 0U;
    SCB->ICSR         = SCB_ICSR_PENDSTCLR_Msk;
    EXTI_ClrITPendBit(EXTI_LINE0);
    __disable_irq();

    for (uint32_t idx = 0U; idx < 3U; idx++)
    {
        NVIC->ICPR[idx] = 0xFFFFFFFFU;
    }

    /* 3. 使能 PA0 WKUP 唤醒引脚与待机配置 */
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_PWR | RCC_APB1_PERIPH_BKP, ENABLE);
    PWR_BackupAccessEnable(ENABLE);
    PWR_WakeUpPinEnable(ENABLE);
    PWR_ClearFlag(PWR_WU_FLAG);
    PWR_ClearFlag(PWR_SB_FLAG);
    DBG_ConfigPeriph(DBG_STDBY, DISABLE);
    __DSB();

    /* 4. 真正切入待机模式并永久挂起（只能由 PA0 WKUP 上升沿硬件复位唤醒） */
    while (1)
    {
        PWR_EnterStandbyState();
    }
}

/**
 * @brief  将所有外部相连引脚配置为模拟输入 (AIN)
 * @note   消除芯片断电或待机期间外部浮空引脚的内部施密特触发器静态漏电。
 */
static void pins_analog(void)
{
    GPIO_InitType gpio_init_struct;

    GPIO_InitStruct(&gpio_init_struct);
    gpio_init_struct.GPIO_Mode  = GPIO_Mode_AIN;
    gpio_init_struct.GPIO_Speed = GPIO_Speed_2MHz;

    /* PB 引脚配置 */
    gpio_init_struct.Pin = GPIO_PIN_3  | GPIO_PIN_6  | GPIO_PIN_7  | GPIO_PIN_10 |
                           GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 |
                           GPIO_PIN_15;
    GPIO_InitPeripheral(GPIOB, &gpio_init_struct);

    /* PA 引脚配置 */
    gpio_init_struct.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_11;
    GPIO_InitPeripheral(GPIOA, &gpio_init_struct);

    /* PC 引脚配置 */
    gpio_init_struct.Pin = GPIO_PIN_13;
    GPIO_InitPeripheral(GPIOC, &gpio_init_struct);
}

/**
 * @brief  执行系统完整优雅关机流程
 */
void app_power_off(void)
{
    g_power_off_stage = 1U;
    app_shutdown      = RT_TRUE;
    app_power_lock(PM_SYSTEM);

    /* 1. 停止 PPG 采集与算法 */
    app_ppg_stop();
    app_record_shutdown_flush();

    /* 2. 断开蓝牙连接与协议栈 */
    g_power_off_stage = 2U;
    app_ble_stop();

    /* 3. 切断传感器 CW3301 负载开关供电 */
    g_power_off_stage = 3U;
    bsp_sensor_power_off();

    /* 4. 引脚转模拟输入防漏电 */
    pins_analog();

    /* 5. 切入 Standby */
    g_power_off_stage = 4U;
    app_power_enter_standby();
}

/**
 * @brief  按键长按关机 20ms 软定时器周期回调
 */
static void key_timeout_callback(void *parameter)
{
    (void)parameter;

    if (app_shutdown)
    {
        rt_timer_stop(&key_timer);
        key_timing = RT_FALSE;
        return;
    }

    /* 检查按键是否提前释放 */
    if (GPIO_ReadInputDataBit(GPIOA, GPIO_PIN_0) == Bit_RESET)
    {
        g_key_press_ms = 0U;
        g_key_cancel_count++;
        key_timing = RT_FALSE;
        rt_timer_stop(&key_timer);
        return;
    }

    g_key_press_ms += KEY_SCAN_PERIOD_MS;

    /* 未达到 2000ms 关机门限，继续计时 */
    if (g_key_press_ms < POWER_KEY_OFF_HOLD_MS)
    {
        return;
    }

    /* 达到 2000ms，触发关机信号量 */
    g_key_shutdown_count++;
    app_shutdown = RT_TRUE;
    key_timing   = RT_FALSE;
    rt_timer_stop(&key_timer);
    rt_sem_release(&power_sem);
}

/**
 * @brief  PA0 按键中断通知回调
 */
void app_power_key_irq(void)
{
    if (!app_shutdown && !key_timing)
    {
        g_key_press_ms = 0U;
        key_timing     = RT_TRUE;
        rt_timer_start(&key_timer);
    }
}

/**
 * @brief  关机执行任务主体
 */
static void power_thread_entry(void *parameter)
{
    (void)parameter;

    /* 阻塞等待关机信号量 */
    rt_sem_take(&power_sem, RT_WAITING_FOREVER);
    app_power_off();
}

/**
 * @brief  读取当前 RTC 亚秒级时间戳 (单位：1/256 秒)
 * @note   两次回读校验防止秒/分/时跨进位期间发生读取撕裂。
 */
static uint32_t rtc_units_now(void)
{
    RTC_TimeType first_reading;
    RTC_TimeType second_reading;
    uint32_t     subsecond_val;

    do
    {
        RTC_GetTime(RTC_FORMAT_BIN, &first_reading);
        subsecond_val = RTC_GetSubSecond() & 0xFFU;
        RTC_GetTime(RTC_FORMAT_BIN, &second_reading);
    }
    while ((first_reading.Hours   != second_reading.Hours)   ||
           (first_reading.Minutes != second_reading.Minutes) ||
           (first_reading.Seconds != second_reading.Seconds));

    return (((uint32_t)second_reading.Hours * 3600U +
             (uint32_t)second_reading.Minutes * 60U +
             (uint32_t)second_reading.Seconds) * RTC_SUBSECOND_HZ) +
           (0xFFU - subsecond_val);
}

/**
 * @brief  计算 RTC 经过的单位时长
 */
static uint32_t rtc_elapsed_units(uint32_t before, uint32_t after)
{
    return (after >= before) ? (after - before) : (RTC_DAY_UNITS - before + after);
}

/**
 * @brief  对 RT-Thread 操作系统 tick 执行时间补偿
 * @param  rtc_units 休眠期间经过的 RTC 单位数 (256Hz)
 * @note   在暂停 SysTick 的深度睡眠退出后，将休眠流逝的真实物理时间补偿给 RTOS 节拍，
 *         防止软件定时器与延时产生严重累积漂移。
 */
static void compensate_rt_tick(uint32_t rtc_units)
{
    uint64_t  scaled_ticks = (uint64_t)rtc_units * RT_TICK_PER_SECOND + tick_fraction;
    rt_tick_t elapsed_ticks = (rt_tick_t)(scaled_ticks / RTC_SUBSECOND_HZ);

    tick_fraction = (uint16_t)(scaled_ticks % RTC_SUBSECOND_HZ);

    if (elapsed_ticks != 0U)
    {
        rt_tick_set(rt_tick_get() + elapsed_ticks);
        rt_timer_check();
    }
}

/**
 * @brief  单次极浅睡眠 (WFI)
 * @note   条件不足以关闭 SysTick 时使用，避免空闲线程纯死循环空转。
 */
static void shallow_sleep_once(void)
{
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
    __DSB();
    __WFI();
    __ISB();
    g_pm_sleep_count++;
}

static uint8_t connected_sleep_blockers(void)
{
    uint8_t mask = 0U;

    if (app_shutdown) mask |= PM_SLEEP_BLOCK_SHUTDOWN;
    if (!locks_clear()) mask |= PM_SLEEP_BLOCK_LOCK;
    if (!app_ppg_idle()) mask |= PM_SLEEP_BLOCK_PPG;
    if (!app_ble_idle()) mask |= PM_SLEEP_BLOCK_BLE;
    if (GPIO_ReadInputDataBit(GPIOA, GPIO_PIN_0) == Bit_SET) mask |= PM_SLEEP_BLOCK_KEY;
    return mask;
}

/**
 * @brief  蓝牙连接态低功耗睡眠流程 (Connected Sleep)
 * @details 保持当前主频和射频通信，先与无线核完成休眠握手，
 *          随后关闭 SysTick 并 WFI 进入睡眠；唤醒后补偿 RTOS tick。
 */
static void connected_ble_sleep(void)
{
    uint32_t rtc_start_time;
    uint32_t rtc_end_time;
    uint32_t elapsed_time;

    g_pm_connected_sleep_block_mask = connected_sleep_blockers();
    if (g_pm_connected_sleep_block_mask != 0U)
    {
        shallow_sleep_once();
        return;
    }

    rt_enter_critical();

    g_pm_connected_sleep_block_mask = connected_sleep_blockers();
    if (!app_ble_connected() || (g_pm_connected_sleep_block_mask != 0U))
    {
        rt_exit_critical();
        shallow_sleep_once();
        return;
    }

    /* 与无线射频核完成低功耗握手协商 */
    if (ble_monitor_wait(SystemCoreClock / 100U))
    {
        g_pm_connected_sleep_block_mask = PM_SLEEP_BLOCK_MONITOR;
        g_pm_stop_monitor_abort++;
        rt_exit_critical();
        shallow_sleep_once();
        return;
    }

    /* 记录进入睡眠前的 RTC 时间并关闭 SysTick */
    g_pm_connected_sleep_block_mask = 0U;
    g_pm_connected_sleep_attempts++;
    rtc_start_time = rtc_units_now();
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
    SCB->ICSR      = SCB_ICSR_PENDSTCLR_Msk;
    SCB->SCR      &= ~SCB_SCR_SLEEPDEEP_Msk;
    __DSB();
    __WFI();

    /* 唤醒后重新开启 SysTick 并补偿经过的时间 */
    rtc_end_time = rtc_units_now();
    elapsed_time = rtc_elapsed_units(rtc_start_time, rtc_end_time);
    compensate_rt_tick(elapsed_time);

    g_pm_ble_sleep_count++;
    g_pm_ble_sleep_rtc_units += elapsed_time;

    SysTick->LOAD  = SystemCoreClock / RT_TICK_PER_SECOND - 1U;
    SysTick->VAL   = 0U;
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;

    rt_exit_critical();
}

static rt_bool_t stop0_allowed(void)
{
    return (!app_shutdown && locks_clear() && app_ppg_idle() &&
            app_ble_stop_allowed() &&
            (GPIO_ReadInputDataBit(GPIOA, GPIO_PIN_0) == Bit_RESET));
}

static void enter_stop0_once(void)
{
    uint32_t rtc_start_time;
    uint32_t rtc_end_time;
    uint32_t elapsed_time;
    rt_bool_t entered_stop_mode = RT_FALSE;

    rt_enter_critical();
    if (!stop0_allowed())
    {
        rt_exit_critical();
        shallow_sleep_once();
        return;
    }

    rtc_start_time = rtc_units_now();
    SetSysClock_HSI();
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    eif_gpio_DeInit();

    DBG_ConfigPeriph(DBG_STOP, DISABLE);
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_PWR, ENABLE);

    if (!ble_monitor_wait(SystemCoreClock / 100U))
    {
        if (!flag_bt_irq && !wakeup_flag && stop0_allowed())
        {
            __DSB();
            entered_stop_mode = RT_TRUE;
            PWR_EnterStopState(PWR_REGULATOR_ON, PWR_STOPENTRY_WFI);
        }
    }
    else
    {
        g_pm_stop_monitor_abort++;
    }

    SetSysClock_HSE_PLL();
    SystemCoreClockUpdate();
    (void)ble_hardware_reinit();
    bsp_hw_i2c_init();
    MyI2C2_Init();
    MySPI2_Init();
    SC7A20_InterruptReInit();

    rtc_end_time = rtc_units_now();
    elapsed_time = rtc_elapsed_units(rtc_start_time, rtc_end_time);
    compensate_rt_tick(elapsed_time);

    if (entered_stop_mode)
    {
        g_pm_stop_count++;
        g_pm_stop_last_rtc_units = elapsed_time;
        g_pm_stop_rtc_units += elapsed_time;
    }

    SysTick->LOAD = SystemCoreClock / RT_TICK_PER_SECOND - 1U;
    SysTick->VAL = 0U;
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;
    rt_exit_critical();
}

/**
 * @brief  RT-Thread 空闲线程钩子函数 (idle_hook)
 * @details 系统全自动低功耗核心策略：
 *          1. 若处于蓝牙连接态，调用 `connected_ble_sleep()`；
 *          2. 若处于未连接广播或待机状态，则在满足安全条件时进入深度停机模式 (STOP0)；
 *          3. 停机流程：切 HSI -> 关 SysTick -> 停机 -> 唤醒 -> 恢复 64MHz HSE PLL -> 恢复 I2C/SPI -> 补偿 tick。
 */
static void idle_hook(void)
{
    if (g_pm_runtime_sleep_disabled)
    {
        g_pm_connected_sleep_block_mask = PM_SLEEP_BLOCK_RUNTIME;
        return;
    }

    if (app_ble_connected())
    {
        connected_ble_sleep();
        return;
    }

    if (!stop0_allowed())
    {
        shallow_sleep_once();
        return;
    }

    enter_stop0_once();
}

/**
 * @brief  初始化系统电源管理模块
 */
void app_power_init(void)
{
    memset((void *)power_locks, 0, sizeof(power_locks));
    power_locks[PM_SYSTEM] = 1U;

    /* 初始化关机信号量与按键检测定时器 */
    rt_sem_init(&power_sem, "power", 0, RT_IPC_FLAG_FIFO);
    key_init();

    rt_timer_init(&key_timer, "key", key_timeout_callback, RT_NULL,
                  rt_tick_from_millisecond(KEY_SCAN_PERIOD_MS),
                  RT_TIMER_FLAG_PERIODIC | RT_TIMER_FLAG_SOFT_TIMER);

    /* 创建关机服务任务：优先级 3（高优先级） */
    rt_thread_init(&power_thread, "power", power_thread_entry, RT_NULL,
                   power_stack, sizeof(power_stack),
                   APP_POWER_TASK_PRIORITY, APP_POWER_TASK_TIMESLICE);
    rt_thread_startup(&power_thread);

    /* 注册空闲线程低功耗钩子 */
    rt_thread_idle_sethook(idle_hook);
}
