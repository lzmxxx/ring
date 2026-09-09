#include <stddef.h>
#include "vt2102.h"
#include "vt2102_reg.h"

#define VT2102_Int_WriteFieldInVariable(var,field,val)              {var &= ~(MASK(field)); var |= ((val) << BIT_OFFSET(field));}
#define VT2102_Int_ReadFieldInVariable(var,field)                   ((var & MASK(field)) >> BIT_OFFSET(field))

VT2102_FUNC_DECLARE void VT2102_ReadAllInterruptFlags( uint8_t *pu8_buff )
{
    VT2102_ASSERT(pu8_buff != NULL);
	VT2102_ReadRegisters(REG_INTR_RAW_1_ADDR, pu8_buff, 4);
}

VT2102_FUNC_DECLARE void VT2102_SetInterruptEnabled( VT2102IntSource int_src, bool b_enabled )
{
    uint8_t u8_reg_val, u8_reg_addr;
    uint8_t u8_bit_offset;

    u8_reg_addr = REG_INTR_ENABLE_1_ADDR + (((uint16_t)int_src) >> 8);
    u8_bit_offset = (uint8_t)int_src & 0xFFU;
    if (u8_bit_offset >= 8U)
    {
        return;
    }
    VT2102_ReadRegisters(u8_reg_addr, &u8_reg_val, 0x01);

    if (b_enabled == true)
    {
        u8_reg_val |= (uint8_t)(0x01U << u8_bit_offset);
    }
    else
    {
        u8_reg_val &= (uint8_t)~(0x01U << u8_bit_offset);
    }

    VT2102_WriteRegisters(u8_reg_addr, &u8_reg_val, 0x01);
}

#define FIELD_OSC_4M_PD_MASK            (FIELD_OSC_4M_MANUAL_PD_MASK | FIELD_OSC_4M_AUTO_PD_MASK)
#define FIELD_OSC_4M_PD_BIT_OFFSET      (FIELD_OSC_4M_AUTO_PD_BIT_OFFSET)

VT2102_FUNC_DECLARE void VT2102_ConfigOnChipOscillator( VT2102Osc4MPDMode osc_4m_pd_mode, VT2102Osc32kPDMode osc_32k_pd_mode )
{
    VT2102_ASSERT(osc_4m_pd_mode < VT2102_OSC_4M_PD_MODE_NUM);
    VT2102_ASSERT(osc_32k_pd_mode < VT2102_OSC_32K_PD_MODE_NUM);

    uint8_t u8_reg_val;
    VT2102_ReadRegisters(REG_SYS_CFG_2_ADDR, &u8_reg_val, 0x01);

    VT2102_Int_WriteFieldInVariable(u8_reg_val, FIELD_OSC_4M_PD, ((uint8_t)osc_4m_pd_mode));
    VT2102_Int_WriteFieldInVariable(u8_reg_val, FIELD_OSC_32K_PD, ((uint8_t)osc_32k_pd_mode));

    VT2102_WriteRegisters(REG_SYS_CFG_2_ADDR, &u8_reg_val, 0x01);
}

VT2102_FUNC_DECLARE void VT2102_GetOnChipOscillatorConfig( VT2102Osc4MPDMode *p_osc_4m_pd_mode, VT2102Osc32kPDMode *p_osc_32k_pd_mode )
{
    VT2102_ASSERT(p_osc_4m_pd_mode != NULL);
    VT2102_ASSERT(p_osc_32k_pd_mode != NULL);

    uint8_t u8_reg_val;
    VT2102_ReadRegisters(REG_SYS_CFG_2_ADDR, &u8_reg_val, 0x01);

    *p_osc_4m_pd_mode =  (VT2102Osc4MPDMode)(VT2102_Int_ReadFieldInVariable(u8_reg_val, FIELD_OSC_4M_PD));
    *p_osc_32k_pd_mode = (VT2102Osc32kPDMode)(VT2102_Int_ReadFieldInVariable(u8_reg_val, FIELD_OSC_32K_PD));
}

VT2102_FUNC_DECLARE void VT2102_EnterLowPowerMode( void )
{
    uint8_t u8_reg_val = 0x00;

    VT2102_Int_WriteFieldInVariable(u8_reg_val, FIELD_PUP_DAT, 1);
    VT2102_WriteRegisters(REG_SYS_CFG_4_ADDR, &u8_reg_val, 0x01);
    VT2102_Int_WriteFieldInVariable(u8_reg_val, FIELD_PUP_CLK, 1);
    VT2102_WriteRegisters(REG_SYS_CFG_4_ADDR, &u8_reg_val, 0x01);
    VT2102_Int_WriteFieldInVariable(u8_reg_val, FIELD_PUP_CLK, 0);
    VT2102_WriteRegisters(REG_SYS_CFG_4_ADDR, &u8_reg_val, 0x01);
    VT2102_Int_WriteFieldInVariable(u8_reg_val, FIELD_PUP_CLK, 1);
    VT2102_WriteRegisters(REG_SYS_CFG_4_ADDR, &u8_reg_val, 0x01);
    VT2102_Int_WriteFieldInVariable(u8_reg_val, FIELD_PUP_CLK, 0);
    VT2102_WriteRegisters(REG_SYS_CFG_4_ADDR, &u8_reg_val, 0x01);
}

VT2102_FUNC_DECLARE void VT2102_ReadFIFO( VT2102FIFOData *p_data, uint32_t u32_data_cnt )
{
    VT2102_ASSERT(p_data != NULL);
    VT2102_ASSERT(u32_data_cnt > 0 && u32_data_cnt <= ((uint32_t)0xFFFFFFFF >> 2));

    VT2102_ReadRegisters(REG_FIFO_DATA_ADDR, (uint8_t*)&p_data->pu8_buff[0], (u32_data_cnt << 1) + u32_data_cnt);
}

VT2102_FUNC_DECLARE void VT2102_ConfigFIFOAFull( uint8_t u8_afull_thres, VT2102FIFOAlmostFullType type )
{
    VT2102_ASSERT(u8_afull_thres < 64);
    VT2102_ASSERT(type < VT2102_FIFO_AF_TYPE_NUM);

    uint8_t pu8_regs_val[2];
    VT2102_ReadRegisters(REG_FIFO_CFG_2_ADDR, &pu8_regs_val[0], 0x02);

    pu8_regs_val[0] = u8_afull_thres;
    VT2102_Int_WriteFieldInVariable(pu8_regs_val[1], FIELD_FIFO_AFULL_TYPE, (uint8_t)type);
    VT2102_WriteRegisters(REG_FIFO_CFG_2_ADDR, &pu8_regs_val[0], 0x02);
}

VT2102_FUNC_DECLARE void VT2102_GetFIFOPointers( uint8_t *pu8_write_ptr, uint8_t *pu8_read_ptr )
{
    uint8_t u8_regs_val[2];

    VT2102_ReadRegisters(REG_FIFO_STAT_2_ADDR, &u8_regs_val[0], 0x02);
    if (pu8_write_ptr != NULL)
    {
        *pu8_write_ptr = u8_regs_val[0] & FIELD_FIFO_WR_PTR_MASK;
    }
    if (pu8_read_ptr != NULL)
    {
        *pu8_read_ptr = u8_regs_val[1] & FIELD_FIFO_RD_PTR_MASK;
    }
}

VT2102_FUNC_DECLARE uint8_t VT2102_GetFIFOCount( void )
{
    uint8_t u8_reg_val;
    VT2102_ReadRegisters(REG_FIFO_STAT_1_ADDR, &u8_reg_val, 0x01);
    return (u8_reg_val & FIELD_FIFO_DATA_COUNT_MASK);
}

VT2102_FUNC_DECLARE void VT2102_ClearFIFO( void )
{
    uint8_t u8_reg_val = 0x00;
    VT2102_ReadRegisters(REG_FIFO_CFG_3_ADDR, &u8_reg_val, 0x01);
    VT2102_Int_WriteFieldInVariable(u8_reg_val, FIELD_FIFO_FLUSH, 0x01);
    VT2102_WriteRegisters(REG_FIFO_CFG_3_ADDR, &u8_reg_val, 0x01);
}

VT2102_FUNC_DECLARE void VT2102_SetFramePeriod( uint32_t u32_period )
{
    VT2102_ASSERT(u32_period > 0);
    uint8_t pu8_period_buff[] = {(uint8_t)(u32_period >> 16), (uint8_t)((u32_period >> 8) & 0xFF), (uint8_t)(u32_period & 0xFF)};
    VT2102_WriteRegisters(REG_GLB_CFG_1_ADDR, &pu8_period_buff[0], 0x03);
}

VT2102_FUNC_DECLARE uint32_t VT2102_GetFramePeriod( void )
{
    uint32_t u32_period;
    uint8_t pu8_period_buff[3];
    VT2102_ReadRegisters(REG_GLB_CFG_1_ADDR, &pu8_period_buff[0], 0x03);
    u32_period = (pu8_period_buff[0] << 16) + (pu8_period_buff[1] << 8) + pu8_period_buff[2];
    return u32_period;
}

VT2102_FUNC_DECLARE void VT2102_ConfigPDIO( const PDIOConfigItems *p_items )
{
    VT2102_ASSERT(p_items != NULL);
    VT2102_ASSERT((uint8_t)p_items->precon_state < (uint8_t)PDIO_STATE_NUM);
    VT2102_ASSERT((uint8_t)p_items->sleep_state < (uint8_t)PDIO_STATE_NUM);
    VT2102_ASSERT((uint8_t)p_items->type < (uint8_t)PDIO_TYPE_NUM);
    VT2102_ASSERT(p_items->u8_precon_width_us < 16);

    uint8_t pu8_regs_val[3] = {0x00};
    VT2102_Int_WriteFieldInVariable(pu8_regs_val[0], FIELD_PDIO0_PRECON_STATE, p_items->precon_state);
    VT2102_Int_WriteFieldInVariable(pu8_regs_val[0], FIELD_PDIO0_SLEEP_STATE, p_items->sleep_state);
    VT2102_Int_WriteFieldInVariable(pu8_regs_val[0], FIELD_PDIO0_TYPE, p_items->type);
    VT2102_Int_WriteFieldInVariable(pu8_regs_val[2], FIELD_PDIO_PRECON_WIDTH, p_items->u8_precon_width_us);
    
    VT2102_WriteRegisters(REG_GLB_CFG_4_ADDR, &pu8_regs_val[0], 0x03);
}

VT2102_FUNC_DECLARE void VT2102_GetPDIOConfig( PDIOConfigItems *p_items )
{
    VT2102_ASSERT(p_items != NULL);
    uint8_t pu8_regs_val[3];
    VT2102_ReadRegisters(REG_GLB_CFG_4_ADDR, &pu8_regs_val[0], 0x03);

    p_items->precon_state       = (PDIOState)VT2102_Int_ReadFieldInVariable(pu8_regs_val[0], FIELD_PDIO0_PRECON_STATE);
    p_items->sleep_state        = (PDIOState)VT2102_Int_ReadFieldInVariable(pu8_regs_val[0], FIELD_PDIO0_SLEEP_STATE);
    p_items->type               = (PDIOType)VT2102_Int_ReadFieldInVariable(pu8_regs_val[0], FIELD_PDIO0_TYPE);
    p_items->u8_precon_width_us = VT2102_Int_ReadFieldInVariable(pu8_regs_val[2], FIELD_PDIO_PRECON_WIDTH);
}

VT2102_FUNC_DECLARE void VT2102_SetADCBufferEmpowerMode( ADCBufferEMPowerMode mode )
{
    VT2102_ASSERT(mode == ADC_BUF_EMPOWER_LOW_POWER_MODE || mode == ADC_BUF_EMPOWER_HIGH_PERF_MODE );
    uint8_t u8_reg_val;
    VT2102_ReadRegisters(REG_SYS_CFG_2_ADDR, &u8_reg_val, 0x01);
    VT2102_Int_WriteFieldInVariable(u8_reg_val, FIELD_CHA_ADC_BUF_EMPOWER, ((uint8_t)mode));
    VT2102_WriteRegisters(REG_SYS_CFG_2_ADDR, &u8_reg_val, 0x01);
}

VT2102_FUNC_DECLARE ADCBufferEMPowerMode VT2102_GetADCBufferEmpowerMode( void )
{
    uint8_t u8_reg_val;
    VT2102_ReadRegisters(REG_SYS_CFG_2_ADDR, &u8_reg_val, 0x01);
    return (ADCBufferEMPowerMode)VT2102_Int_ReadFieldInVariable(u8_reg_val, FIELD_CHA_ADC_BUF_EMPOWER);
}

VT2102_FUNC_DECLARE void VT2102_ConfigAACMGlobal( const AACMConfigItems *p_items )
{
    VT2102_ASSERT(p_items != NULL);
    VT2102_ASSERT(p_items->full_scale < AACM_FULL_SCALE_NUM);

    uint8_t pu8_regs_val[4] = {0x00};
    VT2102_Int_WriteFieldInVariable(pu8_regs_val[0], FIELD_AACM_FULL_SCALE, p_items->full_scale);
    pu8_regs_val[2] = p_items->start_time;
    pu8_regs_val[3] = p_items->end_time;
    VT2102_WriteRegisters(REG_GLB_CFG_7_ADDR, &pu8_regs_val[0], 4);
}

VT2102_FUNC_DECLARE void VT2102_GetAACMGlobalConfig( AACMConfigItems *p_items )
{
    VT2102_ASSERT(p_items != NULL);
    uint8_t pu8_regs_val[4];
    VT2102_ReadRegisters(REG_GLB_CFG_7_ADDR, &pu8_regs_val[0], 4);

    p_items->full_scale = (AACMFullScale)VT2102_Int_ReadFieldInVariable(pu8_regs_val[0], FIELD_AACM_FULL_SCALE);
    p_items->start_time = pu8_regs_val[2];
    p_items->end_time   = pu8_regs_val[3];
}

VT2102_FUNC_DECLARE void VT2102_ConfigPMIC( const PMICConfigItems *p_items )
{
    VT2102_ASSERT(p_items != NULL);
    VT2102_ASSERT(p_items->tx_mode < PMIC_TX_MODE_NUM);
    VT2102_ASSERT(p_items->tx_odd < PMIC_TX_PARITY_NUM );
    VT2102_ASSERT(p_items->tx_out_mode < PMIC_TX_OUT_NUM );
    VT2102_ASSERT(p_items->tx_prep_x4 <= MAX_PMIC_TX_PREP_X4_VALUE );
    VT2102_ASSERT(p_items->tx_bit_width < PMIC_TX_BIT_WIDTH_NUM );

    uint8_t pu8_buff[2] = {0x00};
    pu8_buff[0] = p_items->tx_prep_x4;
    if (p_items->b_tx_sync_idle == true)
    {
        VT2102_Int_WriteFieldInVariable(pu8_buff[0], FIELD_PMIC_TX_SYNC_IDLE, 0x01);
    }
    VT2102_Int_WriteFieldInVariable(pu8_buff[0], FIELD_PMIC_TX_BIT_WIDTH, p_items->tx_bit_width);
    VT2102_Int_WriteFieldInVariable(pu8_buff[0], FIELD_PMIC_TX_MODE, p_items->tx_mode);
    VT2102_Int_WriteFieldInVariable(pu8_buff[1], FIELD_PMIC_TX_OPEN_DRAIN, p_items->tx_out_mode & 0x01);
    VT2102_Int_WriteFieldInVariable(pu8_buff[1], FIELD_PMIC_TX_PULL_UP, (p_items->tx_out_mode >> 1));
    VT2102_Int_WriteFieldInVariable(pu8_buff[1], FIELD_PMIC_TX_ODD, p_items->tx_odd);
    VT2102_WriteRegisters(REG_GLB_CFG_11_ADDR, &pu8_buff[0], 2);
}

VT2102_FUNC_DECLARE void VT2102_GetPMICConfig( PMICConfigItems *p_items )
{
    VT2102_ASSERT(p_items != NULL);
    uint8_t pu8_buff[2];
    VT2102_ReadRegisters(REG_GLB_CFG_11_ADDR, &pu8_buff[0], 2);

    p_items->tx_prep_x4     = VT2102_Int_ReadFieldInVariable(pu8_buff[0], FIELD_PMIC_TX_PREPARE_X4);
    p_items->b_tx_sync_idle = (VT2102_Int_ReadFieldInVariable(pu8_buff[0], FIELD_PMIC_TX_SYNC_IDLE) == 0x01) ? true: false;
    p_items->tx_bit_width   = (PMICTxBitWidth)VT2102_Int_ReadFieldInVariable(pu8_buff[0], FIELD_PMIC_TX_BIT_WIDTH);
    p_items->tx_mode        = (PMICTxMode)VT2102_Int_ReadFieldInVariable(pu8_buff[0], FIELD_PMIC_TX_MODE);
    p_items->tx_out_mode    = (PMICTxOutMode)((VT2102_Int_ReadFieldInVariable(pu8_buff[1], FIELD_PMIC_TX_PULL_UP) << 1) | VT2102_Int_ReadFieldInVariable(pu8_buff[1], FIELD_PMIC_TX_OPEN_DRAIN));
    p_items->tx_odd         = (PMICTxParity)VT2102_Int_ReadFieldInVariable(pu8_buff[1], FIELD_PMIC_TX_ODD);
}

VT2102_FUNC_DECLARE void VT2102_ConfigTimeSlot( TimeSlotIndex idx, const TimeSlotConfigItems *p_items )
{
    VT2102_ASSERT(idx < TS_NUM);
    VT2102_ASSERT(p_items != NULL);
    VT2102_ASSERT(p_items->slot_end_clk_prediv < TS_END_CLK_PREDIV_NUM);
    VT2102_ASSERT(p_items->u8_slot_end_clk_cnt < 64);

    uint8_t pu8_reg_val[2];
    VT2102_ReadRegisters(REG_TS_CFG_1_ADDR, &pu8_reg_val[0], 2);
    
    pu8_reg_val[0] = (uint8_t)idx;
    VT2102_Int_WriteFieldInVariable(pu8_reg_val[1], FIELD_TS_ENABLE, (p_items->enabled == true) ? 0x01 : 0x00);
    VT2102_Int_WriteFieldInVariable(pu8_reg_val[1], FIELD_TS_PRECON_EN, (p_items->precon_enabled == true) ? 0x01 : 0x00);
    VT2102_WriteRegisters(REG_TS_CFG_1_ADDR, &pu8_reg_val[0], 2);

    pu8_reg_val[0] = 0x00;
    VT2102_Int_WriteFieldInVariable(pu8_reg_val[0], FIELD_TS_END_PART0, p_items->slot_end_clk_prediv);
    VT2102_Int_WriteFieldInVariable(pu8_reg_val[0], FIELD_TS_END_PART1, p_items->u8_slot_end_clk_cnt);
    VT2102_WriteRegisters(REG_TS_CFG_4_ADDR, &pu8_reg_val[0], 1);

    VT2102_ReadRegisters(REG_TS_CFG_6_ADDR, &pu8_reg_val[0], 1);
    VT2102_Int_WriteFieldInVariable(pu8_reg_val[0], FIELD_TS_SUB_SAMPLE, p_items->sub_sample_ratio);
    VT2102_WriteRegisters(REG_TS_CFG_6_ADDR, &pu8_reg_val[0], 1);
}

VT2102_FUNC_DECLARE void VT2102_GetTimeSlotConfig( TimeSlotIndex idx, TimeSlotConfigItems *p_items )
{
    VT2102_ASSERT(idx < TS_NUM);
    VT2102_ASSERT(p_items != NULL);

    uint8_t pu8_regs_val[5];
    VT2102_WriteRegisters(REG_TS_CFG_1_ADDR, (uint8_t*)(&idx), 1);
    VT2102_ReadRegisters(REG_TS_CFG_2_ADDR, &pu8_regs_val[0], 5);

    p_items->enabled             = (VT2102_Int_ReadFieldInVariable(pu8_regs_val[0], FIELD_TS_ENABLE) == 0x01) ? true : false;
    p_items->precon_enabled      = (VT2102_Int_ReadFieldInVariable(pu8_regs_val[0], FIELD_TS_PRECON_EN) == 0x01) ? true : false;
    p_items->slot_end_clk_prediv = (TimeSlotEndTimePreDivider)VT2102_Int_ReadFieldInVariable(pu8_regs_val[2], FIELD_TS_END_PART0);
    p_items->u8_slot_end_clk_cnt = VT2102_Int_ReadFieldInVariable(pu8_regs_val[2], FIELD_TS_END_PART1);
    p_items->sub_sample_ratio    = (TimeSlotSubSampleRatio)VT2102_Int_ReadFieldInVariable(pu8_regs_val[4], FIELD_TS_SUB_SAMPLE);
}

VT2102_FUNC_DECLARE void VT2102_ConfigTimeSlotMeasure( TimeSlotIndex idx, const TimeSlotMeasureConfigItems *p_items )
{
    VT2102_ASSERT(idx < TS_NUM);
    VT2102_ASSERT(p_items != NULL);
    VT2102_ASSERT(p_items->adc_osr < TS_ADC_OSR_NUM);
    VT2102_ASSERT(p_items->measure_type < TS_MEASURE_TYPE_NUM);

    uint8_t pu8_regs_val[2] = {0x00};
    VT2102_WriteRegisters(REG_TS_CFG_1_ADDR, (uint8_t*)(&idx), 1);
    VT2102_ReadRegisters(REG_TS_CFG_2_ADDR, &pu8_regs_val[0], 2);

    VT2102_Int_WriteFieldInVariable(pu8_regs_val[0], FIELD_TS_MEASURE_TYPE, p_items->measure_type);
    VT2102_Int_WriteFieldInVariable(pu8_regs_val[1], FIELD_TS_ADC_OSR, p_items->adc_osr);
    VT2102_WriteRegisters(REG_TS_CFG_2_ADDR, &pu8_regs_val[0], 2);
}

VT2102_FUNC_DECLARE void VT2102_GetTimeSlotMeasureConfig( TimeSlotIndex idx, TimeSlotMeasureConfigItems *p_items )
{
    VT2102_ASSERT(idx < TS_NUM);
    VT2102_ASSERT(p_items != NULL);

    uint8_t pu8_regs_val[2];
    VT2102_WriteRegisters(REG_TS_CFG_1_ADDR, (uint8_t*)(&idx), 1);
    VT2102_ReadRegisters(REG_TS_CFG_2_ADDR, &pu8_regs_val[0], 2);

    p_items->measure_type = (TimeSlotMeasureType)VT2102_Int_ReadFieldInVariable(pu8_regs_val[0], FIELD_TS_MEASURE_TYPE);
    p_items->adc_osr      = (TimeSlotADCOverSampling)VT2102_Int_ReadFieldInVariable(pu8_regs_val[1], FIELD_TS_ADC_OSR);
}

VT2102_FUNC_DECLARE void VT2102_ConfigTimeSlotPMIC( TimeSlotIndex idx, const TimeSlotPMICConfigItems *p_items )
{
    VT2102_ASSERT(idx < TS_NUM);
    VT2102_ASSERT(p_items != NULL);

    uint8_t u8_reg_val = 0x00;
    VT2102_WriteRegisters(REG_TS_CFG_1_ADDR, (uint8_t*)(&idx), 1);
    VT2102_ReadRegisters(REG_TS_CFG_2_ADDR, &u8_reg_val, 0x01);
    if (p_items->pmic_ctrl_enabled == true)
    {
        VT2102_Int_WriteFieldInVariable(u8_reg_val, FIELD_TS_PMIC_CTRL_EN, 0x01);
    }
    VT2102_WriteRegisters(REG_TS_CFG_2_ADDR, &u8_reg_val, 0x01);
    VT2102_WriteRegisters(REG_TS_CFG_5_ADDR, &p_items->pmic_cmd, 0x01);
}

VT2102_FUNC_DECLARE void VT2102_GetTimeSlotPMICConfig( TimeSlotIndex idx, TimeSlotPMICConfigItems *p_items )
{
    VT2102_ASSERT(idx < TS_NUM);
    VT2102_ASSERT(p_items != NULL);

    uint8_t u8_reg_val = 0x00;
    VT2102_WriteRegisters(REG_TS_CFG_1_ADDR, (uint8_t*)(&idx), 1);
    VT2102_ReadRegisters(REG_TS_CFG_2_ADDR, &u8_reg_val, 0x01);
    p_items->pmic_ctrl_enabled = (VT2102_Int_ReadFieldInVariable(u8_reg_val, FIELD_TS_PMIC_CTRL_EN) == 0x01) ? true : false;
    VT2102_ReadRegisters(REG_TS_CFG_5_ADDR, &p_items->pmic_cmd, 0x01);
}

VT2102_FUNC_DECLARE void VT2102_ConfigTimeSlotTiming( TimeSlotIndex idx, const TimeSlotTimingConfigItems *p_items )
{
    VT2102_ASSERT(idx < TS_NUM);
    VT2102_ASSERT(p_items != NULL);

    uint8_t pu8_regs_val[] = {
        (uint8_t)(p_items->u16_dark0_start_time >> 8), (uint8_t)(p_items->u16_dark0_start_time & 0xFF),
        (uint8_t)(p_items->u16_led_idac_start_time >> 8), (uint8_t)(p_items->u16_led_idac_start_time & 0xFF),
        (uint8_t)(p_items->u16_led_drive_start_time >> 8), (uint8_t)(p_items->u16_led_drive_start_time & 0xFF),
        (uint8_t)(p_items->u16_light_start_time >> 8), (uint8_t)(p_items->u16_light_start_time & 0xFF),
        (uint8_t)(p_items->u16_dark1_start_time >> 8), (uint8_t)(p_items->u16_dark1_start_time & 0xFF)
    };

    VT2102_WriteRegisters(REG_TS_CFG_1_ADDR, (uint8_t*)(&idx), 1);
    VT2102_WriteRegisters(REG_TS_CFG_7_ADDR, &pu8_regs_val[0], 10);
}

VT2102_FUNC_DECLARE void VT2102_GetTimeSlotTimingConfig( TimeSlotIndex idx, TimeSlotTimingConfigItems *p_items )
{
    VT2102_ASSERT(idx < TS_NUM);
    VT2102_ASSERT(p_items != NULL);

    uint8_t pu8_regs_val[10];
    VT2102_WriteRegisters(REG_TS_CFG_1_ADDR, (uint8_t*)(&idx), 1);
    VT2102_ReadRegisters(REG_TS_CFG_7_ADDR, &pu8_regs_val[0], 10);

    p_items->u16_dark0_start_time     = (pu8_regs_val[0x00] << 8) + pu8_regs_val[0x01];
    p_items->u16_led_idac_start_time  = (pu8_regs_val[0x02] << 8) + pu8_regs_val[0x03];
    p_items->u16_led_drive_start_time = (pu8_regs_val[0x04] << 8) + pu8_regs_val[0x05];
    p_items->u16_light_start_time     = (pu8_regs_val[0x06] << 8) + pu8_regs_val[0x07];
    p_items->u16_dark1_start_time     = (pu8_regs_val[0x08] << 8) + pu8_regs_val[0x09];
}

VT2102_FUNC_DECLARE void VT2102_ConfigTimeSlotLEDDriver( TimeSlotIndex idx, const TimeSlotLEDDriverConfigItems *p_items )
{
    VT2102_ASSERT(idx < TS_NUM);
    VT2102_ASSERT(p_items != NULL);
    VT2102_ASSERT(p_items->crnt_dac_full_scale < TS_LED_FULLSCALE_NUM);

    VT2102_WriteRegisters(REG_TS_CFG_1_ADDR, (uint8_t*)(&idx), 1);

    uint8_t pu8_regs_val[] = {
        p_items->u8_selected_ch_mask,
        p_items->u8_crnt_dac_code,
        0,
        0,
        (uint8_t)p_items->crnt_dac_full_scale,
    };

    VT2102_WriteRegisters(REG_TS_CFG_17_ADDR, &pu8_regs_val[0], 5);
}

VT2102_FUNC_DECLARE void VT2102_GetTimeSlotLEDDriverConfig( TimeSlotIndex idx, TimeSlotLEDDriverConfigItems *p_items )
{
    VT2102_ASSERT(idx < TS_NUM);
    VT2102_ASSERT(p_items != NULL);

    uint8_t pu8_regs_val[5];
    VT2102_WriteRegisters(REG_TS_CFG_1_ADDR, (uint8_t*)(&idx), 1);
    VT2102_ReadRegisters(REG_TS_CFG_17_ADDR, &pu8_regs_val[0], 5);

    p_items->u8_selected_ch_mask = VT2102_Int_ReadFieldInVariable(pu8_regs_val[0], FIELD_TS_LED_A_SELECT);
    p_items->u8_crnt_dac_code    = VT2102_Int_ReadFieldInVariable(pu8_regs_val[1], FIELD_TS_LED_A_CODE);
    p_items->crnt_dac_full_scale = (TimeSlotLEDFullScale)VT2102_Int_ReadFieldInVariable(pu8_regs_val[4], FIELD_TS_LED_A_FULLSCALE);
}

VT2102_FUNC_DECLARE void VT2102_ConfigTimeSlotIIRFilter( TimeSlotIndex idx, const TimeSlotFilterConfigItems *p_items )
{
    VT2102_ASSERT(idx < TS_NUM);
    VT2102_ASSERT(p_items != NULL);
    VT2102_ASSERT(p_items->iir_filt_band_width < TS_IIR_FILT_BW_NUM);
    VT2102_ASSERT(p_items->iir_filt_dec_ratio < TS_IIR_FILT_DCM_NUM);
    
    uint8_t u8_reg_val = 0x00;
    VT2102_WriteRegisters(REG_TS_CFG_1_ADDR, (uint8_t*)(&idx), 1);

    if (p_items->b_iir_filt_enabled == true)
    {
        VT2102_Int_WriteFieldInVariable(u8_reg_val, FIELD_TS_IIR_EN, 0x01);
    }
    VT2102_Int_WriteFieldInVariable(u8_reg_val, FIELD_TS_IIR_RATIO, p_items->iir_filt_band_width);
    VT2102_Int_WriteFieldInVariable(u8_reg_val, FIELD_TS_DCM_RATIO, p_items->iir_filt_dec_ratio);
    
    VT2102_WriteRegisters(REG_TS_CFG_23_ADDR, &u8_reg_val, 1);
}

VT2102_FUNC_DECLARE void VT2102_GetTimeSlotIIRFilterConfig( TimeSlotIndex idx, TimeSlotFilterConfigItems *p_items )
{
    VT2102_ASSERT(idx < TS_NUM);
    VT2102_ASSERT(p_items != NULL);
    
    uint8_t u8_reg_val;
    VT2102_WriteRegisters(REG_TS_CFG_1_ADDR, (uint8_t*)(&idx), 1);
    VT2102_ReadRegisters(REG_TS_CFG_23_ADDR, &u8_reg_val, 1);

    p_items->b_iir_filt_enabled = (VT2102_Int_ReadFieldInVariable(u8_reg_val, FIELD_TS_IIR_EN) == 0x01) ? true : false;
    p_items->iir_filt_band_width = VT2102_Int_ReadFieldInVariable(u8_reg_val, FIELD_TS_IIR_RATIO);
    p_items->iir_filt_dec_ratio  = VT2102_Int_ReadFieldInVariable(u8_reg_val, FIELD_TS_DCM_RATIO);
}

VT2102_FUNC_DECLARE void VT2102_ConfigTimeSlotFIFOStoreRule( TimeSlotIndex idx, TimeSlotFIFOStoreSelection selected_items )
{
    VT2102_ASSERT(idx < TS_NUM);
    uint8_t pu8_regs_val[2];

    VT2102_WriteRegisters(REG_TS_CFG_1_ADDR, (uint8_t*)(&idx), 1);
    VT2102_ReadRegisters(REG_TS_CFG_24_ADDR, &pu8_regs_val[0], 2);

    pu8_regs_val[0] = (uint8_t)(selected_items & 0xFF);
    VT2102_Int_WriteFieldInVariable(pu8_regs_val[1], FIELD_TS_SATU_STORE_FIFO, ((selected_items & TS_SAT_INFO_STORE_ALWAYS) == 0x00) ? 0x00 : 0x01);
    VT2102_WriteRegisters(REG_TS_CFG_24_ADDR, &pu8_regs_val[0], 2);
}

VT2102_FUNC_DECLARE TimeSlotFIFOStoreSelection VT2102_GetTimeSlotFIFOStoreRuleConfig( TimeSlotIndex idx )
{
    VT2102_ASSERT(idx < TS_NUM);
    uint8_t pu8_reg_val[2];

    VT2102_WriteRegisters(REG_TS_CFG_1_ADDR, (uint8_t*)(&idx), 1);
    VT2102_ReadRegisters(REG_TS_CFG_24_ADDR, &pu8_reg_val[0], 2);

    return (TimeSlotFIFOStoreSelection)(pu8_reg_val[0] | (pu8_reg_val[1] << 8));
}

VT2102_FUNC_DECLARE void VT2102_ConfigTimeSlotADCSaturationLevel( TimeSlotIndex idx, TimeSlotADCSaturationLevel level )
{
    VT2102_ASSERT(idx < TS_NUM);
    uint8_t u8_reg_val;

    VT2102_WriteRegisters(REG_TS_CFG_1_ADDR, (uint8_t*)(&idx), 1);
    VT2102_ReadRegisters(REG_TS_CFG_25_ADDR, &u8_reg_val, 1);
    VT2102_Int_WriteFieldInVariable(u8_reg_val, FIELD_TS_SATU_CODE, level);
    VT2102_WriteRegisters(REG_TS_CFG_25_ADDR, &u8_reg_val, 1);
}

VT2102_FUNC_DECLARE TimeSlotADCSaturationLevel VT2102_GetTimeSlotADCSaturationLevelConfig( TimeSlotIndex idx )
{
    VT2102_ASSERT(idx < TS_NUM);
    uint8_t u8_reg_val;

    VT2102_WriteRegisters(REG_TS_CFG_1_ADDR, (uint8_t*)(&idx), 1);
    VT2102_ReadRegisters(REG_TS_CFG_25_ADDR, &u8_reg_val, 1);
    return (TimeSlotADCSaturationLevel)VT2102_Int_ReadFieldInVariable(u8_reg_val, FIELD_TS_SATU_CODE);
}

VT2102_FUNC_DECLARE void VT2102_ConfigTimeSlotTIA( TimeSlotIndex idx, const TimeSlotTIAConfigItems *p_items )
{
    VT2102_ASSERT(idx < TS_NUM);
    VT2102_ASSERT(p_items != NULL);
    VT2102_ASSERT(p_items->pdio_sel < TS_TIA_CHANNEL_TO_PDIO_NUM);
    VT2102_ASSERT(p_items->power_down_mode < TS_TIA_POWER_DOWN_MODE_NUM);
    VT2102_ASSERT(p_items->cfb_select < TS_TIA_CFB_SELECT_NUM);
    VT2102_ASSERT(p_items->rfb_select < TS_TIA_RFB_SELECT_NUM);

    uint8_t pu8_regs_val[2] = {0x00};
    VT2102_WriteRegisters(REG_TS_CFG_1_ADDR, (uint8_t*)(&idx), 1);
    VT2102_ReadRegisters(REG_CH_CFG_1_ADDR, &pu8_regs_val[0], 2);

    VT2102_Int_WriteFieldInVariable(pu8_regs_val[0], FIELD_TS_CHA_PDIO_SEL, p_items->pdio_sel);
    VT2102_Int_WriteFieldInVariable(pu8_regs_val[1], FIELD_TS_CHA_TIA_CF, p_items->cfb_select);
    VT2102_Int_WriteFieldInVariable(pu8_regs_val[1], FIELD_TS_CHA_TIA_RF, p_items->rfb_select);
    VT2102_WriteRegisters(REG_CH_CFG_1_ADDR, &pu8_regs_val[0], 2);

    VT2102_ReadRegisters(REG_TS_CFG_6_ADDR, &pu8_regs_val[0], 1);
    VT2102_Int_WriteFieldInVariable(pu8_regs_val[0], FIELD_TS_TIA_IDLE_PD, p_items->power_down_mode);
    VT2102_WriteRegisters(REG_TS_CFG_6_ADDR, &pu8_regs_val[0], 1);
}

VT2102_FUNC_DECLARE void VT2102_GetTimeSlotTIAConfig( TimeSlotIndex idx, TimeSlotTIAConfigItems *p_items )
{
    VT2102_ASSERT(idx < TS_NUM);
    VT2102_ASSERT(p_items != NULL);

    uint8_t pu8_regs_val[2];
    VT2102_WriteRegisters(REG_TS_CFG_1_ADDR, (uint8_t*)(&idx), 1);
    VT2102_ReadRegisters(REG_CH_CFG_1_ADDR, &pu8_regs_val[0], 2);

    p_items->pdio_sel   = (PDIOTIAChannelMap)VT2102_Int_ReadFieldInVariable(pu8_regs_val[0], FIELD_TS_CHA_PDIO_SEL);
    p_items->cfb_select = (TimeSlotTIACFBSelection)VT2102_Int_ReadFieldInVariable(pu8_regs_val[1], FIELD_TS_CHA_TIA_CF);
    p_items->rfb_select = (TimeSlotTIARFBSelection)VT2102_Int_ReadFieldInVariable(pu8_regs_val[1], FIELD_TS_CHA_TIA_RF);

    VT2102_ReadRegisters(REG_TS_CFG_6_ADDR, &pu8_regs_val[0], 1);
    p_items->power_down_mode = (TimeSlotTIAPowerDownMode)VT2102_Int_ReadFieldInVariable(pu8_regs_val[0], FIELD_TS_TIA_IDLE_PD);
}

VT2102_FUNC_DECLARE void VT2102_ConfigTimeSlotTIAIDAC( TimeSlotIndex idx, const TimeSlotTIAIDACConfigItems *p_items )
{
    VT2102_ASSERT(idx < TS_NUM);
    VT2102_ASSERT(p_items != NULL);

    VT2102_WriteRegisters(REG_TS_CFG_1_ADDR, (uint8_t*)(&idx), 1);
    VT2102_WriteRegisters(REG_CH_CFG_5_ADDR, &p_items->u8_led_idac_code, 1);
    VT2102_WriteRegisters(REG_CH_CFG_9_ADDR, &p_items->u8_amb_idac_code, 1);
}

VT2102_FUNC_DECLARE void VT2102_GetTimeSlotTIAIDACConfig( TimeSlotIndex idx, TimeSlotTIAIDACConfigItems *p_items )
{
    VT2102_ASSERT(idx < TS_NUM);
    VT2102_ASSERT(p_items != NULL);

    VT2102_WriteRegisters(REG_TS_CFG_1_ADDR, (uint8_t*)(&idx), 1);
    VT2102_ReadRegisters(REG_CH_CFG_5_ADDR, &p_items->u8_led_idac_code, 1);
    VT2102_ReadRegisters(REG_CH_CFG_9_ADDR, &p_items->u8_amb_idac_code, 1);
}

VT2102_FUNC_DECLARE void VT2102_ConfigTimeSlotAlarm( TimeSlotIndex idx, const TimeSlotAlarmConfigItems *p_items )
{
    VT2102_ASSERT(idx < TS_NUM);
    VT2102_ASSERT(p_items != NULL);
    VT2102_ASSERT(p_items->u8_alcm_step < 4);
    VT2102_ASSERT(p_items->alarm_src < TS_ALARM_SRC_NUM);
    VT2102_ASSERT(p_items->u8_alarm_trip_cnt < 16);
    VT2102_ASSERT(p_items->comp_mode < TS_ALARM_COMP_MODE_NUM);

    uint8_t pu8_regs_val[9] = {0x00};
    VT2102_WriteRegisters(REG_TS_CFG_1_ADDR, (uint8_t*)(&idx), 1);

    if (p_items->b_alarm_enabled == true)
    {
        VT2102_Int_WriteFieldInVariable(pu8_regs_val[0], FIELD_TS_ALARM_CHA_EN, 0x01);
    }
    VT2102_Int_WriteFieldInVariable(pu8_regs_val[0], FIELD_TS_ALARM_DATA_FIL, p_items->alarm_src >> 2);
    VT2102_Int_WriteFieldInVariable(pu8_regs_val[0], FIELD_TS_ALARM_DATA_SEL, p_items->alarm_src & 0x03);

    if (p_items->alarm_intr_enabled == true)
    {
        VT2102_Int_WriteFieldInVariable(pu8_regs_val[1], FIELD_TS_ALARM_INTR, 0x01);
    }
    VT2102_Int_WriteFieldInVariable(pu8_regs_val[1], FIELD_TS_ALARM_TRIP_CNT, p_items->u8_alarm_trip_cnt);
    
    if (p_items->comp_mode == TS_ALARM_COMP_MODE_ABSOLUTE)
    {
        VT2102_Int_WriteFieldInVariable(pu8_regs_val[1], FIELD_TS_ALARM_ABS_MODE, 0x01);
    }

    if (p_items->b_alcm_enabled == true)
    {
        VT2102_Int_WriteFieldInVariable(pu8_regs_val[2], FIELD_TS_ALARM_ALCM_LED_A, 0x01);
    }
    VT2102_Int_WriteFieldInVariable(pu8_regs_val[2], FIELD_TS_ALARM_ALCM_STEP, p_items->u8_alcm_step);
    pu8_regs_val[3] = (uint8_t)((*(((uint8_t*)(&p_items->s16_alarm_lo_thres)) + 1)) & 0x03);
    pu8_regs_val[4] = *((uint8_t*)(&p_items->s16_alarm_lo_thres));
    pu8_regs_val[5] = (uint8_t)((*(((uint8_t*)(&p_items->s16_alarm_hi_thres)) + 1)) & 0x03);
    pu8_regs_val[6] = *((uint8_t*)(&p_items->s16_alarm_hi_thres));
    pu8_regs_val[7] = p_items->u8_alcm_hi_sat;
    pu8_regs_val[8] = p_items->u8_alcm_lo_sat;

    VT2102_WriteRegisters(REG_ALARM_CFG_1_ADDR, &pu8_regs_val[0], 9);
}

VT2102_FUNC_DECLARE void VT2102_GetTimeSlotAlarmConfig( TimeSlotIndex idx, TimeSlotAlarmConfigItems *p_items )
{
    VT2102_ASSERT(idx < TS_NUM);
    VT2102_ASSERT(p_items != NULL);

    uint8_t pu8_regs_val[9];
    VT2102_WriteRegisters(REG_TS_CFG_1_ADDR, (uint8_t*)(&idx), 1);
    VT2102_ReadRegisters(REG_ALARM_CFG_1_ADDR, &pu8_regs_val[0], 9);

    p_items->b_alarm_enabled    = (VT2102_Int_ReadFieldInVariable(pu8_regs_val[0], FIELD_TS_ALARM_CHA_EN) == 0x01) ? true : false;
    p_items->alarm_src          = (VT2102_Int_ReadFieldInVariable(pu8_regs_val[0], FIELD_TS_ALARM_DATA_FIL) << 2);
    p_items->alarm_src         |= VT2102_Int_ReadFieldInVariable(pu8_regs_val[0], FIELD_TS_ALARM_DATA_SEL);
    p_items->comp_mode          = VT2102_Int_ReadFieldInVariable(pu8_regs_val[1], FIELD_TS_ALARM_ABS_MODE);
    p_items->alarm_intr_enabled = (VT2102_Int_ReadFieldInVariable(pu8_regs_val[1], FIELD_TS_ALARM_INTR) == 0x01) ? true : false;
    p_items->u8_alarm_trip_cnt  = VT2102_Int_ReadFieldInVariable(pu8_regs_val[1], FIELD_TS_ALARM_TRIP_CNT);
    p_items->b_alcm_enabled     = (VT2102_Int_ReadFieldInVariable(pu8_regs_val[2], FIELD_TS_ALARM_ALCM_LED_A) == 0x01) ? true : false;
    p_items->u8_alcm_step       = VT2102_Int_ReadFieldInVariable(pu8_regs_val[2], FIELD_TS_ALARM_ALCM_STEP);
    p_items->s16_alarm_lo_thres = (int16_t)((pu8_regs_val[3] << 8) + pu8_regs_val[4]);
    p_items->s16_alarm_hi_thres = (int16_t)((pu8_regs_val[5] << 8) + pu8_regs_val[6]);
    p_items->u8_alcm_hi_sat     = pu8_regs_val[7];
    p_items->u8_alcm_lo_sat     = pu8_regs_val[8];
}

VT2102_FUNC_DECLARE void VT2102_ConfigGPIO( const VT2102GPIOConfigItems *p_items )
{
    VT2102_ASSERT(p_items != NULL);
    VT2102_ASSERT(p_items->gpio_func < GPIO_FUNC_NUM);
    VT2102_ASSERT(p_items->slot_timing_output_idx_filter < GPIO_TS_OUTPUT_SLOT_NUM);
    VT2102_ASSERT(p_items->sys_debug_output < GPIO_SYSTEM_DEBUG_OUTPUT_NUM);
    VT2102_ASSERT(p_items->u8_gpio_out_clk_div_ratio < 16);

    uint8_t u8_regs_val[5] = {0x00};
    u8_regs_val[0] = (uint8_t)p_items->gpio_func;
    u8_regs_val[1] = p_items->u8_gpio_out_clk_div_ratio;
    u8_regs_val[2] = (uint8_t)p_items->slot_timing_output_idx_filter;
    u8_regs_val[3] = (uint8_t)p_items->slot_timing_output_event_filter;
    u8_regs_val[4] = (uint8_t)p_items->sys_debug_output;

    VT2102_Int_WriteFieldInVariable(u8_regs_val[1], 
                                    FIELD_GPIO1_INPUT_PE, 
                                    ((p_items->b_pull_res_enabled == true) ? 0x01 : 0x00));

    VT2102_WriteRegisters(REG_GPIO1_CFG_1_ADDR, &u8_regs_val[0], 5);
}

VT2102_FUNC_DECLARE void VT2102_GetGPIOConfig( VT2102GPIOConfigItems *p_items )
{
    VT2102_ASSERT(p_items != NULL);
    uint8_t u8_regs_val[5] = {0x00};

    VT2102_ReadRegisters(REG_GPIO1_CFG_1_ADDR, &u8_regs_val[0], 5);
    p_items->gpio_func                       = VT2102_Int_ReadFieldInVariable(u8_regs_val[0], FIELD_GPIO1_FUNC_SEL);
    p_items->u8_gpio_out_clk_div_ratio       = VT2102_Int_ReadFieldInVariable(u8_regs_val[1], FIELD_GPIO1_CLK_OUT_DIV);
    p_items->b_pull_res_enabled              = ((VT2102_Int_ReadFieldInVariable(u8_regs_val[1], FIELD_GPIO1_INPUT_PE) == 0x01) ? true : false);
    p_items->slot_timing_output_idx_filter   = (u8_regs_val[2] & (FIELD_GPIO1_TS_ALL_MASK | FIELD_GPIO1_TS_SEL_MASK));
    p_items->slot_timing_output_event_filter = u8_regs_val[3];
    p_items->sys_debug_output                = VT2102_Int_ReadFieldInVariable(u8_regs_val[4], FIELD_GPIO1_SYSDBG_SEL);
}

VT2102_FUNC_DECLARE GPIOLevel VT2102_ReadGPIODigitalInput( void )
{
    GPIOLevel rtn_val;
    uint8_t u8_reg_val;

    VT2102_ReadRegisters(REG_GPIO1_CFG_6_ADDR, &u8_reg_val, 1);
    if (u8_reg_val == 0x01)
    {
        rtn_val = GPIO_LEVEL_HIGH;
    }
    else
    {
        rtn_val = GPIO_LEVEL_LOW;
    }

    return rtn_val;
}

VT2102_FUNC_DECLARE uint8_t VT2102_GetChipID( void )
{
    uint8_t u8_chip_id = 0x00;
    VT2102_ReadRegisters(REG_CHIP_ID_ADDR, &u8_chip_id, 0x01);
    return u8_chip_id;
}

VT2102_FUNC_DECLARE void VT2102_SoftReset( void )
{
    uint8_t u8_reg_val = 0x01;
    VT2102_WriteRegisters(REG_SYS_CFG_1_ADDR, &u8_reg_val, 0x01);
}

VT2102_FUNC_DECLARE void VT2102_StartConversion( VT2102ConvsionMode mode )
{
    uint8_t u8_reg_val = (0x01 << BIT_OFFSET(FIELD_TIMESLOT_CONFIG_LOCK));

    VT2102_ASSERT(  mode == VT2102_CONV_MODE_AUTO ||                                        mode == VT2102_CONV_MODE_ONCE ||                                        mode == VT2102_CONV_MODE_ONCE_WITH_REFRESH ||                           mode == VT2102_CONV_MODE_GPIO);

    VT2102_WriteRegisters(REG_TS_CFG_1_ADDR, &u8_reg_val, 0x01);
    VT2102_WriteRegisters(REG_GLB_CFG_16_ADDR, (uint8_t*)&mode, 0x01);
}

VT2102_FUNC_DECLARE void VT2102_StopConversion( void )
{
    uint8_t u8_reg_val = 0x00;
    VT2102_WriteRegisters(REG_GLB_CFG_16_ADDR, &u8_reg_val, 0x01);
}

VT2102_FUNC_DECLARE bool VT2102_IsConversionCompleted( void )
{
    bool b_completed;
    uint8_t u8_reg_val;
    (void)VT2102_ReadRegisters(REG_GLB_CFG_16_ADDR, &u8_reg_val, 0x01);
    b_completed = ((u8_reg_val == 0x00) ? true : false);
    return b_completed;
}

VT2102_FUNC_DECLARE bool VT2102_IsTimeSlotActive( void )
{
    bool b_active;
    uint8_t u8_reg_val;
    (void)VT2102_ReadRegisters(REG_GLB_CFG_16_ADDR, &u8_reg_val, 0x01);
    b_active = (((u8_reg_val & FIELD_TIMESLOT_ACTIVE_MASK) == 0x00) ? false : true);
    return b_active;
}

VT2102_FUNC_DECLARE void VT2102_SetI2CDeadLockEnabled( bool b_enabled, uint16_t u16_timeout )
{
    VT2102_ASSERT(u16_timeout >= 0x0F);
    uint8_t pu8_regs_val[3];
    
    VT2102_Int_WriteFieldInVariable(pu8_regs_val[0], FIELD_I2C_DEADLOCK_MON_EN, ((b_enabled == true) ? 0x01 : 0x00));
    pu8_regs_val[1] = (uint8_t)(u16_timeout >> 8);
    pu8_regs_val[2] = (uint8_t)(u16_timeout & 0xFF);

    if (b_enabled == true)
    {
        VT2102_WriteRegisters(REG_PAD_CFG_2_ADDR, &pu8_regs_val[0], 0x03);
    }
    else
    {
        VT2102_WriteRegisters(REG_PAD_CFG_2_ADDR, &pu8_regs_val[0], 0x01);
    }
}
