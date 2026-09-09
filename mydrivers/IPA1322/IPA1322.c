/**
 * @file    IPA1322.c
 * @brief   IPA1322 (VT2102兼容) 双波长光学 AFE 传感器驱动实现
 * 
 * 模块职责：
 * 1. 负责光学模拟前端（AFE）的时钟树配置、LED 恒流驱动、TIA 增益与低通 IIR 滤波；
 * 2. 精确计算并烧录暗-亮-暗（Dark-Light-Dark）三采样时钟时隙，实现硬件级二阶环境光消除；
 * 3. 管理硬件 FIFO（64 entries）的水线中断（INTB 水满中断到 MCU PB3 EXTI3）；
 * 4. 解析 DMA 搬运的原始 FIFO 字节流，执行 20 位带符号数符号位扩展，完成 660nm 红光与 905nm 红外的同步配对。
 * 
 * 与其他模块交互：
 * - 依赖 `bsp_hw_i2c.c` 进行硬件 I2C 通信及 DMA 接收；
 * - 供 `app_ppg.c` 数据采集线程调用；
 * - 异常时配合 `bsp_i2c.c` 软件开漏总线进行诊断。
 * 
 * 中断与线程安全：
 * - 所有寄存器访问运行在线程上下文；
 * - 采集线程被 PB3 EXTI3 中断唤醒后调用 `IPA1322_ParseFIFO` 进行解析。
 * 
 * 低功耗考量：
 * - 未连接蓝牙时不启动连续采样，AFE 处于待机状态；
 * - 关机时由 `bsp_power` 切断其 VDD 与 1.8V 域，彻底杜绝静态漏电。
 */

#include "IPA1322.h"
#include "IPA1322_Debug.h"
#include "bsp_power.h"
#include "bsp_hw_i2c.h"
#include "bsp_i2c.h"
#include "bsp_systick.h"
#include "vt2102.h"

/* 诊断与状态变量定义 */
volatile uint8_t  g_i2c_ack_found;
volatile uint8_t  g_i2c_probe_0x48_res;
volatile uint8_t  g_i2c_scan_count;
volatile uint8_t  g_i2c_scan_results[8];
volatile uint8_t  g_ipa1322_chip_id;
volatile uint8_t  g_ipa1322_reg19;
volatile uint8_t  g_ipa1322_reg1a;
volatile uint8_t  g_ipa1322_reg23;
volatile uint8_t  g_ipa1322_reg30;
volatile uint8_t  g_ipa1322_reg2f;
volatile uint8_t  g_ipa1322_reg0a;
volatile uint8_t  g_ipa1322_fifo_cnt;
volatile uint8_t  g_ipa1322_init_res = 0xFFU;
volatile uint32_t g_ipa1322_sample_cnt;
volatile uint32_t g_ipa1322_fifo_overflow;
volatile uint32_t g_ipa1322_bad_tags;
volatile int32_t  g_latest_red;
volatile int32_t  g_latest_ir;
volatile uint32_t g_soft_i2c_probe = 0xFFFFFFFFU;
volatile uint32_t g_soft_i2c_id;
volatile uint8_t  g_ipa1322_failed_reg;
volatile uint8_t  g_ipa1322_expected;
volatile uint8_t  g_ipa1322_actual;
volatile uint8_t  g_ipa1322_slot_dump[2][0x38];
volatile uint8_t  g_ipa1322_regs_dump[0x50];

/* 跨读取周期的红外数据配对暂存状态 */
static bool    ir_pending = false;
static int32_t pending_ir = 0;

/**
 * @brief  将过采样率 OSR 编码为芯片寄存器配置代码
 * @param  OSR 实际过采样倍率 (16/32/64/128/256/512/1024/2048)
 * @return 0~7 对应寄存器值，0xFF 表示不支持
 */
static uint8_t EncodeOSR(uint16_t OSR)
{
    uint8_t code;

    for (code = 0U; code < 8U; code++)
    {
        if (OSR == (16U << code))
        {
            return code;
        }
    }

    return 0xFFU;
}

/**
 * @brief  将 TIA 反馈跨阻编码为芯片寄存器配置代码
 * @param  ResistanceKOhm 阻值（单位：kOhm）
 * @return 寄存器编码，0xFF 表示不支持
 */
static uint8_t EncodeResistance(uint16_t ResistanceKOhm)
{
    static const uint16_t values[] = {0U, 2000U, 1000U, 500U, 250U, 100U, 50U, 10U};
    uint8_t code;

    for (code = 1U; code < 8U; code++)
    {
        if (ResistanceKOhm == values[code])
        {
            return code;
        }
    }

    return 0xFFU;
}

/**
 * @brief  向寄存器写入数据并立即回读校验，确保总线传输可靠
 * @param  reg   目标寄存器地址
 * @param  value 期望写入的值
 * @return true 写入并校验完全一致，false 校验失败并记录故障快照
 */
static bool write_checked(uint8_t reg, uint8_t value)
{
    uint8_t  actual_val = 0U;
    uint32_t error_snapshot = g_hw_i2c_error_count;

    VT2102_WriteRegisters(reg, &value, 1U);
    VT2102_ReadRegisters(reg, &actual_val, 1U);

    if ((error_snapshot == g_hw_i2c_error_count) && (actual_val == value))
    {
        return true;
    }

    /* 记录回读故障现场 */
    g_ipa1322_failed_reg = reg;
    g_ipa1322_expected   = value;
    g_ipa1322_actual     = actual_val;

    return false;
}

/**
 * @brief IPA1322 内部时序与物理通道配置结构体
 */
typedef struct
{
    uint16_t LED0_mA;        /**< LED0 (905nm IR) 峰值电流 (0 ~ 120 mA) */
    uint16_t LED1_mA;        /**< LED1 (660nm Red) 峰值电流 (0 ~ 120 mA) */
    uint16_t ADC_OSR;        /**< ADC 过采样率 (如 1024 对应 256us 积分时间) */
    uint16_t TIA_kOhm;       /**< TIA 跨阻增益 (kOhm，如 250k) */
    uint8_t  TIA_Cap;        /**< TIA 补偿并联电容 (0=2.5pF, 1=5pF, 2=7.5pF, 3=10pF) */
    uint8_t  IIR_Enable;     /**< 内部低通 IIR 滤波器使能 (1 开启, 0 关闭) */
    uint8_t  IIR_Ratio;      /**< IIR 滤波截止比率 (0=b00, 1=b01, 2=b10, 3=b11) */
    uint8_t  EarlySamplePD;  /**< 光电二极管选通修正位 (0x23[7]) */
    uint16_t FrameTicks;     /**< 帧周期 Ticks (基于 30.5us 基底，437 对应约 75.03Hz) */
    uint16_t LightStart_us;  /**< 亮采样相对时隙起点启动延时 (us) */
    uint16_t SampleGap_us;   /**< 暗0与暗1相对亮采样的对称时间间隔 (us) */
    uint16_t LEDLead_us;     /**< LED 相对亮采样提前点亮时间 (us，预留模拟建立时间) */
} IPA1322_Config;

static uint8_t IPA1322_Configure(const IPA1322_Config *Config);

/**
 * @brief  初始化双波长 PPG 传感器
 * @return 0 成功，1 失败
 * @note   所有生理光学测量参数在函数开头的用户调参区集中修改。
 */
uint8_t IPA1322_Init(void)
{
    IPA1322_Config config;

    /* ========================= 用户集中调参区 ========================= */
    config.LED0_mA       = 40U;    /* 905nm 红外发光二极管峰值驱动电流：60mA (0..120mA) */
    config.LED1_mA       = 40U;    /* 660nm 红光发光二极管峰值驱动电流：60mA (0..120mA) */
    config.ADC_OSR       = 1024U;  /* ADC 积分过采样：1024 (对应 1024/4 = 256us 积分时间) */
    config.TIA_kOhm      = 250U;   /* 跨阻放大器反馈电阻：250 kOhm */
    config.TIA_Cap       = 0U;     /* 反馈并联补偿电容：0 对应 2.5pF */
    config.IIR_Enable    = 1U;     /* 开启芯片内置低通 IIR 滤波 */
    config.IIR_Ratio     = 1U;     /* IIR 滤波比率：b01 */
    config.EarlySamplePD = 1U;     /* 硬件样品版本 Photodiode 修正 (0x23[7]=1) */
    config.FrameTicks    = 437U;   /* 帧率配置：437 Ticks 对应 75.03Hz (4MHz/(122*437)) */
    config.LightStart_us = 2048U;  /* 亮采样起始延时：2048 us */
    config.SampleGap_us  = 552U;   /* 对称采样间隙：552 us (保证暗0与暗1完全对称) */
    config.LEDLead_us    = 20U;    /* LED 提前点亮时间：20 us (消除 LED 上冲毛刺) */
    /* ================================================================== */

    return IPA1322_Configure(&config);
}

/**
 * @brief  将配置参数转化为芯片时序与寄存器列表并烧录
 */
static uint8_t IPA1322_Configure(const IPA1322_Config *Config)
{
    /* 时隙基础寄存器模版配置表 */
    uint8_t slot_settings[][2] = {
        {0x31, 0xCC}, {0x32, 0},    {0x33, 0},    {0x34, 0},    {0x35, 0x40},
        {0x36, 0},    {0x37, 0},    {0x38, 0},    {0x39, 0},
        {0x3A, 0},    {0x3B, 0},    {0x3C, 0},    {0x3D, 0},
        {0x3E, 0},    {0x3F, 0},    {0x41, 0},    {0x44, 0},
        {0x46, 0},    {0x47, 1},    {0x48, 0},
        {0x50, 0},    {0x51, 0},    {0x54, 0},    {0x58, 0}
    };

    uint32_t timing_points[5];
    uint8_t  osr_code = EncodeOSR(Config->ADC_OSR);
    uint8_t  rf_code  = EncodeResistance(Config->TIA_kOhm);
    uint8_t  slot_idx;
    uint8_t  soft_id;
    uint8_t  reg_val;
    uint8_t  table_idx;
    uint32_t error_snapshot;
    uint32_t led_current_code;

    g_ipa1322_init_res = 7U;

    /* 
     * 参数边界与时序防重叠安全校验：
     * 1. 编码必须合法；
     * 2. 电流不超过 120mA 极限；
     * 3. 亮采样时间必须大于暗0采样和 LED 建立时间；
     * 4. 两个时隙总采样周期不得超出单帧周期的一半，杜绝时隙碰撞。
     */
    if ((osr_code == 0xFFU) || (rf_code == 0xFFU) ||
        (Config->LED0_mA > 120U) || (Config->LED1_mA > 120U) || (Config->TIA_Cap > 3U) ||
        (Config->IIR_Enable > 1U) || (Config->IIR_Ratio > 3U) || (Config->EarlySamplePD > 1U) ||
        (Config->FrameTicks == 0U) || (Config->LEDLead_us < 20U) ||
        (Config->LightStart_us <= Config->SampleGap_us) ||
        (Config->LightStart_us <= (Config->LEDLead_us + 4U)) ||
        (Config->SampleGap_us <= (Config->ADC_OSR / 4U + 2U + Config->LEDLead_us + 4U)) ||
        (((uint32_t)Config->LightStart_us + Config->SampleGap_us) > 16383U) ||
        (2U * ((uint32_t)Config->LightStart_us + Config->SampleGap_us + Config->ADC_OSR / 4U + 4U) >=
         ((uint32_t)Config->FrameTicks * 122U / 4U)))
    {
        return 1U;
    }

    /* 
     * 计算 5 个核心时序控制点（转换为芯片 0.25us 时钟 Tick）：
     * [0] 暗0采样起点
     * [1] LED 开启时钟
     * [2] 模拟偏置稳定点
     * [3] 亮采样起点
     * [4] 暗1采样起点
     */
    timing_points[0] = ((uint32_t)Config->LightStart_us - Config->SampleGap_us) * 4U;
    timing_points[1] = ((uint32_t)Config->LightStart_us - Config->LEDLead_us - 4U) * 4U;
    timing_points[2] = ((uint32_t)Config->LightStart_us - Config->LEDLead_us) * 4U;
    timing_points[3] = (uint32_t)Config->LightStart_us * 4U;
    timing_points[4] = ((uint32_t)Config->LightStart_us + Config->SampleGap_us) * 4U;

    slot_settings[1][1] = osr_code;
    for (table_idx = 0U; table_idx < 5U; table_idx++)
    {
        slot_settings[5U + table_idx * 2U][1]     = (uint8_t)(timing_points[table_idx] >> 8);
        slot_settings[6U + table_idx * 2U][1]     = (uint8_t)timing_points[table_idx];
    }
    slot_settings[17][1] = (uint8_t)((Config->IIR_Ratio << 4) | Config->IIR_Enable);
    slot_settings[21][1] = (uint8_t)((Config->TIA_Cap << 3) | rf_code);

    /* 初始化变量与电源总线状态 */
    ir_pending           = false;
    g_ipa1322_init_res   = 1U;
    g_ipa1322_failed_reg = 0U;
    g_ipa1322_chip_id    = 0U;

    /* 确保负载开关上电并初始化 I2C1 外设 */
    bsp_sensor_power_on();
    bsp_hw_i2c_init();

    g_i2c_probe_0x48_res = bsp_hw_i2c_probe_addr(0x48U);
    if (g_i2c_probe_0x48_res == 0U)
    {
        /* 硬件 I2C 探测失败时，调用独立软件 I2C 探测以排查硬件总线死锁 */
        bsp_i2c_init();
        bsp_i2c_start();
        g_soft_i2c_probe = bsp_i2c_write_byte(0x90U);
        bsp_i2c_stop();

        soft_id = 0U;
        g_soft_i2c_id = (bsp_i2c_read_reg(0x48U, 0xFFU, &soft_id) == 0U) ? soft_id : 0U;
        bsp_hw_i2c_init();
        return 1U;
    }

    g_i2c_ack_found = 0x48U;
    error_snapshot  = g_hw_i2c_error_count;

    /* 软复位芯片并等待 50ms 模拟稳压建立 */
    VT2102_SoftReset();
    delay_ms(50U);

    g_ipa1322_chip_id  = VT2102_GetChipID();
    g_ipa1322_init_res = 2U;

    if ((error_snapshot != g_hw_i2c_error_count) || (g_ipa1322_chip_id != 0xAFU))
    {
        return 1U;
    }

    g_ipa1322_init_res = 3U;

    /* 
     * 配置芯片全局主控与时钟：
     * 0x1A/0x19: 配置 4MHz 主时钟，关闭独立 32kHz 异步振荡器以杜绝采样抖动；
     * 0x21/0x22: 写入单帧周期 FrameTicks；
     * 0x23: 光电二极管选通配置；
     * 0x25: 环境光自动消除模式使能。
     */
    if (!write_checked(0x1AU, 0x01U) || !write_checked(0x19U, 0x02U) ||
        !write_checked(0x20U, 0x00U) ||
        !write_checked(0x21U, (uint8_t)((Config->FrameTicks - 1U) >> 8)) ||
        !write_checked(0x22U, (uint8_t)(Config->FrameTicks - 1U)) ||
        !write_checked(0x23U, Config->EarlySamplePD ? 0x80U : 0x00U) ||
        !write_checked(0x25U, 0x02U))
    {
        return 1U;
    }

    /* 
     * 配置 4 个物理时隙 (Slot 0 ~ 3)：
     * Slot 0: 905nm 红外发光与采样
     * Slot 1: 660nm 红光发光与采样
     * Slot 2/3: 禁用
     */
    for (slot_idx = 0U; slot_idx < 4U; slot_idx++)
    {
        if (!write_checked(0x30U, slot_idx))
        {
            return 1U;
        }

        if (slot_idx >= 2U)
        {
            if (!write_checked(0x31U, 0x00U))
            {
                return 1U;
            }
            continue;
        }

        /* 计算 LED 驱动电流 DAC 编码 (0 ~ 120mA 对应 0 ~ 255) */
        led_current_code = (((slot_idx == 0U) ? Config->LED0_mA : Config->LED1_mA) * 256UL + 60U) / 120U;
        slot_settings[15][1] = (uint8_t)((led_current_code > 255U) ? 255U : led_current_code);

        for (table_idx = 0U; table_idx < (sizeof(slot_settings) / sizeof(slot_settings[0])); table_idx++)
        {
            if (!write_checked(slot_settings[table_idx][0], slot_settings[table_idx][1]))
            {
                return 1U;
            }
        }

        if (!write_checked(0x40U, (uint8_t)(1U << slot_idx)))
        {
            return 1U;
        }

        VT2102_ReadRegisters(0x31U, (uint8_t *)g_ipa1322_slot_dump[slot_idx], 0x38U);
        if (error_snapshot != g_hw_i2c_error_count)
        {
            return 1U;
        }
    }

    /* 
     * 配置硬件 FIFO 与 INTB 中断输出模式：
     * 水满门限配置为剩余空间为 8 时触发 (约积累 56 entries 时触发水满)
     */
    VT2102_ClearFIFO();
    VT2102_ConfigFIFOAFull(8U, VT2102_FIFO_AF_TYPE_NON_CONSECUTIVE);
    VT2102_SetInterruptEnabled(VT2102_INTR_SOURCE_FIFO_AFULL, false);
    VT2102_SetInterruptEnabled(VT2102_INTR_SOURCE_FIFO_OFLOW, false);

    /* 0x0A: INTB 输出模式配置为开漏 (OD)、低电平有效，并使能芯片内部上拉 */
    reg_val = 0x06U;
    VT2102_WriteRegisters(0x0AU, &reg_val, 1U);
    VT2102_ReadRegisters(0x0AU, (uint8_t *)&g_ipa1322_reg0a, 1U);
    if (error_snapshot != g_hw_i2c_error_count)
    {
        return 1U;
    }

    if (!write_checked(0x30U, 0x08U))
    {
        return 1U;
    }

    VT2102_ReadRegisters(0x19U, (uint8_t *)&g_ipa1322_reg19, 1U);
    VT2102_ReadRegisters(0x1AU, (uint8_t *)&g_ipa1322_reg1a, 1U);
    VT2102_ReadRegisters(0x23U, (uint8_t *)&g_ipa1322_reg23, 1U);
    VT2102_ReadRegisters(0x30U, (uint8_t *)&g_ipa1322_reg30, 1U);

    if ((error_snapshot != g_hw_i2c_error_count) || ((g_ipa1322_reg30 & 0x08U) == 0U))
    {
        return 1U;
    }

    g_ipa1322_init_res = 0U;
    return 0U;
}

/**
 * @brief  启动连续采样与中断使能
 */
uint8_t IPA1322_Start(void)
{
    uint8_t  enable_cmd = 1U;
    uint32_t error_snapshot = g_hw_i2c_error_count;

    ir_pending = false;

    /* 清空 FIFO 与残留中断标志 */
    VT2102_ClearFIFO();
    (void)IPA1322_ReadInterruptFlags();

    /* 使能水满与溢出中断 */
    VT2102_SetInterruptEnabled(VT2102_INTR_SOURCE_FIFO_AFULL, true);
    VT2102_SetInterruptEnabled(VT2102_INTR_SOURCE_FIFO_OFLOW, true);

    /* 写入运行控制寄存器 0x2F = 1 开启时序循环 */
    VT2102_WriteRegisters(0x2FU, &enable_cmd, 1U);
    VT2102_ReadRegisters(0x2FU, (uint8_t *)&g_ipa1322_reg2f, 1U);

    g_ipa1322_init_res = 4U;

    if ((error_snapshot != g_hw_i2c_error_count) || ((g_ipa1322_reg2f & 1U) == 0U))
    {
        return 1U;
    }

    g_ipa1322_init_res = 0U;
    return 0U;
}

/**
 * @brief  停止连续采样并关闭中断
 */
void IPA1322_Stop(void)
{
    uint8_t disable_cmd = 0U;

    VT2102_WriteRegisters(0x2FU, &disable_cmd, 1U);
    VT2102_SetInterruptEnabled(VT2102_INTR_SOURCE_FIFO_AFULL, false);
    VT2102_SetInterruptEnabled(VT2102_INTR_SOURCE_FIFO_OFLOW, false);
}

/**
 * @brief  获取当前 FIFO 样本条数
 */
uint8_t IPA1322_GetFIFOCount(void)
{
    uint8_t count = VT2102_GetFIFOCount();

    if ((g_hw_i2c_last_err != 0U) || (count > 64U))
    {
        g_ipa1322_init_res = 5U;
        return 0U;
    }

    g_ipa1322_fifo_cnt = count;
    return count;
}

/**
 * @brief  读取中断状态寄存器
 */
uint8_t IPA1322_ReadInterruptFlags(void)
{
    uint8_t flags[4] = {0U};

    VT2102_ReadAllInterruptFlags(flags);
    return flags[0];
}

/**
 * @brief  清空硬件 FIFO
 */
void IPA1322_ClearFIFO(void)
{
    ir_pending = false;
    VT2102_ClearFIFO();
}

/**
 * @brief  解析由 DMA 批量读取的原始 FIFO 字节流，并配对红光与红外数据
 * @details 数据格式说明：
 *          每个 FIFO entry 占 3 字节 (24 位)：
 *          Byte 0: [7:6] Slot 编号 (0=IR, 1=Red), [5:4] 数据 Tag (1=IIR 滤波值), [3:0] 数据最高 4 位
 *          Byte 1: 数据中间 8 位
 *          Byte 2: 数据最低 8 位
 *          拼接成 20 位有符号整数后进行符号扩展：若 bit 19 为 1，则减去 2^20 (0x100000)。
 */
uint8_t IPA1322_ParseFIFO(const uint8_t *raw, uint8_t entries,
                          IPA1322_Data *pairs, uint8_t max_pairs)
{
    uint8_t  entry_idx;
    uint8_t  pairs_produced = 0U;
    uint8_t  slot_id;
    uint8_t  tag_id;
    uint32_t raw_code_20bit;

    if (!raw || !pairs || (max_pairs == 0U) || (entries > IPA1322_FIFO_DEPTH))
    {
        return 0U;
    }

    for (entry_idx = 0U; (entry_idx < entries) && (pairs_produced < max_pairs); entry_idx++, raw += 3U)
    {
        slot_id = raw[0] >> 6;
        tag_id  = (raw[0] >> 4) & 0x03U;

        /* 仅接收 Tag 1 (IIR 滤波二阶消除结果) 且 Slot 仅限 0 (IR) 或 1 (Red) */
        if ((tag_id != 1U) || (slot_id > 1U))
        {
            ir_pending = false;
            g_ipa1322_bad_tags++;
            continue;
        }

        /* 拼接 20 位原始数据 */
        raw_code_20bit = ((uint32_t)(raw[0] & 0x0FU) << 16) |
                         ((uint32_t)raw[1] << 8) |
                         (uint32_t)raw[2];

        if (slot_id == 0U)
        {
            /* Slot 0: 905nm 红外数据，暂存并等待配对红光 */
            if (ir_pending)
            {
                g_ipa1322_bad_tags++; /* 连续两次 Slot 0，说明丢掉了红光帧 */
            }

            pending_ir = (raw_code_20bit & 0x80000U) ?
                         ((int32_t)raw_code_20bit - 0x100000) : (int32_t)raw_code_20bit;
            ir_pending = true;
        }
        else if (ir_pending)
        {
            /* Slot 1: 660nm 红光数据，与此前暂存的红外数据完成配对输出 */
            pairs[pairs_produced].red_signal = (raw_code_20bit & 0x80000U) ?
                                               ((int32_t)raw_code_20bit - 0x100000) : (int32_t)raw_code_20bit;
            pairs[pairs_produced].ir_signal  = pending_ir;

            /* 更新全局调试观测变量 */
            g_latest_red = pairs[pairs_produced].red_signal;
            g_latest_ir  = pairs[pairs_produced].ir_signal;

            pairs_produced++;
            g_ipa1322_sample_cnt++;
            ir_pending = false;
        }
        else
        {
            /* 收到 Slot 1 但此前没有红外数据暂存，失步丢弃 */
            g_ipa1322_bad_tags++;
        }
    }

    return pairs_produced;
}

/**
 * @brief  轮询读取并解析 FIFO 数据对（备用阻塞式接口）
 */
uint8_t IPA1322_ReadPairs(IPA1322_Data *pairs, uint8_t max_pairs)
{
    VT2102FIFOData raw_words[16];
    uint8_t  fifo_entries;
    uint8_t  idx;
    uint8_t  pairs_produced = 0U;
    uint8_t  slot_id;
    uint8_t  tag_id;
    uint32_t raw_code_20bit;
    uint32_t error_snapshot = g_hw_i2c_error_count;

    if (!pairs || (max_pairs == 0U) || (g_ipa1322_init_res != 0U))
    {
        return 0U;
    }

    if (max_pairs > 8U)
    {
        max_pairs = 8U;
    }

    fifo_entries = IPA1322_GetFIFOCount();
    if ((fifo_entries == 0U) || (g_ipa1322_init_res != 0U))
    {
        return 0U;
    }

    /* FIFO 满溢处理：丢弃可能不完整的截断帧并清空 */
    if (fifo_entries == 64U)
    {
        g_ipa1322_fifo_overflow++;
        ir_pending = false;
        VT2102_ClearFIFO();

        if (error_snapshot != g_hw_i2c_error_count)
        {
            g_ipa1322_init_res = 5U;
        }
        return 0U;
    }

    if (fifo_entries > (max_pairs * 2U))
    {
        fifo_entries = max_pairs * 2U;
    }

    VT2102_ReadFIFO(raw_words, fifo_entries);
    if (error_snapshot != g_hw_i2c_error_count)
    {
        ir_pending = false;
        g_ipa1322_init_res = 5U;
        return 0U;
    }

    for (idx = 0U; idx < fifo_entries; idx++)
    {
        slot_id = raw_words[idx].pu8_buff[0] >> 6;
        tag_id  = (raw_words[idx].pu8_buff[0] >> 4) & 0x03U;

        if ((tag_id != 1U) || (slot_id > 1U))
        {
            ir_pending = false;
            g_ipa1322_bad_tags++;
            continue;
        }

        raw_code_20bit = ((uint32_t)(raw_words[idx].pu8_buff[0] & 0x0FU) << 16) |
                         ((uint32_t)raw_words[idx].pu8_buff[1] << 8) |
                         (uint32_t)raw_words[idx].pu8_buff[2];

        if (slot_id == 0U)
        {
            if (ir_pending)
            {
                g_ipa1322_bad_tags++;
            }
            pending_ir = (raw_code_20bit & 0x80000U) ?
                         ((int32_t)raw_code_20bit - 0x100000) : (int32_t)raw_code_20bit;
            ir_pending = true;
        }
        else if (ir_pending)
        {
            pairs[pairs_produced].red_signal = (raw_code_20bit & 0x80000U) ?
                                               ((int32_t)raw_code_20bit - 0x100000) : (int32_t)raw_code_20bit;
            pairs[pairs_produced].ir_signal  = pending_ir;

            g_latest_red = pairs[pairs_produced].red_signal;
            g_latest_ir  = pending_ir;

            pairs_produced++;
            g_ipa1322_sample_cnt++;
            ir_pending = false;
        }
        else
        {
            g_ipa1322_bad_tags++;
        }
    }

    return pairs_produced;
}
