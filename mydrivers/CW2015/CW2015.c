/**
 * @file    CW2015.c
 * @brief   CW2015 电池电量计应用驱动实现
 * 
 * 模块职责：
 * 1. 管理 CW2015 锂电池电量计的初始化、电池曲线（Battery Profile）烧录与校验；
 * 2. 提供电池电压（mV）高精度采样，内置 3 样本中值滤波抗脉冲毛刺；
 * 3. 获取精确的电池剩余容量百分比（SOC），供系统低电量告警与蓝牙状态同步；
 * 4. 控制芯片运行模式（正常模式 / 休眠模式 / 快速开路电压自校准模式）。
 * 
 * 与其他模块交互：
 * - 底层调用 `MyI2C2.c` 的寄存器读写接口；
 * - 供 `app_main.c` 启动时初始化，由 `app_ble.c` 周期性读取电池状态并组包广播。
 * 
 * 中断与线程安全：
 * - I2C2 为 CW2015 独占总线，读写在线程上下文中执行，内部具备超时保护。
 * 
 * 低功耗考量：
 * - 初始化成功后芯片在后台以极低功耗自动累加库仑电量；
 * - 若芯片未成功配置或处于非活动期，可切入 SLEEP 模式降低电量消耗。
 */

#include "CW2015.h"
#include "CW2015_Reg.h"
#include "MyI2C2.h"
#include <rtthread.h>

#define CW2015_PROFILE_SIZE 64U

/* 全局诊断与扫描状态 */
volatile uint8_t g_cw2015_i2c_ack  = 0xFFU;
volatile uint8_t g_cw2015_version  = 0xFFU;
volatile uint8_t g_cw2015_address  = 0xFFU;
volatile uint8_t g_i2c2_device_count;
volatile uint8_t g_i2c2_addresses[8];

/**
 * 示例电池模型参数 (Battery Profile，共 64 字节)
 * @note 本模型用于工程联调；在量产阶段必须联系电芯原厂提取当前电芯的放电曲线并替换。
 */
static const uint8_t CW2015_BatteryProfile[CW2015_PROFILE_SIZE] = {
    0x15, 0x7E, 0x7C, 0x5C, 0x64, 0x6A, 0x65, 0x5C, 0x55, 0x53, 0x56, 0x61, 0x6F, 0x66, 0x50, 0x48,
    0x43, 0x42, 0x40, 0x43, 0x4B, 0x5F, 0x75, 0x7D, 0x52, 0x44, 0x07, 0xAE, 0x11, 0x22, 0x40, 0x56,
    0x6C, 0x7C, 0x85, 0x86, 0x3D, 0x19, 0x8D, 0x1B, 0x06, 0x34, 0x46, 0x79, 0x8D, 0x90, 0x90, 0x46,
    0x67, 0x80, 0x97, 0xAF, 0x80, 0x9F, 0xAE, 0xCB, 0x2F, 0x00, 0x64, 0xA5, 0xB5, 0x11, 0xD0, 0x11
};

/**
 * @brief  写入 CW2015 单字节寄存器
 */
static uint8_t CW2015_WriteByte(uint8_t RegAddress, uint8_t Data)
{
    return MyI2C2_WriteRegister(g_cw2015_address, RegAddress, &Data, 1U);
}

/**
 * @brief  读取 CW2015 单字节寄存器
 */
static uint8_t CW2015_ReadByte(uint8_t RegAddress, uint8_t *Data)
{
    return MyI2C2_ReadRegister(g_cw2015_address, RegAddress, Data, 1U);
}

/**
 * @brief  读取 CW2015 芯片版本号
 */
uint8_t CW2015_ReadVersion(uint8_t *Version)
{
    if (!Version)
    {
        return 1U;
    }

    return CW2015_ReadByte(CW2015_REG_VERSION, Version);
}

/**
 * @brief  软复位 CW2015 芯片逻辑
 */
uint8_t CW2015_Reset(void)
{
    if (CW2015_WriteByte(CW2015_REG_MODE, CW2015_MODE_RESTART) != 0U)
    {
        return 1U;
    }

    rt_thread_mdelay(1U);

    if (CW2015_WriteByte(CW2015_REG_MODE, CW2015_MODE_NORMAL) != 0U)
    {
        return 1U;
    }

    rt_thread_mdelay(1U);
    return 0U;
}

/**
 * @brief  执行 QuickStart（快速评估开路电压）
 */
uint8_t CW2015_QuickStart(void)
{
    if (CW2015_WriteByte(CW2015_REG_MODE, CW2015_MODE_QUICK_START) != 0U)
    {
        return 1U;
    }

    rt_thread_mdelay(10U);
    return CW2015_WriteByte(CW2015_REG_MODE, CW2015_MODE_NORMAL);
}

/**
 * @brief  更新并校验 CW2015 电池曲线参数
 * @details 将 64 字节模型数据写入 0x10~0x4F，回读比对一致后将 UPDATE 标志位置 1 并执行软复位。
 */
static uint8_t CW2015_UpdateProfile(void)
{
    uint8_t profile_index;
    uint8_t read_back_value;
    uint8_t config_value;

    /* 1. 写入 64 字节电池曲线参数 */
    for (profile_index = 0U; profile_index < CW2015_PROFILE_SIZE; profile_index++)
    {
        if (CW2015_WriteByte((uint8_t)(CW2015_REG_BATINFO + profile_index),
                             CW2015_BatteryProfile[profile_index]) != 0U)
        {
            return 1U;
        }
    }

    /* 2. 逐字节回读校验，确保 I2C 通信无误码 */
    for (profile_index = 0U; profile_index < CW2015_PROFILE_SIZE; profile_index++)
    {
        if ((CW2015_ReadByte((uint8_t)(CW2015_REG_BATINFO + profile_index), &read_back_value) != 0U) ||
            (read_back_value != CW2015_BatteryProfile[profile_index]))
        {
            return 1U;
        }
    }

    /* 3. 读取 CONFIG 寄存器，置位 CONFIG_UPDATE 标志位告知芯片曲线已就绪 */
    if (CW2015_ReadByte(CW2015_REG_CONFIG, &config_value) != 0U)
    {
        return 1U;
    }

    config_value = (uint8_t)((config_value & 0x07U) | CW2015_CONFIG_UPDATE);

    if (CW2015_WriteByte(CW2015_REG_CONFIG, config_value) != 0U)
    {
        return 1U;
    }

    /* 4. 重启芯片以激活新曲线 */
    return CW2015_Reset();
}

/**
 * @brief  初始化 CW2015 电池电量计
 */
uint8_t CW2015_Init(void)
{
    uint8_t scan_index;
    uint8_t config_val;
    uint8_t soc_val;

    /* 1. 初始化 I2C2 总线并扫描所有从机地址 */
    MyI2C2_Init();
    g_i2c2_device_count = MyI2C2_Scan((uint8_t *)g_i2c2_addresses, sizeof(g_i2c2_addresses));

    /* 匹配 CW2015 默认地址 0x62 */
    for (scan_index = 0U; (scan_index < g_i2c2_device_count) && (scan_index < sizeof(g_i2c2_addresses)); scan_index++)
    {
        if (g_i2c2_addresses[scan_index] == CW2015_ADDRESS)
        {
            g_cw2015_address = CW2015_ADDRESS;
        }
    }

    if ((g_cw2015_address == 0xFFU) && (g_i2c2_device_count == 1U))
    {
        g_cw2015_address = g_i2c2_addresses[0];
    }

    g_cw2015_i2c_ack = (g_cw2015_address == 0xFFU) ? 1U : 0U;

    /* 2. 检查设备应答并切入正常工作模式 */
    if ((g_cw2015_i2c_ack != 0U) ||
        (CW2015_ReadVersion((uint8_t *)&g_cw2015_version) != 0U) ||
        (CW2015_WriteByte(CW2015_REG_MODE, CW2015_MODE_NORMAL) != 0U) ||
        (CW2015_ReadByte(CW2015_REG_CONFIG, &config_val) != 0U))
    {
        return 1U;
    }

    /* 3. 检查电池曲线状态：若未烧录或校验不匹配则重新烧录 */
    if ((config_val & CW2015_CONFIG_UPDATE) == 0U)
    {
        if (CW2015_UpdateProfile() != 0U)
        {
            return 1U;
        }
    }
    else
    {
        for (scan_index = 0U; scan_index < CW2015_PROFILE_SIZE; scan_index++)
        {
            if (CW2015_ReadByte((uint8_t)(CW2015_REG_BATINFO + scan_index), &config_val) != 0U)
            {
                return 1U;
            }

            if (config_val != CW2015_BatteryProfile[scan_index])
            {
                break;
            }
        }

        if ((scan_index != CW2015_PROFILE_SIZE) && (CW2015_UpdateProfile() != 0U))
        {
            return 1U;
        }
    }

    /* 4. 上电后主动触发一次 QuickStart，以当前开路电压立即重估 SOC */
    if (CW2015_QuickStart() != 0U)
    {
        return 1U;
    }

    /* 5. 最长等待 3 秒 (30 * 100ms)，等待芯片输出 <= 100% 的有效 SOC */
    for (scan_index = 0U; scan_index < 30U; scan_index++)
    {
        if (CW2015_ReadByte(CW2015_REG_SOC, &soc_val) != 0U)
        {
            return 1U;
        }

        if (soc_val <= 100U)
        {
            return 0U; /* 成功获取有效 SOC */
        }

        rt_thread_mdelay(100U);
    }

    /* 超时未获取有效 SOC，切入休眠并报错 */
    (void)CW2015_WriteByte(CW2015_REG_MODE, CW2015_MODE_SLEEP);
    return 1U;
}

/**
 * @brief  读取电池电量（SOC）与端电压（mV）
 * @details 采样说明：
 *          1. 校验版本与从机状态，读取 SOC 寄存器；
 *          2. 连续采样 3 次 VCELL 寄存器并执行中值滤波（取中间值），
 *             消除充电瞬间或负载瞬态突变引发的毛刺；
 *          3. 将 14 位原始 ADC 计数乘以 305uV/LSB 换算为实际毫伏电压；
 *          4. 检查电池电压是否在 2500mV ~ 5000mV 合理锂电池范围内。
 */
uint8_t CW2015_ReadData(CW2015_Data *Data)
{
    uint8_t  raw_buf[2];
    uint8_t  version_id;
    uint32_t samples[3];
    uint32_t sample_sum;
    uint32_t min_sample;
    uint32_t max_sample;
    uint32_t median_sample;

    if (!Data || (g_cw2015_address == 0xFFU) ||
        (CW2015_ReadVersion(&version_id) != 0U) ||
        (MyI2C2_ReadRegister(g_cw2015_address, CW2015_REG_SOC, raw_buf, 2U) != 0U))
    {
        return 1U;
    }

    if (raw_buf[0] > 100U)
    {
        return 1U;
    }

    Data->Capacity     = raw_buf[0];
    Data->CapacityX100 = (uint16_t)raw_buf[0] * 100U + ((uint16_t)raw_buf[1] * 100U) / 256U;

    /* 连续采集 3 次 VCELL 寄存器 */
    for (uint8_t idx = 0U; idx < 3U; idx++)
    {
        if (MyI2C2_ReadRegister(g_cw2015_address, CW2015_REG_VCELL, raw_buf, 2U) != 0U)
        {
            return 1U;
        }
        samples[idx] = ((uint32_t)raw_buf[0] << 8) | raw_buf[1];
    }

    /*
     * 3 样本中值滤波 (Median Filter) 原理：
     * 找出 3 个样本的最小值与最大值，从总和中减去最小值与最大值，剩余即为中间值。
     */
    sample_sum = samples[0] + samples[1] + samples[2];

    min_sample = samples[0];
    if (samples[1] < min_sample)
    {
        min_sample = samples[1];
    }
    if (samples[2] < min_sample)
    {
        min_sample = samples[2];
    }

    max_sample = samples[0];
    if (samples[1] > max_sample)
    {
        max_sample = samples[1];
    }
    if (samples[2] > max_sample)
    {
        max_sample = samples[2];
    }

    median_sample = sample_sum - min_sample - max_sample;

    /* 换算为毫伏 (mV)：ADC 值 * 305uV / 1000 */
    Data->VoltageMv = (uint16_t)((median_sample * CW2015_VOLTAGE_UV_PER_LSB) / 1000U);

    /* 合理性门限检查：单节锂电池正常工作区间为 2.5V ~ 5.0V */
    if ((Data->VoltageMv < 2500U) || (Data->VoltageMv > 5000U))
    {
        return 1U;
    }

    return 0U;
}

/**
 * @brief  配置低电量硬件报警阈值 (1% ~ 31%)
 */
uint8_t CW2015_SetAlertThreshold(uint8_t Percent)
{
    uint8_t config_value;

    if ((Percent > 31U) || (CW2015_ReadByte(CW2015_REG_CONFIG, &config_value) != 0U))
    {
        return 1U;
    }

    config_value = (uint8_t)((config_value & (uint8_t)~CW2015_ALERT_THD_MASK) | (Percent << 3));
    return CW2015_WriteByte(CW2015_REG_CONFIG, config_value);
}

/**
 * @brief  查询并清除低电量中断告警标志
 */
uint8_t CW2015_ClearAlert(uint8_t *WasActive)
{
    uint8_t raw_alert[2];

    if (!WasActive || (MyI2C2_ReadRegister(g_cw2015_address, CW2015_REG_RRT_ALERT, raw_alert, 2U) != 0U))
    {
        return 1U;
    }

    *WasActive = ((raw_alert[0] & CW2015_ALERT_FLAG) != 0U) ? 1U : 0U;

    if (*WasActive == 0U)
    {
        return 0U;
    }

    /* 清除 ALRT 标志并回写 */
    raw_alert[0] &= (uint8_t)~CW2015_ALERT_FLAG;
    return MyI2C2_WriteRegister(g_cw2015_address, CW2015_REG_RRT_ALERT, raw_alert, 2U);
}
