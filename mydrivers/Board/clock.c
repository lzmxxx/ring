/**
 * @file    clock.c
 * @brief   系统时钟管理与时钟源切换实现
 *
 * 模块职责：
 * 1. 提供由 APP_SYSCLK_HZ 选择的 48/64/96/128MHz 高性能运行态时钟；
 * 2. 提供进入低功耗停机模式前的 HSI 8MHz 降频与时钟平滑过渡；
 * 3. 严格遵循 N32WB452 官方时钟树与 Flash 访问等待周期规格。
 *
 * 与其他模块交互：
 * - 由 `board.c` 在系统启动阶段调用以建立主频；
 * - 由 `app_power.c` 在进入 STOP0 停机模式前切换至 HSI，并在唤醒后重新恢复 APP_SYSCLK_HZ 指定的 HSE PLL。
 *
 * 中断与线程安全：
 * - 时钟配置直接修改 RCC 核心控制寄存器；
 * - 在低功耗唤醒阶段由 `app_power` 在临界区保护（关中断/调度锁）下执行，避免时钟不稳定时发生任务调度。
 *
 * 低功耗设计考量：
 * - 64MHz 是当前 BLE 固件已验证的稳定运行态；更低/更高主频仅作对照测试。
 * - 休眠前切入内部 HSI，可安全关闭耗电较大的外部晶振与 PLL 电路。
 */

#include "clock.h"
#include "app_config.h"
#include "n32wb452.h"
#include "n32wb452_rcc.h"
#include "n32wb452_flash.h"

/* 供 SWD 读取的时钟启动诊断。失败时不在时钟等待循环中永久卡死。 */
volatile uint8_t Clock_PllFailure;

/**
 * @brief  按 APP_SYSCLK_HZ 配置系统时钟
 * @details 执行步骤：
 *          1. 复位 RCC 时钟配置；
 *          2. 开启并等待 32MHz HSE 晶振稳定；
 *          3. 使能 Flash 预取缓冲区并按配置主频设置等待周期；
 *          4. 配置 AHB/APB 总线分频器；
 *          5. 配置 PLL（由 APP_SYSCLK_HZ 选择输入分频与倍频）并使能；
 *          6. 等待 PLL 就绪后切换系统主时钟为 PLL 时钟；
 *          7. 更新 SystemCoreClock 全局变量。
 */
void SetSysClock_HSE_PLL(void)
{
    __IO uint32_t hse_startup_status = 0U;
    uint32_t pll_wait_count = 0U;

    Clock_PllFailure = 0U;

    /* 1. 将 RCC 时钟配置重置为默认复位状态 */
    RCC_DeInit();

    /* 2. 开启外部高速晶振 (32MHz HSE) */
    RCC_ConfigHse(RCC_HSE_ENABLE);
    hse_startup_status = RCC_WaitHseStable();

    if (hse_startup_status == SUCCESS)
    {
        /* 3. 使能 Flash 预取缓冲区，并按运行主频设置等待周期。 */
        FLASH_PrefetchBufSet(FLASH_PrefetchBuf_EN);
#if (APP_SYSCLK_HZ == 128000000UL)
        FLASH_SetLatency(FLASH_LATENCY_3);
#elif (APP_SYSCLK_HZ == 96000000UL)
        FLASH_SetLatency(FLASH_LATENCY_2);
#else
        FLASH_SetLatency(FLASH_LATENCY_1);
#endif

        /*
         * 4. 设置总线分频：
         *    - HCLK (AHB): APP_SYSCLK_HZ (DIV1)
         *    - 128MHz 时 PCLK2=64MHz、PCLK1=32MHz，满足外设总线频率限制；
         *      其他配置沿用 PCLK2=HCLK、PCLK1=HCLK/2。
         */
        RCC_ConfigHclk(RCC_SYSCLK_DIV1);
#if (APP_SYSCLK_HZ == 128000000UL)
        RCC_ConfigPclk2(RCC_HCLK_DIV2);
        RCC_ConfigPclk1(RCC_HCLK_DIV4);
#else
        RCC_ConfigPclk2(RCC_HCLK_DIV1);
        RCC_ConfigPclk1(RCC_HCLK_DIV2);
#endif

        /* 5. HSE 32MHz 经所选分频后输入 PLL。 */
#if (APP_SYSCLK_HZ == 48000000UL)
        /* 32MHz / 2 * 3 = 48MHz。 */
        RCC_ConfigPll(RCC_PLL_SRC_HSE_DIV2, RCC_PLL_MUL_3);
#elif (APP_SYSCLK_HZ == 128000000UL)
        RCC_ConfigPll(RCC_PLL_SRC_HSE_DIV1, RCC_PLL_MUL_4);
#elif (APP_SYSCLK_HZ == 96000000UL)
        RCC_ConfigPll(RCC_PLL_SRC_HSE_DIV1, RCC_PLL_MUL_3);
#else
        RCC_ConfigPll(RCC_PLL_SRC_HSE_DIV1, RCC_PLL_MUL_2);
#endif

        /* 使能 PLL 并等待锁相环锁定稳定 */
        RCC_EnablePll(ENABLE);
        while ((RCC_GetFlagStatus(RCC_FLAG_PLLRD) == RESET) &&
               (pll_wait_count++ < PLL_STARTUP_TIMEOUT))
        {
            /* 等待 PLL 锁定；上限避免异常时钟配置导致永久卡死。 */
        }
        if (RCC_GetFlagStatus(RCC_FLAG_PLLRD) == RESET)
        {
            Clock_PllFailure = 1U;
            RCC_EnablePll(DISABLE);
            SystemCoreClockUpdate();
            return;
        }

        /* 6. 将系统主时钟切换为 PLL 输出 */
        RCC_ConfigSysclk(RCC_SYSCLK_SRC_PLLCLK);

        /* 等待系统时钟源状态切实切换至 PLL 时钟 (消除魔法数字 0x08) */
        pll_wait_count = 0U;
        while ((RCC_GetSysclkSrc() != (uint8_t)RCC_CFG_SCLKSTS_PLL) &&
               (pll_wait_count++ < PLL_STARTUP_TIMEOUT))
        {
            /* 等待时钟源切换；超时后保留 HSI，避免启动阶段死锁。 */
        }
        if (RCC_GetSysclkSrc() != (uint8_t)RCC_CFG_SCLKSTS_PLL)
        {
            Clock_PllFailure = 2U;
            RCC_ConfigSysclk(RCC_SYSCLK_SRC_HSI);
            SystemCoreClockUpdate();
            return;
        }

        /* 7. 根据硬件时钟寄存器更新全局主频变量 SystemCoreClock。 */
        SystemCoreClockUpdate();
    }
    else
    {
        /* 外部晶振起振失败时回退保持在 HSI 运行，避免芯片死锁 */
    }
}

/**
 * @brief  配置系统时钟切换为内部 HSI (8MHz RC 振荡器)
 * @details 在进入 STOP0 停机模式前调用：
 *          1. 开启内部高速 8MHz HSI 振荡器并等待稳定；
 *          2. 切换系统主时钟为 HSI；
 *          3. 关闭 PLL 与外部 HSE 晶振，使系统以最低时钟功耗准备进入深度休眠；
 *          4. 更新 SystemCoreClock 为 8000000。
 */
void SetSysClock_HSI(void)
{
    /* 1. 使能内部高速 8MHz HSI 振荡器 */
    RCC_EnableHsi(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_HSIRD) == RESET)
    {
        /* 等待 HSI 稳定 */
    }

    /* 2. 切换系统主时钟源为 HSI */
    RCC_ConfigSysclk(RCC_SYSCLK_SRC_HSI);

    /* 等待系统时钟源切实切换为 HSI (消除魔法数字 0x00) */
    while (RCC_GetSysclkSrc() != (uint8_t)RCC_CFG_SCLKSTS_HSI)
    {
        /* 等待时钟源切换生效 */
    }

    /* 3. 关闭 PLL 与外部 HSE 振荡器以节电 */
    RCC_EnablePll(DISABLE);
    RCC_ConfigHse(RCC_HSE_DISABLE);

    /* 等待外部晶振彻底停止 */
    while ((RCC->CTRL & RCC_CTRL_HSERDF) != 0U)
    {
        /* 等待 HSE 就绪位清除 */
    }

    /* 4. 更新全局主频变量为 8000000 */
    SystemCoreClockUpdate();
}
