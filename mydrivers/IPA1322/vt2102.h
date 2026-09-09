#ifndef VT2102_H__
#define VT2102_H__

#include <stdbool.h>
#include <stdint.h>

#include "vt2102_config.h"
#include "vt2102_reg.h"

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef VT2102_PROTOTYPE_TEST
    #define VT2102_FUNC_EXPORT __declspec(dllexport)
    #define VT2102_FUNC_DECLARE __declspec(dllexport)
#else
    #define VT2102_FUNC_EXPORT extern
    #define VT2102_FUNC_DECLARE
#endif

#ifdef VT2102_DEBUG
    #define VT2102_ASSERT(x)    if(!(x)) {while(1);}
#else
    #define VT2102_ASSERT(x)
#endif

VT2102_FUNC_EXPORT void VT2102_WriteRegisters( uint8_t u8RegStartAddr, const uint8_t pu8Value[], uint32_t u32SizeInByte );
VT2102_FUNC_EXPORT void VT2102_ReadRegisters( uint8_t u8RegStartAddr, uint8_t *pu8Buff, uint32_t u32SizeInByte );

typedef enum
{
    VT2102_INTR_SOURCE_FIFO_AFULL       = 0x007,
    VT2102_INTR_SOURCE_FIFO_DATA_RDY    = 0x006,
    VT2102_INTR_SOURCE_FIFO_OFLOW       = 0x005,
    VT2102_INTR_SOURCE_FIFO_UFLOW       = 0x004,
    VT2102_INTR_SOURCE_FRAME_OVERLAP    = 0x003,
    VT2102_INTR_SOURCE_PWR_RDY          = 0x000,
    VT2102_INTR_SOURCE_LOW_ALARM        = 0x200,
    VT2102_INTR_SOURCE_HIGH_ALARM       = 0x300,

    VT2102_INTR_SOURCE_ALARM_TS_A       = 0x01,
    VT2102_INTR_SOURCE_ALARM_TS_B       = 0x02,
    VT2102_INTR_SOURCE_ALARM_TS_C       = 0x04,
    VT2102_INTR_SOURCE_ALARM_TS_D       = 0x08
} VT2102IntSource;

VT2102_FUNC_EXPORT void VT2102_ReadAllInterruptFlags( uint8_t *pu8_buff );

#define VT2102_IsInterruptFlagSet(buff,src)         ((*(buff + (((uint16_t)src) >> 8)) & (0x01 << (src & 0xFF))) != 0x00 ? true: false)

VT2102_FUNC_EXPORT void VT2102_SetInterruptEnabled( VT2102IntSource int_src, bool b_enabled );

typedef enum
{
    VT2102_OSC_4M_PD_NEVER          = 0x00,
    VT2102_OSC_4M_PD_AUTO           = 0x01,
    VT2102_OSC_4M_PD_MANUAL         = 0x02,
    VT2102_OSC_4M_PD_MODE_NUM
} VT2102Osc4MPDMode;

typedef enum
{
    VT2102_OSC_32K_PD_OFF           = 0x00,
    VT2102_OSC_32K_PD_ON            = 0x01,
    VT2102_OSC_32K_PD_MODE_NUM
} VT2102Osc32kPDMode;

VT2102_FUNC_EXPORT void VT2102_ConfigOnChipOscillator( VT2102Osc4MPDMode osc_4m_pd_mode, VT2102Osc32kPDMode osc_32k_pd_mode );
VT2102_FUNC_EXPORT void VT2102_GetOnChipOscillatorConfig( VT2102Osc4MPDMode *p_osc_4m_pd_mode, VT2102Osc32kPDMode *p_osc_32k_pd_mode );
VT2102_FUNC_EXPORT void VT2102_EnterLowPowerMode( void );

typedef enum
{
    VT2102_FIFO_DATA_SRC_ADC_RAW          = 0x00,
    VT2102_FIFO_DATA_SRC_SIGNAL,
    VT2102_FIFO_DATA_SRC_AACM_ALCM,
    VT2102_FIFO_DATA_SRC_UF_OF,
    VT2102_FIFO_DATA_SRC_NONE,
    VT2102_FIFO_DATA_SRC_NUM,
} VT2102FIFODataSource;

typedef struct {uint8_t pu8_buff[3];} VT2102FIFOData;

#define VT2102_GetSlotIDFromFIFOData(p_data)                (p_data->pu8_buff[0] >> 6)
#define VT2102_GetDataSourceFromFIFOData(p_data)            ((p_data->pu8_buff[0] & 0x30) >> 4)

#define VT2102_GetADCCodeFromFIFOData(p_data)               (((((uint32_t)p_data->pu8_buff[0]) & 0x0F) << 16) +                                                             (((uint32_t)p_data->pu8_buff[1]) << 8) +                                                                        (p_data->pu8_buff[2] & 0xFF))

#define VT2102_GetLEDDrvFSRFromFIFOData(p_data)             ((p_data->pu8_buff[0] & 0x0F) >> 2)
#define VT2102_GetLEDDrvDACCodeFromFIFOData(p_data)         (((p_data->pu8_buff[0] & 0x03) << 6) +                                                                          (p_data->pu8_buff[1] >> 2))
#define VT2102_GetAmbIDACCodeFromFIFOData(p_data)           (p_data->pu8_buff[2])

#define UNDER_FLOW      ((uint8_t)0)
#define OVER_FLOW       ((uint8_t)1)
#define SIGNAL          ((uint8_t)6)
#define DARK1           ((uint8_t)4)
#define LIGHT           ((uint8_t)2)
#define DARK0           ((uint8_t)0)
#define VT2102_GetFlagFromFIFOData(p_data,src,dir)          ((p_data->pu8_buff[2] & (0x01 << (src + dir))) != 0x00 ? true : false)

VT2102_FUNC_EXPORT void VT2102_ReadFIFO( VT2102FIFOData *p_data, uint32_t u32_data_cnt );

typedef enum VT2102FIFOAlmostFullType
{
    VT2102_FIFO_AF_TYPE_CONSECUTIVE         = 0x00,
    VT2102_FIFO_AF_TYPE_NON_CONSECUTIVE,
    VT2102_FIFO_AF_TYPE_NUM
} VT2102FIFOAlmostFullType;

VT2102_FUNC_EXPORT void VT2102_ConfigFIFOAFull( uint8_t u8_afull_thres, VT2102FIFOAlmostFullType type );
VT2102_FUNC_EXPORT void VT2102_GetFIFOPointers( uint8_t *pu8_write_ptr, uint8_t *pu8_read_ptr );
VT2102_FUNC_EXPORT uint8_t VT2102_GetFIFOCount( void );
VT2102_FUNC_EXPORT void VT2102_ClearFIFO( void );

VT2102_FUNC_EXPORT void VT2102_SetFramePeriod( uint32_t u32_period );
VT2102_FUNC_EXPORT uint32_t VT2102_GetFramePeriod( void );

typedef enum PDIOState
{
    PDIO_STATE_SHORT_2_VCM          = 0x00,
    PDIO_STATE_OPEN_FLOAT,
    PDIO_STATE_SHORT_FLOAT,
    PDIO_STATE_SHORT_2_GND,
    PDIO_STATE_NUM
} PDIOState;

typedef enum PDIOType
{
    PDIO_TYPE_DIFFERENTIAL          = 0x00,
    PDIO_TYPE_SINGLE_END,
    PDIO_TYPE_NUM
} PDIOType;

typedef struct PDIOConfigItems
{
    PDIOState       precon_state;
    PDIOState       sleep_state;
    PDIOType        type;
    uint8_t         u8_precon_width_us;
} PDIOConfigItems;

VT2102_FUNC_EXPORT void VT2102_ConfigPDIO( const PDIOConfigItems *p_items );
VT2102_FUNC_EXPORT void VT2102_GetPDIOConfig( PDIOConfigItems *p_items );

typedef enum
{
    ADC_BUF_EMPOWER_LOW_POWER_MODE      = 0x00,
    ADC_BUF_EMPOWER_HIGH_PERF_MODE      = 0x01,
    ADC_BUF_EMPOWER_MODE_NUM
} ADCBufferEMPowerMode;

VT2102_FUNC_EXPORT void VT2102_SetADCBufferEmpowerMode( ADCBufferEMPowerMode mode );
VT2102_FUNC_EXPORT ADCBufferEMPowerMode VT2102_GetADCBufferEmpowerMode( void );

typedef enum AACMFullScale
{
    AACM_FULL_SCALE_255_5UA     = 0x00,
    AACM_FULL_SCALE_127_5UA,
    AACM_FULL_SCALE_63_5UA,
    AACM_FULL_SCALE_NUM
} AACMFullScale;

typedef struct AACMConfigItems
{
    AACMFullScale   full_scale;
    uint8_t         start_time;
    uint8_t         end_time;
} AACMConfigItems;

VT2102_FUNC_EXPORT void VT2102_ConfigAACMGlobal( const AACMConfigItems *p_items );
VT2102_FUNC_EXPORT void VT2102_GetAACMGlobalConfig( AACMConfigItems *p_items );

typedef enum PMICTxMode
{
    PMIC_TX_MODE_SYNC               = 0x00,
    PMIC_TX_MODE_COMMAND,
    PMIC_TX_MODE_NUM
} PMICTxMode;

typedef enum PMICTxBitWidth
{
    PMIC_TX_BIT_WIDTH_0_5US         = 0x00,
    PMIC_TX_BIT_WIDTH_1_0US,
    PMIC_TX_BIT_WIDTH_1_5US,
    PMIC_TX_BIT_WIDTH_2_0US,
    PMIC_TX_BIT_WIDTH_NUM
} PMICTxBitWidth;

typedef enum PMICTxOutMode
{
    PMIC_TX_OUT_PP                  = 0x00,
    PMIC_TX_OUT_OD_NO_PU            = 0x01,
    PMIC_TX_OUT_OD_WITH_PU          = 0x03,
    PMIC_TX_OUT_NUM
} PMICTxOutMode;

typedef enum PMICTxParity
{
    PMIC_TX_PARITY_EVEN             = 0x00,
    PMIC_TX_PARITY_ODD,
    PMIC_TX_PARITY_NUM
} PMICTxParity;

#define MAX_PMIC_TX_PREP_X4_VALUE       (uint8_t)(64)

typedef struct PMICConfigItems
{
    PMICTxMode      tx_mode;
    PMICTxBitWidth  tx_bit_width;
    uint8_t         tx_prep_x4;
    bool            b_tx_sync_idle;
    PMICTxOutMode   tx_out_mode;
    PMICTxParity    tx_odd;
} PMICConfigItems;

VT2102_FUNC_EXPORT void VT2102_ConfigPMIC( const PMICConfigItems *p_items );
VT2102_FUNC_EXPORT void VT2102_GetPMICConfig( PMICConfigItems *p_items );

typedef enum 
{
    TS_A       = 0x00,
    TS_B,
    TS_C,
    TS_D,
    TS_NUM
} TimeSlotIndex;

typedef enum TimeSlotEndTimePreDivider
{
    TS_END_CLK_PREDIV_16        = 0x00,
    TS_END_CLK_PREDIV_1024,
    TS_END_CLK_PREDIV_65536,
    TS_END_CLK_PREDIV_4194304,
    TS_END_CLK_PREDIV_NUM
} TimeSlotEndTimePreDivider;

typedef enum
{
    TS_SUB_SAMPLE_NONE          = 0x00,
    TS_SUB_SAMPLE_RATIO_2,
    TS_SUB_SAMPLE_RATIO_3,
    TS_SUB_SAMPLE_RATIO_4,
    TS_SUB_SAMPLE_RATIO_5,
    TS_SUB_SAMPLE_RATIO_6,
    TS_SUB_SAMPLE_RATIO_7,
    TS_SUB_SAMPLE_RATIO_8,
    TS_SUB_SAMPLE_RATIO_16,
    TS_SUB_SAMPLE_RATIO_32,
    TS_SUB_SAMPLE_RATIO_64,
    TS_SUB_SAMPLE_RATIO_128,
    TS_SUB_SAMPLE_RATIO_256,
    TS_SUB_SAMPLE_RATIO_512,
    TS_SUB_SAMPLE_RATIO_1024,
    TS_SUB_SAMPLE_RATIO_2048,
    TS_SUB_SAMPLE_RATIO_SEL_NUM,
} TimeSlotSubSampleRatio;

typedef struct TimeSlotConfigItems
{
    bool                        enabled;
    TimeSlotSubSampleRatio      sub_sample_ratio;
    bool                        precon_enabled;

    TimeSlotEndTimePreDivider   slot_end_clk_prediv;
    uint8_t                     u8_slot_end_clk_cnt;
} TimeSlotConfigItems;

VT2102_FUNC_EXPORT void VT2102_ConfigTimeSlot( TimeSlotIndex idx, const TimeSlotConfigItems *p_items );
VT2102_FUNC_EXPORT void VT2102_GetTimeSlotConfig( TimeSlotIndex idx, TimeSlotConfigItems *p_items );

typedef enum
{
    TS_MEASURE_TYPE_LIGHT_ONLY              = 0x00,
    TS_MEASURE_TYPE_LIGHT_DARK1,
    TS_MEASURE_TYPE_DARK0_LIGHT,
    TS_MEASURE_TYPE_DARK0_LIGHT_DARK1,
    TS_MEASURE_TYPE_NUM
} TimeSlotMeasureType;

typedef enum
{
    TS_ADC_OSR_16           = 0x00,
    TS_ADC_OSR_32,
    TS_ADC_OSR_64,
    TS_ADC_OSR_128,
    TS_ADC_OSR_256,
    TS_ADC_OSR_512,
    TS_ADC_OSR_1024,
    TS_ADC_OSR_2048,
    TS_ADC_OSR_40,
    TS_ADC_OSR_50,
    TS_ADC_OSR_NUM
} TimeSlotADCOverSampling;

typedef struct TimeSlotMeasureConfigItems
{
    TimeSlotMeasureType         measure_type;
    TimeSlotADCOverSampling     adc_osr;
} TimeSlotMeasureConfigItems;

VT2102_FUNC_EXPORT void VT2102_ConfigTimeSlotMeasure( TimeSlotIndex idx, const TimeSlotMeasureConfigItems *p_items );
VT2102_FUNC_EXPORT void VT2102_GetTimeSlotMeasureConfig( TimeSlotIndex idx, TimeSlotMeasureConfigItems *p_items );

typedef struct TimeSlotPMICConfigItems
{
    bool                        pmic_ctrl_enabled;
    uint8_t                     pmic_cmd;
} TimeSlotPMICConfigItems;

VT2102_FUNC_EXPORT void VT2102_ConfigTimeSlotPMIC( TimeSlotIndex idx, const TimeSlotPMICConfigItems *p_items );
VT2102_FUNC_EXPORT void VT2102_GetTimeSlotPMICConfig( TimeSlotIndex idx, TimeSlotPMICConfigItems *p_items );

typedef struct TimeSlotTimingConfigItems
{
    uint16_t                    u16_dark0_start_time;
    uint16_t                    u16_led_idac_start_time;
    uint16_t                    u16_led_drive_start_time;
    uint16_t                    u16_light_start_time;
    uint16_t                    u16_dark1_start_time;
} TimeSlotTimingConfigItems;

VT2102_FUNC_EXPORT void VT2102_ConfigTimeSlotTiming( TimeSlotIndex idx, const TimeSlotTimingConfigItems *p_items );
VT2102_FUNC_EXPORT void VT2102_GetTimeSlotTimingConfig( TimeSlotIndex idx, TimeSlotTimingConfigItems *p_items );

typedef enum
{
    TS_LED_FULLSCALE_120MA          = 0x00,
    TS_LED_FULLSCALE_240MA,
    TS_LED_FULLSCALE_NUM,
} TimeSlotLEDFullScale;

#define TS_LED_NONE_SELECTED        ((uint8_t)0x00)
#define TS_LED0_SELECTED            ((uint8_t)0x01)
#define TS_LED1_SELECTED            ((uint8_t)0x02)
#define TS_LED2_SELECTED            ((uint8_t)0x04)

typedef struct TimeSlotLEDDriverConfigItems
{
    uint8_t                     u8_selected_ch_mask;
    
    TimeSlotLEDFullScale        crnt_dac_full_scale;
    uint8_t                     u8_crnt_dac_code;
} TimeSlotLEDDriverConfigItems;

VT2102_FUNC_EXPORT void VT2102_ConfigTimeSlotLEDDriver( TimeSlotIndex idx, const TimeSlotLEDDriverConfigItems *p_items );
VT2102_FUNC_EXPORT void VT2102_GetTimeSlotLEDDriverConfig( TimeSlotIndex idx, TimeSlotLEDDriverConfigItems *p_items );

typedef struct TimeSlotFilterConfigItems
{
    bool                        b_iir_filt_enabled;
    enum
    {
        TS_IIR_FILT_DCM_1       = 0x00,
        TS_IIR_FILT_DCM_2,
        TS_IIR_FILT_DCM_4,
        TS_IIR_FILT_DCM_8,
        TS_IIR_FILT_DCM_16,
        TS_IIR_FILT_DCM_32,
        TS_IIR_FILT_DCM_64,
        TS_IIR_FILT_DCM_128,
        TS_IIR_FILT_DCM_NUM
    }                           iir_filt_dec_ratio;
    enum
    {
        TS_IIR_FILT_BW_BROAD    = 0x00,
        TS_IIR_FILT_BW_NORMAL,
        TS_IIR_FILT_BW_NARROW,
        TS_IIR_FILT_BW_NARROWEST,
        TS_IIR_FILT_BW_NUM
    }                           iir_filt_band_width;
} TimeSlotFilterConfigItems;

VT2102_FUNC_EXPORT void VT2102_ConfigTimeSlotIIRFilter( TimeSlotIndex idx, const TimeSlotFilterConfigItems *p_items );
VT2102_FUNC_EXPORT void VT2102_GetTimeSlotIIRFilterConfig( TimeSlotIndex idx, TimeSlotFilterConfigItems *p_items );

typedef enum TimeSlotFIFOStoreSelection
{
    TS_LED_STORE_ALWAYS             = (0x01 << FIELD_TS_LED_STORE_FIFO_BIT_OFFSET),
    TS_LED_STORE_ONLY_CHANGES       = (0x02 << FIELD_TS_LED_STORE_FIFO_BIT_OFFSET),
    TS_IDAC_STORE_ALWAYS            = (0x01 << FIELD_TS_IDAC_STORE_FIFO_BIT_OFFSET),
    TS_IDAC_STORE_ONLY_CHANGES      = (0x02 << FIELD_TS_IDAC_STORE_FIFO_BIT_OFFSET),
    TS_DARK0_STORE_ALWAYS           = (0x01 << FIELD_TS_DARK0_STORE_FIFO_BIT_OFFSET),
    TS_LIGHT_STORE_ALWAYS           = (0x01 << FIELD_TS_LIGHT_STORE_FIFO_BIT_OFFSET),
    TS_DARK1_STORE_ALWAYS           = (0x01 << FIELD_TS_DARK1_STORE_FIFO_BIT_OFFSET),
    TS_SIGNAL_STORE_ALWAYS          = (0x01 << FIELD_TS_SIGNAL_STORE_FIFO_BIT_OFFSET),
    TS_SAT_INFO_STORE_ALWAYS        = 0x100
} TimeSlotFIFOStoreSelection;

VT2102_FUNC_EXPORT void VT2102_ConfigTimeSlotFIFOStoreRule( TimeSlotIndex idx, TimeSlotFIFOStoreSelection selected_items );
VT2102_FUNC_EXPORT TimeSlotFIFOStoreSelection VT2102_GetTimeSlotFIFOStoreRuleConfig( TimeSlotIndex idx );

typedef enum
{
    TS_ADC_SAT_LEVEL_4_MSB          = 0x00,
    TS_ADC_SAT_LEVEL_5_MSB,
    TS_ADC_SAT_LEVEL_6_MSB,
    TS_ADC_SAT_LEVEL_7_MSB,
    TS_ADC_SAT_LEVEL_NUM,
} TimeSlotADCSaturationLevel;

VT2102_FUNC_EXPORT void VT2102_ConfigTimeSlotADCSaturationLevel( TimeSlotIndex idx, TimeSlotADCSaturationLevel level );
VT2102_FUNC_EXPORT TimeSlotADCSaturationLevel VT2102_GetTimeSlotADCSaturationLevelConfig( TimeSlotIndex idx );

typedef enum TimeSlotTIAChannelToPDIO
{
    TS_TIA_CHANNEL_TO_PDIO          = 0x00,
    TS_TIA_CHANNEL_TO_NO_PDIO,
    TS_TIA_CHANNEL_TO_PDIO_NUM
} PDIOTIAChannelMap;

typedef enum TimeSlotTIAPowerDownMode
{
    TS_TIA_POWER_DOWN_NEVER         = 0x00,
    TS_TIA_POWER_DOWN_WHEN_IDLE,
    TS_TIA_POWER_DOWN_MODE_NUM
} TimeSlotTIAPowerDownMode;

typedef enum TimeSlotTIACFBSelection
{
    TS_TIA_CFB_SELECT_2_5PF         = 0x00,
    TS_TIA_CFB_SELECT_5_0PF,
    TS_TIA_CFB_SELECT_7_5PF,
    TS_TIA_CFB_SELECT_10_0PF,
    TS_TIA_CFB_SELECT_NUM
} TimeSlotTIACFBSelection;

typedef enum TimeSlotTIARFBSelection
{
    TS_TIA_RFB_SELECT_INFINITY      = 0x00,
    TS_TIA_RFB_SELECT_2M,
    TS_TIA_RFB_SELECT_1M,
    TS_TIA_RFB_SELECT_500K,
    TS_TIA_RFB_SELECT_250K,
    TS_TIA_RFB_SELECT_100K,
    TS_TIA_RFB_SELECT_50K,
    TS_TIA_RFB_SELECT_10K,
    TS_TIA_RFB_SELECT_NUM,
} TimeSlotTIARFBSelection;

typedef struct TimeSlotTIAConfigItems
{
    PDIOTIAChannelMap           pdio_sel;
    TimeSlotTIAPowerDownMode    power_down_mode;
    TimeSlotTIACFBSelection     cfb_select;
    TimeSlotTIARFBSelection     rfb_select;
} TimeSlotTIAConfigItems;

VT2102_FUNC_EXPORT void VT2102_ConfigTimeSlotTIA( TimeSlotIndex idx, const TimeSlotTIAConfigItems *p_items );
VT2102_FUNC_EXPORT void VT2102_GetTimeSlotTIAConfig( TimeSlotIndex idx, TimeSlotTIAConfigItems *p_items );

typedef struct TimeSlotTIAIDACConfigItems
{
    uint8_t                 u8_led_idac_code;
    uint8_t                 u8_amb_idac_code;
} TimeSlotTIAIDACConfigItems;

VT2102_FUNC_EXPORT void VT2102_ConfigTimeSlotTIAIDAC( TimeSlotIndex idx, const TimeSlotTIAIDACConfigItems *p_items );
VT2102_FUNC_EXPORT void VT2102_GetTimeSlotTIAIDACConfig( TimeSlotIndex idx, TimeSlotTIAIDACConfigItems *p_items );

typedef struct TimeSlotAlarmConfigItems
{
    bool                    b_alarm_enabled;

    enum
    {
        TS_ALARM_SRC_DARK0_RAW_DATA         = 0x00,
        TS_ALARM_SRC_LIGHT_RAW_DATA,
        TS_ALARM_SRC_DARK1_RAW_DATA,
        TS_ALARM_SRC_SIGNAL_RAW_DATA,
        TS_ALARM_SRC_DARK0_FILTERED_DATA,
        TS_ALARM_SRC_LIGHT_FILTERED_DATA,
        TS_ALARM_SRC_DARK1_FILTERED_DATA,
        TS_ALARM_SRC_SIGNAL_FILTERED_DATA,
        TS_ALARM_SRC_NUM
    }                       alarm_src;

    bool                    alarm_intr_enabled;
    uint8_t                 u8_alarm_trip_cnt;
    
    bool                    b_alcm_enabled;
    uint8_t                 u8_alcm_step;
    uint8_t                 u8_alcm_hi_sat;
    uint8_t                 u8_alcm_lo_sat;

    bool                    b_aacm_enabled;

    enum
    {
        TS_ALARM_COMP_MODE_ORIGINAL       = 0x00,
        TS_ALARM_COMP_MODE_ABSOLUTE,
        TS_ALARM_COMP_MODE_NUM
    }                       comp_mode;

    int16_t                 s16_alarm_lo_thres;
    int16_t                 s16_alarm_hi_thres;
} TimeSlotAlarmConfigItems;

VT2102_FUNC_EXPORT void VT2102_ConfigTimeSlotAlarm( TimeSlotIndex idx, const TimeSlotAlarmConfigItems *p_items );
VT2102_FUNC_EXPORT void VT2102_GetTimeSlotAlarmConfig( TimeSlotIndex idx, TimeSlotAlarmConfigItems *p_items );

typedef struct VT2102GPIOConfigItems
{
    enum
    {
        GPIO_FUNC_INPUT_RISE_EDGE_TRIG_ONCE_CONVERT     = 0x00,
        GPIO_FUNC_INPUT_FALL_EDGE_TRIG_ONCE_CONVERT     = 0x01,
        GPIO_FUNC_INPUT_HIGH_LEVEL_TRIG_AUTO_CONVERT    = 0x02,
        GPIO_FUNC_INPUT_LOW_LEVEL_TRIG_AUTO_CONVERT     = 0x03,
        GPIO_FUNC_INPUT_32KHZ_CLK                       = 0x04,
        GPIO_FUNC_OUTPUT_PMIC_CMD                       = 0x05,
        GPIO_FUNC_OUTPUT_LOW                            = 0x06,
        GPIO_FUNC_OUTPUT_HIGH                           = 0x07,
        GPIO_FUNC_OUTPUT_32KHZ_CLK                      = 0x08,
        GPIO_FUNC_OUTPUT_4MHZ_CLK                       = 0x09,
        GPIO_FUNC_OUTPUT_SYSTEM_DEBUG_MONITOR           = 0x0D,
        GPIO_FUNC_OUTPUT_FRAME_ACTIVE_MONITOR           = 0x0E,
        GPIO_FUNC_OUTPUT_SLOT_TIMING_MONITOR            = 0x0F,
        GPIO_FUNC_NUM
    }                   gpio_func;

    bool                b_pull_res_enabled;
    uint8_t             u8_gpio_out_clk_div_ratio;

    enum
    {
        GPIO_TS_OUTPUT_SLOT_A                       = 0x00,
        GPIO_TS_OUTPUT_SLOT_B,
        GPIO_TS_OUTPUT_SLOT_C,
        GPIO_TS_OUTPUT_SLOT_D,
        GPIO_TS_OUTPUT_SLOT_E,
        GPIO_TS_OUTPUT_SLOT_F,
        GPIO_TS_OUTPUT_SLOT_G,
        GPIO_TS_OUTPUT_SLOT_H,
        GPIO_TS_OUTPUT_SLOT_ALL,
        GPIO_TS_OUTPUT_SLOT_NUM
    }                   slot_timing_output_idx_filter;

    enum
    {
        GPIO_TS_OUTPUT_ACTIVE                       = 0x01,
        GPIO_TS_OUTPUT_DURATION                     = 0x02,
        GPIO_TS_OUTPUT_PMIC                         = 0x04,
        GPIO_TS_OUTPUT_AACM                         = 0x08,
        GPIO_TS_OUTPUT_DARK0                        = 0x10,
        GPIO_TS_OUTPUT_LIGHT                        = 0x20,
        GPIO_TS_OUTPUT_DARK1                        = 0x40,
        GPIO_TS_OUTPUT_LED_ON                       = 0x80
    }                   slot_timing_output_event_filter;

    enum
    {
        GPIO_SYSTEM_DEBUG_OUTPUT_D2A_OSC_4M_PD          = 0x00,
        GPIO_SYSTEM_DEBUG_OUTPUT_A2D_CLK_4M,
        GPIO_SYSTEM_DEBUG_OUTPUT_PMU_CLK_4M_OUT,
        GPIO_SYSTEM_DEBUG_OUTPUT_A2D_DVDD2_LDO_EMPOWER,
        GPIO_SYSTEM_DEBUG_OUTPUT_D2A_AVDD2_LDO_PD,
        GPIO_SYSTEM_DEBUG_OUTPUT_A2D_AVDD2_LDO_RDY,
        GPIO_SYSTEM_DEBUG_OUTPUT_D2A_CHA_ADC_RST_N,
        GPIO_SYSTEM_DEBUG_OUTPUT_D2A_CHB_ADC_RST_N,
        GPIO_SYSTEM_DEBUG_OUTPUT_A2D_CHA_ADC_DATA_VALID,
        GPIO_SYSTEM_DEBUG_OUTPUT_A2D_CHB_ADC_DATA_VALID,
        GPIO_SYSTEM_DEBUG_OUTPUT_D2A_CHA_TIA_PD,
        GPIO_SYSTEM_DEBUG_OUTPUT_D2A_CHB_TIA_PD,
        GPIO_SYSTEM_DEBUG_OUTPUT_D2A_LED_A_PD,
        GPIO_SYSTEM_DEBUG_OUTPUT_D2A_LED_B_PD,
        GPIO_SYSTEM_DEBUG_OUTPUT_D2A_INPUT_VCM0_PD,
        GPIO_SYSTEM_DEBUG_OUTPUT_D2A_INPUT_VCM1_PD,
        GPIO_SYSTEM_DEBUG_OUTPUT_NUM
    }                   sys_debug_output;

} VT2102GPIOConfigItems;

VT2102_FUNC_EXPORT void VT2102_ConfigGPIO( const VT2102GPIOConfigItems *p_items );
VT2102_FUNC_EXPORT void VT2102_GetGPIOConfig( VT2102GPIOConfigItems *p_items );

typedef enum
{
    GPIO_LEVEL_LOW      = 0x00,
    GPIO_LEVEL_HIGH,
    GPIO_LEVEL_NUM
} GPIOLevel;

VT2102_FUNC_EXPORT GPIOLevel VT2102_ReadGPIODigitalInput( void );
VT2102_FUNC_EXPORT uint8_t VT2102_GetChipID( void );
VT2102_FUNC_EXPORT void VT2102_SoftReset( void );

typedef enum
{
    VT2102_CONV_MODE_AUTO               = 0x01,
    VT2102_CONV_MODE_ONCE               = 0x02,
    VT2102_CONV_MODE_ONCE_WITH_REFRESH  = 0x08 | VT2102_CONV_MODE_ONCE,
    VT2102_CONV_MODE_GPIO               = 0x04,
} VT2102ConvsionMode;

VT2102_FUNC_EXPORT void VT2102_StartConversion( VT2102ConvsionMode mode );
VT2102_FUNC_EXPORT void VT2102_StopConversion( void );
VT2102_FUNC_EXPORT bool VT2102_IsConversionCompleted( void );
VT2102_FUNC_EXPORT bool VT2102_IsTimeSlotActive( void );
VT2102_FUNC_EXPORT void VT2102_SetI2CDeadLockEnabled( bool b_enabled, uint16_t u16_timeout );

#ifdef __cplusplus
}
#endif

#endif /* VT2102_H__ */
