/**
 * @file    SpO2.h
 * @brief   脉搏血氧与心率生理算法头文件
 * @details 连续151 taps FIR仅在采集会话开始时初始化；600点窗口每75点滑动一次，
 *          分别执行MSPTDfast逐搏法与独立FFT法。
 */

#ifndef __SPO2_H__
#define __SPO2_H__

#include "app_ppg.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 血氧心率生理算法输出结果结构体
 */
typedef struct
{
    uint32_t seq;         /**< 对应分析窗口末尾的最新采样帧序列号 (PPG_TotalFrames) */
    uint32_t timestamp;   /**< RTC Unix 时间戳 */
    float    hr;          /**< 协议主心率：时域候选经跨窗口确认与平滑后的显示值（BPM） */
    float    spo2;        /**< 血氧饱和度估算值（单位：%，由 R 比值插值获得） */
    float    pi;          /**< 灌注指数 (Perfusion Index，单位：%)，反映脉搏搏动强度 */
    float    ratio;       /**< 红光与红外交流/直流调制比值 R = (AC_red/DC_red) / (AC_ir/DC_ir) */
    float    hr_time;     /**< 905nm峰间距独立计算的时域心率（单位：BPM） */
    float    hr_fft;      /**< 905nm波形独立FFT主频心率（单位：BPM） */
    float    correlation; /**< 660nm 红光与 905nm 红外脉搏波形归一化互相关系数 (-1.0 ~ 1.0) */
    float    spo2_time;    /**< MSPTDfast逐搏AC/DC方法血氧（%） */
    float    spo2_fft;     /**< 独立FFT方法血氧（%） */
    /* 以下三个字段仅维持既有 BLE 0xA5/0x05 帧的字节布局，固件不再计算 DST，恒为 0。 */
    float    spo2_dst;     /**< 保留协议槽位，恒为0 */
    float    ratio_time;   /**< 逐搏AC/DC方法R值 */
    float    ratio_fft;    /**< FFT方法R值 */
    float    ratio_dst;    /**< 保留协议槽位，恒为0 */
    float    dst_prominence; /**< 保留协议槽位，恒为0 */
    uint8_t  time_pair_count; /**< 逐搏R汇总采用的有效搏动数量 */
    uint8_t  time_valid;   /**< 时域方法本窗口有效 */
    uint8_t  fft_valid;    /**< FFT方法本窗口有效 */
    uint8_t  dst_valid;    /**< 保留协议标志位，恒为0 */
    uint8_t  valid;       /**< 数据产生标志：算法周期完成后固定为1，质量判断由上位机执行 */
    uint8_t  calibrated;  /**< 标定确认标志位：0 未标定，1 已完成临床或黄金对照标准标定 */
    uint8_t  motion;      /**< 本结果周期内运动事件聚合值 */
    uint8_t  quality;     /**< 0~100 综合质量提示 */
    int16_t  temperature; /**< 预留体温，0.01摄氏度；V1固定为0 */
} SpO2_Result;

/**
 * @brief  输入连续FIR输出及原始PPG，执行时域与FFT两种独立血氧算法
 * @param  frames 输入的 600 帧 PPG_Frame 数据缓冲区首地址（包含 red 与 ir 信号）
 * @param  result 输出的算法解算结果指针
 * @note   1. 内部占用静态工作内存，仅供单个算法线程上下文独占调用；
 *         2. 不使用动态内存，各算法独立返回结果与有效标志；
 *         3. valid固定表示本周期已经计算，上位机根据独立字段决定如何显示。
 */
void SpO2_Calculate(const PPG_Frame *frames, SpO2_Result *result);

/** @brief 重置两路75Hz连续FIR状态；仅在新采集会话或断流时调用。 */
void SpO2_FilterReset(void);

/** @brief 向常驻FIR送入一帧原始双波长数据并返回连续滤波结果。 */
void SpO2_FilterSample(float red_raw, float ir_raw, float *red_filtered, float *ir_filtered);

#ifdef __cplusplus
}
#endif

#endif /* __SPO2_H__ */
