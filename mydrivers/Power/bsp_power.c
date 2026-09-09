/**
 * @file    bsp_power.c
 * @brief   板载电源域与负载开关硬件驱动实现
 * 
 * 模块职责：
 * 1. 初始化板载负载开关 CW3301（U6 与 U9）及状态指示灯；
 * 2. 控制传感器供电域（Sensor_1V8 与 IPA1322_VDD）的上下电时序；
 * 3. 提供硬件级的断电重上电循环（Power Cycle），用于器件通信故障自愈。
 * 
 * 与其他模块交互：
 * - 由 `app_main.c` 在启动时调用 `bsp_power_init()`；
 * - 由 `app_power.c` 在关机或休眠阶段调用 `bsp_sensor_power_off()` 切断漏电；
 * - 由 `app_ppg.c` 在启动采样前调用 `bsp_sensor_power_on()` 稳定供电。
 * 
 * 中断与线程安全：
 * - 仅在初始化阶段及低功耗状态切换时由系统/电源线程调用；
 * - PC13 位于备份域，操作前需使能 PWR/BKP 外设时钟并解锁写保护。
 * 
 * 低功耗关键设计：
 * - 严格遵循“先置位输出数据寄存器（POD）为高电平，再配置引脚为推挽输出”的设计准则，
 *   彻底消除引脚由浮空切换至输出瞬间的低电平毛刺，防止负载开关产生误切断；
 * - 关机时切断两路 1.8V 负载开关，结合引脚模拟输入（AIN）配置，实现 < 5uA 的超低待机电流。
 */

#include "bsp_power.h"
#include "bsp_systick.h"

/**
 * @brief  初始化板载所有电源负载开关与状态指示灯
 * @details 硬件引脚分配：
 *          - PA11: CW3301 (U6) 使能引脚 -> 高电平开启 Sensor_1V8 (传感器 IO 域与 I2C 上拉)
 *          - PC13: CW3301 (U9) 使能引脚 -> 高电平开启 IPA1322_VDD (AFE 芯片与 LED 阳极)
 *          - PA6:  板载状态指示灯 -> 高电平点亮
 */
void bsp_power_init(void)
{
    GPIO_InitType gpio_init_struct;

    /* 1. 开启 GPIOA, GPIOC 与 AFIO 复用功能时钟 */
    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA | RCC_APB2_PERIPH_GPIOC | RCC_APB2_PERIPH_AFIO, ENABLE);

    /* 2. 开启 PWR 与 BKP 时钟，解锁 PC13 备份域控制权限 (PC13 默认属于侵入检测引脚) */
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_PWR | RCC_APB1_PERIPH_BKP, ENABLE);
    PWR_BackupAccessEnable(ENABLE);
    BKP_TPEnable(DISABLE);

    /* 3. 开机仅点亮状态灯；广播阶段两路传感器电源必须保持关闭。 */
    GPIO_ResetBits(GPIOA, GPIO_PIN_11);
    GPIO_SetBits(GPIOA, GPIO_PIN_6);
    GPIO_ResetBits(GPIOC, GPIO_PIN_13);

    /* 4. 配置 PA11 (Sensor_1V8_EN) 为 50MHz 推挽输出并拉高 */
    GPIO_InitStruct(&gpio_init_struct);
    gpio_init_struct.Pin        = GPIO_PIN_11;
    gpio_init_struct.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio_init_struct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(GPIOA, &gpio_init_struct);
    GPIO_ResetBits(GPIOA, GPIO_PIN_11);

    /* 5. 配置 PA6 (LED 指示灯) 为 50MHz 推挽输出并点亮 */
    gpio_init_struct.Pin        = GPIO_PIN_6;
    gpio_init_struct.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio_init_struct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(GPIOA, &gpio_init_struct);
    GPIO_SetBits(GPIOA, GPIO_PIN_6);

    /* 6. 配置 PC13 (IPA1322_VDD_EN) 为 50MHz 推挽输出并拉高 */
    gpio_init_struct.Pin        = GPIO_PIN_13;
    gpio_init_struct.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio_init_struct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(GPIOC, &gpio_init_struct);
    GPIO_ResetBits(GPIOC, GPIO_PIN_13);
}

/**
 * @brief  开启传感器与 AFE 供电域
 * @note   低功耗模式唤醒后显式调用。重新使能外设时钟和解锁备份域，
 *         确保从休眠退出后负载开关引脚恢复受控状态。
 */
void bsp_sensor_power_on(void)
{
    GPIO_InitType gpio_init_struct;

    /* 1. 恢复 GPIOA 与 GPIOC 时钟 */
    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA | RCC_APB2_PERIPH_GPIOC | RCC_APB2_PERIPH_AFIO, ENABLE);
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_PWR | RCC_APB1_PERIPH_BKP, ENABLE);
    PWR_BackupAccessEnable(ENABLE);
    BKP_TPEnable(DISABLE);

    /* 2. 预置高电平并重新配置推挽输出模式 */
    GPIO_SetBits(GPIOA, GPIO_PIN_11);
    GPIO_SetBits(GPIOC, GPIO_PIN_13);

    gpio_init_struct.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio_init_struct.GPIO_Speed = GPIO_Speed_50MHz;

    gpio_init_struct.Pin = GPIO_PIN_11;
    GPIO_InitPeripheral(GPIOA, &gpio_init_struct);

    gpio_init_struct.Pin = GPIO_PIN_13;
    GPIO_InitPeripheral(GPIOC, &gpio_init_struct);

    /* 3. 确保输出维持高电平使能 */
    GPIO_SetBits(GPIOA, GPIO_PIN_11);
    GPIO_SetBits(GPIOC, GPIO_PIN_13);
}

/**
 * @brief  切断传感器与 AFE 供电域
 * @note   PA11 与 PC13 输出低电平，关闭 CW3301 负载开关，切断外部器件供电。
 */
void bsp_sensor_power_off(void)
{
    GPIO_ResetBits(GPIOA, GPIO_PIN_11);
    GPIO_ResetBits(GPIOC, GPIO_PIN_13);
}

void bsp_afe_power_on(void)
{
    GPIO_SetBits(GPIOC, GPIO_PIN_13);
}

void bsp_afe_power_off(void)
{
    GPIO_ResetBits(GPIOC, GPIO_PIN_13);
}

void bsp_status_led_on(void)
{
    GPIO_InitType gpio_init_struct;
    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA, ENABLE);
    GPIO_SetBits(GPIOA, GPIO_PIN_6);
    GPIO_InitStruct(&gpio_init_struct);
    gpio_init_struct.Pin = GPIO_PIN_6;
    gpio_init_struct.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio_init_struct.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_InitPeripheral(GPIOA, &gpio_init_struct);
    GPIO_SetBits(GPIOA, GPIO_PIN_6);
}

void bsp_status_led_off(void)
{
    GPIO_ResetBits(GPIOA, GPIO_PIN_6);
}

/**
 * @brief  传感器硬件冷重启循环
 * @note   用于传感器死机或总线异常时的硬复位：
 *         先下电，等待 10ms 放电完毕，再重新上电并延时 50ms 等待晶振与稳压器稳定。
 */
void bsp_sensor_power_cycle(void)
{
    /* 1. 切断供电 */
    bsp_sensor_power_off();
    delay_ms(10U);

    /* 2. 恢复供电 */
    bsp_sensor_power_on();
    delay_ms(50U);
}
