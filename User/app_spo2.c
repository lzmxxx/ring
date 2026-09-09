/**
 * @file    app_spo2.c
 * @brief   血氧算法后台处理任务实现
 * 
 * 模块职责：
 * 1. 管理专用的算法工作线程 `spo2`（优先级 10，栈深 4096 字节）；
 * 2. 阻塞等待来自采集任务的算法作业消息（`PPG_Job`）；
 * 3. 从 PPG 环形缓冲区提取连续 600 帧（8 秒窗口）的双波长波形数据；
 * 4. 调用 `SpO2_Calculate()` 执行25Hz降采样逐搏配对解算；
 * 5. 将每次计算结果投递给 BLE 任务队列供蓝牙上报。
 * 
 * 与其他模块交互：
 * - 接收来自 `app_ppg.c` 的 `spo2_job_mq` 消息驱动；
 * - 计算期间持有 `app_power` 的 `PM_ALGO` 低功耗锁，防止 CPU 在重度浮点运算时误入休眠；
 * - 计算完毕后调用 `app_ble_post()` 将结果安全推送到蓝牙发送队列。
 * 
 * 线程安全与数据一致性设计：
 * - 采用 `epoch` 代数机制：当硬件 FIFO 溢出、DMA 传输异常或丢包导致断流时，采集任务会递增 epoch；
 * - 环形缓冲区复制时校验 job 的 epoch，确保输入窗口可读取；信号质量由上位机判断。
 */

#include "SpO2.h"
#include "app_ppg.h"
#include "app_ble.h"
#include "app_power.h"
#include "app_config.h"
#include "app_device.h"
#include "app_record.h"
#include "SC7A20.h"
#include <string.h>

/* 全局最新算法解算结果快照（供调试观测与其它模块查询） */
volatile SpO2_Result SpO2_Latest;

/* 供 SWD/MKLink 观测：窗口复制因流式写入而失效的次数。 */
volatile uint32_t SpO2_CopyFailures;

/* 线程专用的静态工作缓冲区（600 帧数据，避免多线程重入） */
static PPG_Frame working_frame_buffer[PPG_WINDOW];
static SpO2_Result last_reported_result;
static rt_bool_t has_last_reported_result = RT_FALSE;

/* 与 ppg_validator.algorithms.heart_rate.HeartRateStabilizer 保持一致。
 * 采集任务每秒送来一个滑动窗口，故异常保持按窗口数计时。 */
#define HR_CONFIRM_WINDOWS        2U
#define HR_MAX_ADJACENT_DIFF_BPM  5.0f
#define HR_SMOOTH_OLD_WEIGHT      0.70f
#define HR_INVALID_HOLD_WINDOWS   10U

static float hr_last_candidate;
static float hr_smoothed;
static float hr_last_confirmed;
static uint8_t hr_consecutive_count;
static uint8_t hr_invalid_windows;
static rt_bool_t hr_has_candidate = RT_FALSE;
static rt_bool_t hr_has_smoothed = RT_FALSE;
static rt_bool_t hr_has_confirmed = RT_FALSE;

static float abs_float(float value)
{
    return (value < 0.0f) ? -value : value;
}

static void stabilize_heart_rate(SpO2_Result *result)
{
    float candidate = result->hr_time;
    rt_bool_t candidate_valid = (result->time_valid &&
                                 candidate >= 30.0f && candidate <= 200.0f);

    if (!candidate_valid)
    {
        hr_has_candidate = RT_FALSE;
        hr_consecutive_count = 0U;
        if (hr_invalid_windows < 0xFFU)
        {
            hr_invalid_windows++;
        }
        result->hr = (hr_has_confirmed &&
                      hr_invalid_windows <= HR_INVALID_HOLD_WINDOWS)
                         ? hr_last_confirmed : 0.0f;
        return;
    }

    hr_invalid_windows = 0U;
    if (!hr_has_candidate ||
        abs_float(candidate - hr_last_candidate) <= HR_MAX_ADJACENT_DIFF_BPM)
    {
        if (hr_consecutive_count < HR_CONFIRM_WINDOWS)
        {
            hr_consecutive_count++;
        }
    }
    else
    {
        /* 突变候选从头确认，期间保持已确认的显示值。 */
        hr_consecutive_count = 1U;
        hr_has_smoothed = RT_FALSE;
    }
    hr_last_candidate = candidate;
    hr_has_candidate = RT_TRUE;

    if (hr_consecutive_count < HR_CONFIRM_WINDOWS)
    {
        result->hr = hr_has_confirmed ? hr_last_confirmed : 0.0f;
        return;
    }

    if (!hr_has_smoothed)
    {
        hr_smoothed = candidate;
        hr_has_smoothed = RT_TRUE;
    }
    else
    {
        hr_smoothed = HR_SMOOTH_OLD_WEIGHT * hr_smoothed +
                      (1.0f - HR_SMOOTH_OLD_WEIGHT) * candidate;
    }
    hr_last_confirmed = hr_smoothed;
    hr_has_confirmed = RT_TRUE;
    result->hr = hr_last_confirmed;
}

/* RT-Thread 静态线程控制块与静态任务栈 */
static struct rt_thread spo2_thread;
ALIGN(RT_ALIGN_SIZE) static rt_uint8_t spo2_thread_stack[APP_SPO2_TASK_STACK_SIZE];

/**
 * @brief  SpO2 算法工作线程主体函数
 * @param  parameter 线程入参（未使用）
 */
static void spo2_thread_entry(void *parameter)
{
    PPG_Job     received_job;
    SpO2_Result calculated_result;

    (void)parameter;

    while (1)
    {
        /* 1. 阻塞等待采集任务发送的算法计算作业 */
        if (rt_mq_recv(&spo2_job_mq, &received_job, sizeof(received_job), RT_WAITING_FOREVER) != RT_EOK)
        {
            continue;
        }

        /* 若系统正处于关机流程，则跳过后续计算 */
        if (app_shutdown)
        {
            continue;
        }
        if (!app_device_algorithm_required())
        {
            continue;
        }

        /* 2. 持有算法低功耗锁，阻止系统在密集计算期间进入深度停机模式 */
        app_power_lock(PM_ALGO);

        memset(&calculated_result, 0, sizeof(calculated_result));
        calculated_result.seq = received_job.end_seq;

        /* 3. 从 PPG 环形缓冲区中提取对应的 600 帧连续数据 */
        if (app_ppg_copy(&received_job, working_frame_buffer) == RT_EOK)
        {
            /* 4. 执行降采样逐搏配对血氧心率计算 */
            SpO2_Calculate(working_frame_buffer, &calculated_result);
            stabilize_heart_rate(&calculated_result);
            last_reported_result = calculated_result;
            has_last_reported_result = RT_TRUE;
        }

        else
        {
            /* 环形缓冲区在复制时被采集线程推进，不能把清零的临时结构体
             * 当作一次算法结果发送。保留最近一次完整窗口的数值，仅更新序号。 */
            SpO2_CopyFailures++;
            if (has_last_reported_result)
            {
                calculated_result = last_reported_result;
                calculated_result.seq = received_job.end_seq;
            }
        }

        calculated_result.timestamp = app_device_rtc_now();
        calculated_result.motion = SC7A20_GetAndClearMotion();
        calculated_result.temperature = 0;
        calculated_result.quality = (calculated_result.time_valid && calculated_result.fft_valid) ? 100U :
                                    ((calculated_result.time_valid || calculated_result.fft_valid) ? 60U : 20U);
        calculated_result.valid = (calculated_result.time_valid || calculated_result.fft_valid) ? 1U : 0U;

        /* 5. 更新全局快照 */
        SpO2_Latest = calculated_result;

        /* 6. 无论信号质量如何都投递，由上位机完成有效性判断 */
        if (!app_shutdown)
        {
            app_ble_post(&calculated_result);
            app_record_post_result(&calculated_result);
        }

        /* 7. 释放算法低功耗锁，允许 CPU 在计算完毕后重新进入休眠 */
        app_power_unlock(PM_ALGO);
    }
}

/**
 * @brief  初始化并启动血氧算法线程
 * @return 0 成功，-1 失败
 */
int app_spo2_init(void)
{
    /* 初始化静态线程：优先级 10，时间片 10 个 tick */
    if (rt_thread_init(&spo2_thread, "spo2", spo2_thread_entry, RT_NULL,
                       spo2_thread_stack, sizeof(spo2_thread_stack),
                       APP_SPO2_TASK_PRIORITY, APP_SPO2_TASK_TIMESLICE) != RT_EOK)
    {
        return -1;
    }

    if (rt_thread_startup(&spo2_thread) != RT_EOK)
    {
        return -1;
    }

    return 0;
}
