/**
 * @file    CW2015_Reg.h
 * @brief   CW2015 电池电量计寄存器与位掩码定义
 * @details 记录 CW2015 I2C 寄存器地址、控制模式、报警位定义及电压换算常数。
 */

#ifndef __CW2015_REG_H__
#define __CW2015_REG_H__

#define CW2015_ADDRESS              0x62U   /**< 7 位 I2C 设备地址 (写: 0xC4, 读: 0xC5) */

/* 核心寄存器地址定义 */
#define CW2015_REG_VERSION          0x00U   /**< 芯片版本寄存器 (通常为 0x6F 或 0x73 等) */
#define CW2015_REG_VCELL            0x02U   /**< 电池电压 VCELL 寄存器 (双字节，高位 0x02，低位 0x03) */
#define CW2015_REG_SOC              0x04U   /**< 剩余电量百分比 SOC (双字节，高位整数%，低位 1/256%) */
#define CW2015_REG_RRT_ALERT        0x06U   /**< 剩余运行时间及低电量告警标志寄存器 */
#define CW2015_REG_CONFIG           0x08U   /**< 配置寄存器 (包含 ATHD 告警阈值与 UPDATE_FLAG 更新标志) */
#define CW2015_REG_MODE             0x0AU   /**< 运行模式寄存器 (休眠、重启、快速启动等) */
#define CW2015_REG_BATINFO          0x10U   /**< 电池建模曲线 (Profile) 首地址，共 64 字节 (0x10~0x4F) */

/* MODE 寄存器 (0x0A) 位掩码与模式命令 */
#define CW2015_MODE_SLEEP_MASK      0xC0U   /**< 休眠模式掩码 */
#define CW2015_MODE_SLEEP           0xC0U   /**< 进入休眠模式 (低功耗模式，关闭内部 ADC) */
#define CW2015_MODE_NORMAL          0x00U   /**< 正常工作模式 */
#define CW2015_MODE_QUICK_START     0x30U   /**< 快速启动模式 (以当前开路电压立即重估 SOC，跳过平滑滤波) */
#define CW2015_MODE_RESTART         0x0FU   /**< 软重启命令 (需在 20ms 内切回 NORMAL) */

/* CONFIG 寄存器 (0x08) 位掩码 */
#define CW2015_CONFIG_UPDATE        0x02U   /**< 电池建模数据已烧录生效标志位 (UPDATE_FLAG) */
#define CW2015_ALERT_THD_MASK       0xF8U   /**< 低电量报警阈值掩码 ATHD[4:0] */

/* RRT_ALERT 寄存器 (0x06) 标志 */
#define CW2015_ALERT_FLAG           0x80U   /**< 低电量报警标志位 (ALRT)，达到阈值时置 1 */

/* 模拟转换分辨率换算常量 */
#define CW2015_VOLTAGE_UV_PER_LSB   305U    /**< 电压 LSB 分辨率：305 uV/LSB (0.305 mV/LSB) */

#endif /* __CW2015_REG_H__ */
