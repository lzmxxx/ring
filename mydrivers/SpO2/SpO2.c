/**
 * @file    SpO2.c
 * @brief   连续151 taps FIR预处理
 * @note    FIR状态跨8秒分析窗口持续保持；逐搏与FFT计算实现在 SpO2_advanced.c。
 */
#include "SpO2.h"
#include "arm_math.h"

#define FIR_TAPS        151U









static arm_fir_instance_f32 red_fir_instance, ir_fir_instance;
static float red_fir_state[FIR_TAPS], ir_fir_state[FIR_TAPS];
static uint8_t fir_initialized;




/*
 * 75.027Hz -> 25.009Hz 三倍抽取带通：151 taps，Kaiser(beta=5.65)，0.3~5Hz。
 * 截止频率定义在约-6dB处；12.5Hz处约-89.0dB，群延迟约1.0秒。
 * 系数线性相位且对称，CMSIS按时间反序读取时数值序列保持一致。
 */
/* 此版 CMSIS-DSP 的 arm_fir_init_f32 系数形参不是 const，故保留可写类型。 */
static float fir_coeffs[FIR_TAPS] = {
#include "SpO2_fir_0p3_5Hz.inc"
};

void SpO2_FilterReset(void)
{
    arm_fir_init_f32(&red_fir_instance, FIR_TAPS, fir_coeffs, red_fir_state, 1U);
    arm_fir_init_f32(&ir_fir_instance, FIR_TAPS, fir_coeffs, ir_fir_state, 1U);
    fir_initialized = 0U;
}

void SpO2_FilterSample(float red_raw, float ir_raw, float *red_filtered, float *ir_filtered)
{
    uint16_t i;

    if (!fir_initialized) {
        SpO2_FilterReset();
        /* 用首帧填充历史状态，只在整次采集开始时抑制一次启动阶跃。 */
        for (i = 0U; i < FIR_TAPS - 1U; ++i) {
            red_fir_state[i] = red_raw;
            ir_fir_state[i] = ir_raw;
        }
        fir_initialized = 1U;
    }
    arm_fir_f32(&red_fir_instance, &red_raw, red_filtered, 1U);
    arm_fir_f32(&ir_fir_instance, &ir_raw, ir_filtered, 1U);
}


/* 旧版整窗降采样、峰值和FFT算法已删除，避免重复计算与静态RAM占用。 */
