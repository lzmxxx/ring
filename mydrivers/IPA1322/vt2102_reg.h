#ifndef VT2102_REG_H__
#define VT2102_REG_H__

#include <stdint.h>

#define MASK(x)            x##_MASK
#define BIT_OFFSET(x)      x##_BIT_OFFSET

#define REG_INTR_RAW_1_ADDR												(uint8_t)(0x0)
#define FIELD_INTR_FIFO_AFULL_MASK										(uint8_t)(0x80)
#define FIELD_INTR_FIFO_AFULL_BIT_OFFSET								(uint8_t)(0x7)
#define FIELD_INTR_FIFO_DATA_RDY_MASK									(uint8_t)(0x40)
#define FIELD_INTR_FIFO_DATA_RDY_BIT_OFFSET								(uint8_t)(0x6)
#define FIELD_INTR_FIFO_OFLOW_MASK										(uint8_t)(0x20)
#define FIELD_INTR_FIFO_OFLOW_BIT_OFFSET								(uint8_t)(0x5)
#define FIELD_INTR_FIFO_UFLOW_MASK										(uint8_t)(0x10)
#define FIELD_INTR_FIFO_UFLOW_BIT_OFFSET								(uint8_t)(0x4)
#define FIELD_INTR_FRAME_OVERLAP_MASK									(uint8_t)(0x8)
#define FIELD_INTR_FRAME_OVERLAP_BIT_OFFSET								(uint8_t)(0x3)
#define FIELD_INTR_PWR_RDY_MASK											(uint8_t)(0x1)
#define FIELD_INTR_PWR_RDY_BIT_OFFSET									(uint8_t)(0x0)

#define REG_INTR_RAW_2_ADDR												(uint8_t)(0x01)

#define REG_INTR_RAW_3_ADDR												(uint8_t)(0x02)
#define FIELD_INTR_LO_ALARM_MASK										(uint8_t)(0xf)
#define FIELD_INTR_LO_ALARM_BIT_OFFSET									(uint8_t)(0x0)

#define REG_INTR_RAW_4_ADDR												(uint8_t)(0x03)
#define FIELD_INTR_HI_ALARM_MASK										(uint8_t)(0xf)
#define FIELD_INTR_HI_ALARM_BIT_OFFSET									(uint8_t)(0x0)

#define REG_INTR_ENABLE_1_ADDR											(uint8_t)(0x05)
#define FIELD_INTR_EN_0X0_MASK											(uint8_t)(0xfe)
#define FIELD_INTR_EN_0X0_BIT_OFFSET									(uint8_t)(0x1)

#define REG_INTR_ENABLE_3_ADDR											(uint8_t)(0x07)
#define FIELD_INTR_EN_0X2_MASK											(uint8_t)(0xf)
#define FIELD_INTR_EN_0X2_BIT_OFFSET									(uint8_t)(0x0)

#define REG_INTR_ENABLE_4_ADDR											(uint8_t)(0x08)
#define FIELD_INTR_EN_0X3_MASK											(uint8_t)(0xf)
#define FIELD_INTR_EN_0X3_BIT_OFFSET									(uint8_t)(0x0)

#define REG_INTR_CFG_ADDR												(uint8_t)(0x0a)
#define FIELD_INTR_PULL_UP_MASK											(uint8_t)(0x4)
#define FIELD_INTR_PULL_UP_BIT_OFFSET									(uint8_t)(0x2)
#define FIELD_INTR_OPEN_DRAIN_MASK										(uint8_t)(0x2)
#define FIELD_INTR_OPEN_DRAIN_BIT_OFFSET								(uint8_t)(0x1)
#define FIELD_INTR_ACTIVE_LEVEL_MASK									(uint8_t)(0x1)
#define FIELD_INTR_ACTIVE_LEVEL_BIT_OFFSET								(uint8_t)(0x0)

#define REG_FIFO_STAT_0_ADDR											(uint8_t)(0x10)

#define REG_FIFO_STAT_1_ADDR											(uint8_t)(0x11)
#define FIELD_FIFO_DATA_COUNT_MASK										(uint8_t)(0x7f)
#define FIELD_FIFO_DATA_COUNT_BIT_OFFSET								(uint8_t)(0x0)

#define REG_FIFO_STAT_2_ADDR											(uint8_t)(0x12)
#define FIELD_FIFO_WR_PTR_MASK											(uint8_t)(0x3f)
#define FIELD_FIFO_WR_PTR_BIT_OFFSET									(uint8_t)(0x0)

#define REG_FIFO_STAT_3_ADDR											(uint8_t)(0x13)
#define FIELD_FIFO_RD_PTR_MASK											(uint8_t)(0x3f)
#define FIELD_FIFO_RD_PTR_BIT_OFFSET									(uint8_t)(0x0)

#define REG_FIFO_DATA_ADDR												(uint8_t)(0x14)
#define FIELD_FIFO_DATA_MASK											(uint8_t)(0xff)
#define FIELD_FIFO_DATA_BIT_OFFSET										(uint8_t)(0x0)

#define REG_FIFO_CFG_1_ADDR												(uint8_t)(0x15)

#define REG_FIFO_CFG_2_ADDR												(uint8_t)(0x16)
#define FIELD_FIFO_AFULL_THRE_MASK										(uint8_t)(0x3f)
#define FIELD_FIFO_AFULL_THRE_BIT_OFFSET								(uint8_t)(0x0)

#define REG_FIFO_CFG_3_ADDR												(uint8_t)(0x17)
#define FIELD_FIFO_FLUSH_MASK											(uint8_t)(0x10)
#define FIELD_FIFO_FLUSH_BIT_OFFSET										(uint8_t)(0x4)
#define FIELD_FIFO_STAT_CLR_MASK										(uint8_t)(0x8)
#define FIELD_FIFO_STAT_CLR_BIT_OFFSET									(uint8_t)(0x3)
#define FIELD_FIFO_AFULL_TYPE_MASK										(uint8_t)(0x4)
#define FIELD_FIFO_AFULL_TYPE_BIT_OFFSET								(uint8_t)(0x2)

#define REG_SYS_CFG_1_ADDR												(uint8_t)(0x18)
#define FIELD_SOFT_RESET_MASK											(uint8_t)(0x1)
#define FIELD_SOFT_RESET_BIT_OFFSET										(uint8_t)(0x0)

#define REG_SYS_CFG_2_ADDR												(uint8_t)(0x19)
#define FIELD_CHA_ADC_BUF_EMPOWER_MASK									(uint8_t)(0x40)
#define FIELD_CHA_ADC_BUF_EMPOWER_BIT_OFFSET							(uint8_t)(0x6)
#define FIELD_OSC_4M_MANUAL_PD_MASK										(uint8_t)(0x8)
#define FIELD_OSC_4M_MANUAL_PD_BIT_OFFSET								(uint8_t)(0x3)
#define FIELD_OSC_4M_AUTO_PD_MASK										(uint8_t)(0x4)
#define FIELD_OSC_4M_AUTO_PD_BIT_OFFSET									(uint8_t)(0x2)
#define FIELD_OSC_32K_PD_MASK											(uint8_t)(0x2)
#define FIELD_OSC_32K_PD_BIT_OFFSET										(uint8_t)(0x1)

#define REG_SYS_CFG_3_ADDR												(uint8_t)(0x1a)
#define FIELD_ENABLE_4M_DIV_32K_MASK									(uint8_t)(0x1)
#define FIELD_ENABLE_4M_DIV_32K_BIT_OFFSET								(uint8_t)(0x0)

#define REG_SYS_CFG_4_ADDR												(uint8_t)(0x1b)
#define FIELD_PUP_DAT_MASK									            (uint8_t)(0x2)
#define FIELD_PUP_DAT_BIT_OFFSET								        (uint8_t)(0x1)
#define FIELD_PUP_CLK_MASK									            (uint8_t)(0x1)
#define FIELD_PUP_CLK_BIT_OFFSET								        (uint8_t)(0x0)

#define REG_GLB_CFG_1_ADDR												(uint8_t)(0x20)
#define FIELD_FRAME_PERIOD_19_16_MASK									(uint8_t)(0xf)
#define FIELD_FRAME_PERIOD_19_16_BIT_OFFSET								(uint8_t)(0x0)

#define REG_GLB_CFG_2_ADDR												(uint8_t)(0x21)
#define FIELD_FRAME_PERIOD_15_8_MASK									(uint8_t)(0xff)
#define FIELD_FRAME_PERIOD_15_8_BIT_OFFSET								(uint8_t)(0x0)

#define REG_GLB_CFG_3_ADDR												(uint8_t)(0x22)
#define FIELD_FRAME_PERIOD_7_0_MASK										(uint8_t)(0xff)
#define FIELD_FRAME_PERIOD_7_0_BIT_OFFSET								(uint8_t)(0x0)

#define REG_GLB_CFG_4_ADDR												(uint8_t)(0x23)
#define FIELD_PDIO0_PRECON_STATE_MASK									(uint8_t)(0x60)
#define FIELD_PDIO0_PRECON_STATE_BIT_OFFSET								(uint8_t)(0x5)
#define FIELD_PDIO0_SLEEP_STATE_MASK									(uint8_t)(0x18)
#define FIELD_PDIO0_SLEEP_STATE_BIT_OFFSET								(uint8_t)(0x3)
#define FIELD_PDIO0_TYPE_MASK											(uint8_t)(0x1)
#define FIELD_PDIO0_TYPE_BIT_OFFSET										(uint8_t)(0x0)

#define REG_GLB_CFG_5_ADDR												(uint8_t)(0x24)

#define REG_GLB_CFG_6_ADDR												(uint8_t)(0x25)
#define FIELD_PDIO_PRECON_WIDTH_MASK									(uint8_t)(0xf)
#define FIELD_PDIO_PRECON_WIDTH_BIT_OFFSET								(uint8_t)(0x0)

#define REG_GLB_CFG_7_ADDR												(uint8_t)(0x26)
#define FIELD_AACM_FULL_SCALE_MASK										(uint8_t)(0x3)
#define FIELD_AACM_FULL_SCALE_BIT_OFFSET								(uint8_t)(0x0)

#define REG_GLB_CFG_9_ADDR												(uint8_t)(0x28)
#define FIELD_AACM_START_MASK											(uint8_t)(0xff)
#define FIELD_AACM_START_BIT_OFFSET										(uint8_t)(0x0)

#define REG_GLB_CFG_10_ADDR												(uint8_t)(0x29)
#define FIELD_AACM_END_MASK												(uint8_t)(0xff)
#define FIELD_AACM_END_BIT_OFFSET										(uint8_t)(0x0)

#define REG_GLB_CFG_11_ADDR												(uint8_t)(0x2a)
#define FIELD_PMIC_TX_BIT_WIDTH_MASK									(uint8_t)(0xc0)
#define FIELD_PMIC_TX_BIT_WIDTH_BIT_OFFSET								(uint8_t)(0x6)
#define FIELD_PMIC_TX_PREPARE_X4_MASK									(uint8_t)(0x3c)
#define FIELD_PMIC_TX_PREPARE_X4_BIT_OFFSET								(uint8_t)(0x2)
#define FIELD_PMIC_TX_SYNC_IDLE_MASK									(uint8_t)(0x2)
#define FIELD_PMIC_TX_SYNC_IDLE_BIT_OFFSET								(uint8_t)(0x1)
#define FIELD_PMIC_TX_MODE_MASK											(uint8_t)(0x1)
#define FIELD_PMIC_TX_MODE_BIT_OFFSET									(uint8_t)(0x0)

#define REG_GLB_CFG_12_ADDR												(uint8_t)(0x2b)
#define FIELD_PMIC_TX_PULL_UP_MASK										(uint8_t)(0x4)
#define FIELD_PMIC_TX_PULL_UP_BIT_OFFSET								(uint8_t)(0x2)
#define FIELD_PMIC_TX_OPEN_DRAIN_MASK									(uint8_t)(0x2)
#define FIELD_PMIC_TX_OPEN_DRAIN_BIT_OFFSET								(uint8_t)(0x1)
#define FIELD_PMIC_TX_ODD_MASK											(uint8_t)(0x1)
#define FIELD_PMIC_TX_ODD_BIT_OFFSET									(uint8_t)(0x0)

#define REG_GLB_CFG_13_ADDR												(uint8_t)(0x2c)

#define REG_GLB_CFG_16_ADDR												(uint8_t)(0x2f)
#define FIELD_TIMESLOT_ACTIVE_MASK										(uint8_t)(0x10)
#define FIELD_TIMESLOT_ACTIVE_BIT_OFFSET								(uint8_t)(0x4)
#define FIELD_NEXT_REFRESH_MASK											(uint8_t)(0x8)
#define FIELD_NEXT_REFRESH_BIT_OFFSET									(uint8_t)(0x3)
#define FIELD_GPIO_CONVERT_MASK											(uint8_t)(0x4)
#define FIELD_GPIO_CONVERT_BIT_OFFSET									(uint8_t)(0x2)
#define FIELD_ONCE_CONVERT_MASK											(uint8_t)(0x2)
#define FIELD_ONCE_CONVERT_BIT_OFFSET									(uint8_t)(0x1)
#define FIELD_AUTO_CONVERT_MASK											(uint8_t)(0x1)
#define FIELD_AUTO_CONVERT_BIT_OFFSET									(uint8_t)(0x0)

#define REG_TS_CFG_1_ADDR												(uint8_t)(0x30)
#define FIELD_TIMESLOT_CONFIG_LOCK_MASK									(uint8_t)(0x8)
#define FIELD_TIMESLOT_CONFIG_LOCK_BIT_OFFSET							(uint8_t)(0x3)
#define FIELD_TIMESLOT_CONFIG_SELECT_MASK								(uint8_t)(0x3)
#define FIELD_TIMESLOT_CONFIG_SELECT_BIT_OFFSET							(uint8_t)(0x0)

#define REG_TS_CFG_2_ADDR												(uint8_t)(0x31)
#define FIELD_TS_ENABLE_MASK											(uint8_t)(0x80)
#define FIELD_TS_ENABLE_BIT_OFFSET										(uint8_t)(0x7)
#define FIELD_TS_PRECON_EN_MASK											(uint8_t)(0x40)
#define FIELD_TS_PRECON_EN_BIT_OFFSET									(uint8_t)(0x6)
#define FIELD_TS_PMIC_CTRL_EN_MASK										(uint8_t)(0x20)
#define FIELD_TS_PMIC_CTRL_EN_BIT_OFFSET								(uint8_t)(0x5)
#define FIELD_TS_MEASURE_TYPE_MASK										(uint8_t)(0xc)
#define FIELD_TS_MEASURE_TYPE_BIT_OFFSET								(uint8_t)(0x2)

#define REG_TS_CFG_3_ADDR												(uint8_t)(0x32)
#define FIELD_TS_ADC_OSR_MASK											(uint8_t)(0xf)
#define FIELD_TS_ADC_OSR_BIT_OFFSET										(uint8_t)(0x0)

#define REG_TS_CFG_4_ADDR												(uint8_t)(0x33)
#define FIELD_TS_END_PART0_MASK											(uint8_t)(0xc0)
#define FIELD_TS_END_PART0_BIT_OFFSET									(uint8_t)(0x6)
#define FIELD_TS_END_PART1_MASK											(uint8_t)(0x3f)
#define FIELD_TS_END_PART1_BIT_OFFSET									(uint8_t)(0x0)

#define REG_TS_CFG_5_ADDR												(uint8_t)(0x34)
#define FIELD_TS_PMIC_CMD_MASK											(uint8_t)(0xff)
#define FIELD_TS_PMIC_CMD_BIT_OFFSET									(uint8_t)(0x0)

#define REG_TS_CFG_6_ADDR												(uint8_t)(0x35)
#define FIELD_TS_TIA_IDLE_PD_MASK										(uint8_t)(0x40)
#define FIELD_TS_TIA_IDLE_PD_BIT_OFFSET									(uint8_t)(0x6)
#define FIELD_TS_SUB_SAMPLE_MASK										(uint8_t)(0xf)
#define FIELD_TS_SUB_SAMPLE_BIT_OFFSET									(uint8_t)(0x0)

#define REG_TS_CFG_7_ADDR												(uint8_t)(0x36)
#define FIELD_TS_DARK0_START_15_8_MASK									(uint8_t)(0xff)
#define FIELD_TS_DARK0_START_15_8_BIT_OFFSET							(uint8_t)(0x0)

#define REG_TS_CFG_8_ADDR												(uint8_t)(0x37)
#define FIELD_TS_DARK0_START_7_0_MASK									(uint8_t)(0xff)
#define FIELD_TS_DARK0_START_7_0_BIT_OFFSET								(uint8_t)(0x0)

#define REG_TS_CFG_9_ADDR												(uint8_t)(0x38)
#define FIELD_TS_LED_IDAC_START_15_8_MASK								(uint8_t)(0xff)
#define FIELD_TS_LED_IDAC_START_15_8_BIT_OFFSET							(uint8_t)(0x0)

#define REG_TS_CFG_10_ADDR												(uint8_t)(0x39)
#define FIELD_TS_LED_IDAC_START_7_0_MASK								(uint8_t)(0xff)
#define FIELD_TS_LED_IDAC_START_7_0_BIT_OFFSET							(uint8_t)(0x0)

#define REG_TS_CFG_11_ADDR												(uint8_t)(0x3a)
#define FIELD_TS_LED_DRIVER_START_15_8_MASK								(uint8_t)(0xff)
#define FIELD_TS_LED_DRIVER_START_15_8_BIT_OFFSET						(uint8_t)(0x0)

#define REG_TS_CFG_12_ADDR												(uint8_t)(0x3b)
#define FIELD_TS_LED_DRIVER_START_7_0_MASK								(uint8_t)(0xff)
#define FIELD_TS_LED_DRIVER_START_7_0_BIT_OFFSET						(uint8_t)(0x0)

#define REG_TS_CFG_13_ADDR												(uint8_t)(0x3c)
#define FIELD_TS_LIGHT_START_15_8_MASK									(uint8_t)(0xff)
#define FIELD_TS_LIGHT_START_15_8_BIT_OFFSET							(uint8_t)(0x0)

#define REG_TS_CFG_14_ADDR												(uint8_t)(0x3d)
#define FIELD_TS_LIGHT_START_7_0_MASK									(uint8_t)(0xff)
#define FIELD_TS_LIGHT_START_7_0_BIT_OFFSET								(uint8_t)(0x0)

#define REG_TS_CFG_15_ADDR												(uint8_t)(0x3e)
#define FIELD_TS_DARK1_START_15_8_MASK									(uint8_t)(0xff)
#define FIELD_TS_DARK1_START_15_8_BIT_OFFSET							(uint8_t)(0x0)

#define REG_TS_CFG_16_ADDR												(uint8_t)(0x3f)
#define FIELD_TS_DARK1_START_7_0_MASK									(uint8_t)(0xff)
#define FIELD_TS_DARK1_START_7_0_BIT_OFFSET								(uint8_t)(0x0)

#define REG_TS_CFG_17_ADDR												(uint8_t)(0x40)
#define FIELD_TS_LED_A_SELECT_MASK										(uint8_t)(0x7)
#define FIELD_TS_LED_A_SELECT_BIT_OFFSET								(uint8_t)(0x0)

#define REG_TS_CFG_18_ADDR												(uint8_t)(0x41)
#define FIELD_TS_LED_A_CODE_MASK										(uint8_t)(0xff)
#define FIELD_TS_LED_A_CODE_BIT_OFFSET									(uint8_t)(0x0)

#define REG_TS_CFG_19_ADDR												(uint8_t)(0x42)

#define REG_TS_CFG_20_ADDR												(uint8_t)(0x43)

#define REG_TS_CFG_21_ADDR												(uint8_t)(0x44)
#define FIELD_TS_LED_A_FULLSCALE_MASK									(uint8_t)(0x1)
#define FIELD_TS_LED_A_FULLSCALE_BIT_OFFSET								(uint8_t)(0x0)

#define REG_TS_CFG_23_ADDR												(uint8_t)(0x46)
#define FIELD_TS_IIR_RATIO_MASK											(uint8_t)(0x30)
#define FIELD_TS_IIR_RATIO_BIT_OFFSET									(uint8_t)(0x4)
#define FIELD_TS_DCM_RATIO_MASK											(uint8_t)(0xe)
#define FIELD_TS_DCM_RATIO_BIT_OFFSET									(uint8_t)(0x1)
#define FIELD_TS_IIR_EN_MASK											(uint8_t)(0x1)
#define FIELD_TS_IIR_EN_BIT_OFFSET										(uint8_t)(0x0)

#define REG_TS_CFG_24_ADDR												(uint8_t)(0x47)
#define FIELD_TS_LED_STORE_FIFO_MASK									(uint8_t)(0xc0)
#define FIELD_TS_LED_STORE_FIFO_BIT_OFFSET								(uint8_t)(0x6)
#define FIELD_TS_IDAC_STORE_FIFO_MASK									(uint8_t)(0x30)
#define FIELD_TS_IDAC_STORE_FIFO_BIT_OFFSET								(uint8_t)(0x4)
#define FIELD_TS_DARK0_STORE_FIFO_MASK									(uint8_t)(0x8)
#define FIELD_TS_DARK0_STORE_FIFO_BIT_OFFSET							(uint8_t)(0x3)
#define FIELD_TS_LIGHT_STORE_FIFO_MASK									(uint8_t)(0x4)
#define FIELD_TS_LIGHT_STORE_FIFO_BIT_OFFSET							(uint8_t)(0x2)
#define FIELD_TS_DARK1_STORE_FIFO_MASK									(uint8_t)(0x2)
#define FIELD_TS_DARK1_STORE_FIFO_BIT_OFFSET							(uint8_t)(0x1)
#define FIELD_TS_SIGNAL_STORE_FIFO_MASK									(uint8_t)(0x1)
#define FIELD_TS_SIGNAL_STORE_FIFO_BIT_OFFSET							(uint8_t)(0x0)

#define REG_TS_CFG_25_ADDR												(uint8_t)(0x48)
#define FIELD_TS_SATU_STORE_FIFO_MASK									(uint8_t)(0x4)
#define FIELD_TS_SATU_STORE_FIFO_BIT_OFFSET								(uint8_t)(0x2)
#define FIELD_TS_SATU_CODE_MASK											(uint8_t)(0x3)
#define FIELD_TS_SATU_CODE_BIT_OFFSET									(uint8_t)(0x0)

#define REG_CH_CFG_1_ADDR												(uint8_t)(0x50)
#define FIELD_TS_CHA_AACM_EN_MASK										(uint8_t)(0x8)
#define FIELD_TS_CHA_AACM_EN_BIT_OFFSET									(uint8_t)(0x3)
#define FIELD_TS_CHA_PDIO_SEL_MASK										(uint8_t)(0x1)
#define FIELD_TS_CHA_PDIO_SEL_BIT_OFFSET								(uint8_t)(0x0)

#define REG_CH_CFG_2_ADDR												(uint8_t)(0x51)
#define FIELD_TS_CHA_TIA_CF_MASK										(uint8_t)(0x18)
#define FIELD_TS_CHA_TIA_CF_BIT_OFFSET									(uint8_t)(0x3)
#define FIELD_TS_CHA_TIA_RF_MASK										(uint8_t)(0x7)
#define FIELD_TS_CHA_TIA_RF_BIT_OFFSET									(uint8_t)(0x0)

#define REG_CH_CFG_5_ADDR												(uint8_t)(0x54)
#define FIELD_TS_CHA_LED_IDAC_CODE_MASK									(uint8_t)(0xff)
#define FIELD_TS_CHA_LED_IDAC_CODE_BIT_OFFSET							(uint8_t)(0x0)

#define REG_CH_CFG_9_ADDR												(uint8_t)(0x58)
#define FIELD_TS_CHA_AMB_IDAC_CODE_MASK									(uint8_t)(0xff)
#define FIELD_TS_CHA_AMB_IDAC_CODE_BIT_OFFSET							(uint8_t)(0x0)

#define REG_ALARM_CFG_1_ADDR											(uint8_t)(0x60)
#define FIELD_TS_ALARM_CHA_EN_MASK										(uint8_t)(0x10)
#define FIELD_TS_ALARM_CHA_EN_BIT_OFFSET								(uint8_t)(0x4)
#define FIELD_TS_ALARM_DATA_FIL_MASK									(uint8_t)(0x4)
#define FIELD_TS_ALARM_DATA_FIL_BIT_OFFSET								(uint8_t)(0x2)
#define FIELD_TS_ALARM_DATA_SEL_MASK									(uint8_t)(0x3)
#define FIELD_TS_ALARM_DATA_SEL_BIT_OFFSET								(uint8_t)(0x0)

#define REG_ALARM_CFG_2_ADDR											(uint8_t)(0x61)
#define FIELD_TS_ALARM_INTR_MASK										(uint8_t)(0x20)
#define FIELD_TS_ALARM_INTR_BIT_OFFSET									(uint8_t)(0x5)
#define FIELD_TS_ALARM_ABS_MODE_MASK									(uint8_t)(0x10)
#define FIELD_TS_ALARM_ABS_MODE_BIT_OFFSET								(uint8_t)(0x4)
#define FIELD_TS_ALARM_TRIP_CNT_MASK									(uint8_t)(0xf)
#define FIELD_TS_ALARM_TRIP_CNT_BIT_OFFSET								(uint8_t)(0x0)

#define REG_ALARM_CFG_3_ADDR											(uint8_t)(0x62)
#define FIELD_TS_ALARM_ALCM_LED_A_MASK									(uint8_t)(0x8)
#define FIELD_TS_ALARM_ALCM_LED_A_BIT_OFFSET							(uint8_t)(0x3)
#define FIELD_TS_ALARM_ALCM_STEP_MASK									(uint8_t)(0x3)
#define FIELD_TS_ALARM_ALCM_STEP_BIT_OFFSET								(uint8_t)(0x0)

#define REG_ALARM_CFG_4_ADDR											(uint8_t)(0x63)
#define FIELD_TS_LO_THRESH_9_8_MASK										(uint8_t)(0x3)
#define FIELD_TS_LO_THRESH_9_8_BIT_OFFSET								(uint8_t)(0x0)

#define REG_ALARM_CFG_5_ADDR											(uint8_t)(0x64)
#define FIELD_TS_LO_THRESH_7_0_MASK										(uint8_t)(0xff)
#define FIELD_TS_LO_THRESH_7_0_BIT_OFFSET								(uint8_t)(0x0)

#define REG_ALARM_CFG_6_ADDR											(uint8_t)(0x65)
#define FIELD_TS_HI_THRESH_9_8_MASK										(uint8_t)(0x3)
#define FIELD_TS_HI_THRESH_9_8_BIT_OFFSET								(uint8_t)(0x0)

#define REG_ALARM_CFG_7_ADDR											(uint8_t)(0x66)
#define FIELD_TS_HI_THRESH_7_0_MASK										(uint8_t)(0xff)
#define FIELD_TS_HI_THRESH_7_0_BIT_OFFSET								(uint8_t)(0x0)

#define REG_ALARM_CFG_8_ADDR											(uint8_t)(0x67)
#define FIELD_TS_ALCM_HI_LIMIT_MASK										(uint8_t)(0xff)
#define FIELD_TS_ALCM_HI_LIMIT_BIT_OFFSET								(uint8_t)(0x0)

#define REG_ALARM_CFG_9_ADDR											(uint8_t)(0x68)
#define FIELD_TS_ALCM_LO_LIMIT_MASK										(uint8_t)(0xff)
#define FIELD_TS_ALCM_LO_LIMIT_BIT_OFFSET								(uint8_t)(0x0)

#define REG_GPIO1_CFG_1_ADDR											(uint8_t)(0x80)
#define FIELD_GPIO1_FUNC_SEL_MASK										(uint8_t)(0xf)
#define FIELD_GPIO1_FUNC_SEL_BIT_OFFSET									(uint8_t)(0x0)

#define REG_GPIO1_CFG_2_ADDR											(uint8_t)(0x81)
#define FIELD_GPIO1_INPUT_PE_MASK										(uint8_t)(0x10)
#define FIELD_GPIO1_INPUT_PE_BIT_OFFSET									(uint8_t)(0x4)
#define FIELD_GPIO1_CLK_OUT_DIV_MASK									(uint8_t)(0xf)
#define FIELD_GPIO1_CLK_OUT_DIV_BIT_OFFSET								(uint8_t)(0x0)

#define REG_GPIO1_CFG_3_ADDR											(uint8_t)(0x82)
#define FIELD_GPIO1_TS_ALL_MASK											(uint8_t)(0x8)
#define FIELD_GPIO1_TS_ALL_BIT_OFFSET									(uint8_t)(0x3)
#define FIELD_GPIO1_TS_SEL_MASK											(uint8_t)(0x3)
#define FIELD_GPIO1_TS_SEL_BIT_OFFSET									(uint8_t)(0x0)

#define REG_GPIO1_CFG_4_ADDR											(uint8_t)(0x83)
#define FIELD_GPIO1_TS_LED_ON_MASK										(uint8_t)(0x80)
#define FIELD_GPIO1_TS_LED_ON_BIT_OFFSET								(uint8_t)(0x7)
#define FIELD_GPIO1_TS_DARK1_MASK										(uint8_t)(0x40)
#define FIELD_GPIO1_TS_DARK1_BIT_OFFSET									(uint8_t)(0x6)
#define FIELD_GPIO1_TS_LIGHT_MASK										(uint8_t)(0x20)
#define FIELD_GPIO1_TS_LIGHT_BIT_OFFSET									(uint8_t)(0x5)
#define FIELD_GPIO1_TS_DARK0_MASK										(uint8_t)(0x10)
#define FIELD_GPIO1_TS_DARK0_BIT_OFFSET									(uint8_t)(0x4)
#define FIELD_GPIO1_TS_AACM_MASK										(uint8_t)(0x8)
#define FIELD_GPIO1_TS_AACM_BIT_OFFSET									(uint8_t)(0x3)
#define FIELD_GPIO1_TS_PMIC_MASK										(uint8_t)(0x4)
#define FIELD_GPIO1_TS_PMIC_BIT_OFFSET									(uint8_t)(0x2)
#define FIELD_GPIO1_TS_DURATION_MASK									(uint8_t)(0x2)
#define FIELD_GPIO1_TS_DURATION_BIT_OFFSET								(uint8_t)(0x1)
#define FIELD_GPIO1_TS_ACTIVE_MASK										(uint8_t)(0x1)
#define FIELD_GPIO1_TS_ACTIVE_BIT_OFFSET								(uint8_t)(0x0)

#define REG_GPIO1_CFG_5_ADDR											(uint8_t)(0x84)
#define FIELD_GPIO1_SYSDBG_SEL_MASK										(uint8_t)(0xf)
#define FIELD_GPIO1_SYSDBG_SEL_BIT_OFFSET								(uint8_t)(0x0)

#define REG_GPIO1_CFG_6_ADDR											(uint8_t)(0x85)
#define FIELD_GPIO1_RPT_MASK											(uint8_t)(0x1)
#define FIELD_GPIO1_RPT_BIT_OFFSET										(uint8_t)(0x0)

#define REG_PAD_CFG_1_ADDR												(uint8_t)(0x90)
#define FIELD_IO_SDA_PE_MASK											(uint8_t)(0x2)
#define FIELD_IO_SDA_PE_BIT_OFFSET										(uint8_t)(0x1)
#define FIELD_IO_SCL_PE_MASK											(uint8_t)(0x1)
#define FIELD_IO_SCL_PE_BIT_OFFSET										(uint8_t)(0x0)

#define REG_PAD_CFG_2_ADDR												(uint8_t)(0x91)
#define FIELD_I2C_DEADLOCK_MON_EN_MASK									(uint8_t)(0x1)
#define FIELD_I2C_DEADLOCK_MON_EN_BIT_OFFSET							(uint8_t)(0x0)

#define REG_PAD_CFG_3_ADDR												(uint8_t)(0x92)
#define FIELD_I2C_DEADLOCK_MON_TIME_15_8_MASK							(uint8_t)(0xff)
#define FIELD_I2C_DEADLOCK_MON_TIME_15_8_BIT_OFFSET						(uint8_t)(0x0)

#define REG_PAD_CFG_4_ADDR												(uint8_t)(0x93)
#define FIELD_I2C_DEADLOCK_MON_TIME_7_0_MASK							(uint8_t)(0xff)
#define FIELD_I2C_DEADLOCK_MON_TIME_7_0_BIT_OFFSET						(uint8_t)(0x0)

#define REG_ECC_RPT_1_ADDR												(uint8_t)(0xa0)
#define FIELD_ECC_ERR_DET_MASK											(uint8_t)(0x8)
#define FIELD_ECC_ERR_DET_BIT_OFFSET									(uint8_t)(0x3)
#define FIELD_ECC_ERR_MUL_MASK											(uint8_t)(0x4)
#define FIELD_ECC_ERR_MUL_BIT_OFFSET									(uint8_t)(0x2)

#define REG_CHIP_ID_ADDR												(uint8_t)(0xff)
#define FIELD_CHIP_ID_MASK												(uint8_t)(0xff)
#define FIELD_CHIP_ID_BIT_OFFSET										(uint8_t)(0x0)

#endif /* VT2102_REG_H__ */
