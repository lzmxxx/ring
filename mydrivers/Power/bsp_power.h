/**
 * @file    bsp_power.h
 * @brief   板载电源域与负载开关控制头文件
 * @details 负责戒指硬件电源供电管理，控制板载 CW3301 负载开关芯片：
 *          - U6 (CW3301): PA11 控制 Sensor_1V8 传感器 IO 域与 I2C 上拉供电；
 *          - U9 (CW3301): PC13 控制 IPA1322 AFE 芯片 VDD 及 LED 发射管阳极供电；
 *          - 板载指示灯: PA6 控制状态 LED。
 */

#ifndef __BSP_POWER_H__
#define __BSP_POWER_H__

#include "n32wb452.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 传感器 1.8V 域控制引脚 (CW3301 U6) */
#define SENSOR_1V8_PORT     GPIOA
#define SENSOR_1V8_PIN      GPIO_PIN_11
#define SENSOR_1V8_CLK      RCC_APB2_PERIPH_GPIOA

/* IPA1322 AFE 芯片主供电域控制引脚 (CW3301 U9，位于 PC13 备份域) */
#define IPA1322_VDD_PORT    GPIOC
#define IPA1322_VDD_PIN     GPIO_PIN_13
#define IPA1322_VDD_CLK     RCC_APB2_PERIPH_GPIOC

/* 板载状态指示灯控制引脚 (高电平点亮) */
#define BOARD_LED_PORT      GPIOA
#define BOARD_LED_PIN       GPIO_PIN_6
#define BOARD_LED_CLK       RCC_APB2_PERIPH_GPIOA

/**
 * @brief  初始化板载所有电源负载开关与状态指示灯
 * @note   先设置输出寄存器（POD）为高电平再配置为推挽输出模式，
 *         彻底规避引脚浮空切换瞬间产生的低电平毛刺，防止负载开关被误关闭。
 */
void bsp_power_init(void);

/**
 * @brief  开启所有传感器与 AFE 供电域 (PA11 与 PC13 输出高电平)
 * @note   低功耗唤醒或开启采样时调用，并重新使能 GPIO 端口以防休眠期被重置。
 */
void bsp_sensor_power_on(void);
void bsp_afe_power_on(void);
void bsp_afe_power_off(void);
void bsp_status_led_on(void);
void bsp_status_led_off(void);

/**
 * @brief  切断所有传感器与 AFE 供电域 (PA11 与 PC13 输出低电平)
 * @note   关机或极低功耗待机前调用，彻底切断外部传感器静态漏电通道。
 */
void bsp_sensor_power_off(void);

/**
 * @brief  对传感器供电域执行硬件冷重启循环（切断 -> 延时10ms -> 上电 -> 延时50ms稳定）
 * @note   用于 AFE 或传感器总线锁死、通信完全失步时的硬件级自愈复位。
 */
void bsp_sensor_power_cycle(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_POWER_H__ */
