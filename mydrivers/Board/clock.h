/**
 * @file    clock.h
 * @brief   系统时钟配置头文件
 * @details 负责 N32WB452 系统主时钟在 APP_SYSCLK_HZ 指定的高性能运行态（HSE PLL）与低功耗/休眠态（HSI）之间的配置与平滑切换。
 */

#ifndef __CLOCK_H__
#define __CLOCK_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  配置系统时钟为 APP_SYSCLK_HZ 指定频率（HSE 32MHz 作为 PLL 源）
 * @note   正常运行态使用。外设总线分频：
 *         - SYSCLK/HCLK = APP_SYSCLK_HZ
 *         - 48MHz 配置下 PCLK2 = 48MHz，PCLK1 = 24MHz
 *         - 48MHz 配置下 Flash 等待周期 = 1 周期
 */
void SetSysClock_HSE_PLL(void);

/** @brief PLL 锁定或系统时钟切换失败标志；0 表示成功，非0表示已安全回退至HSI。 */
extern volatile uint8_t Clock_PllFailure;

/**
 * @brief  配置系统时钟为 HSI（内部高速 8MHz RC 振荡器）并关闭 PLL/HSE
 * @note   低功耗 STOP0 准备阶段使用。
 *         切入 HSI 后关闭 HSE 与 PLL，可大幅降低时钟切换过程中的漏电与动态功耗。
 */
void SetSysClock_HSI(void);

#ifdef __cplusplus
}
#endif

#endif /* __CLOCK_H__ */
