/**
 * @file    app_ppg.c
 * @brief   PPG 数据采集与环形缓冲区管理任务实现
 * 
 * 模块职责：
 * 1. 管理专用的 PPG 采集工作线程 `ppg`（优先级 6，高于算法与普通任务）；
 * 2. 由 PB3 / EXTI3 下降沿（IPA1322 硬件 FIFO 水线中断）触发唤醒，坚决杜绝低效轮询；
 * 3. 驱动 I2C1 RX DMA1 Channel 7 进行 64 entry 硬件 FIFO 的零 CPU 负荷批量读取；
 * 4. 维护 768 深度的大容量环形缓冲区，将连续波形切分为 600 帧（8 秒分析窗口），
 *    每滑动 75 帧（约 1 秒）产生一个 `PPG_Job` 任务通过消息队列派发给算法任务；
 * 5. 健全的断流熔断机制（`epoch` 计数）：当发生 FIFO 满溢、DMA 超时或波长 Tag 失步时，
 *    立即作废当前滑动窗口并递增 `epoch`，保证算法绝不将断流前后的拼凑数据作为有效脉搏计算。
 * 
 * 与其他模块交互：
 * - 底层调用 `IPA1322.c` 与 `bsp_hw_i2c.c`；
 * - 向上通过 `spo2_job_mq` 消息队列驱动 `app_spo2.c`；
 * - 受 `app_ble.c` 连接状态驱动（连接成功时开采，断开时停采省电）；
 * - 采集与 DMA 期间受 `app_power` 的 `PM_FIFO` / `PM_DMA` 锁保护。
 * 
 * 中断与实时性考量：
 * - PB3 EXTI3 中断优先级配置为 Preemption 1, Sub 1；
 * - DMA1 Channel 7 中断优先级配置为 Preemption 1, Sub 2；
 * - 两者 ISR 均极为简短（< 1us），绝不执行任何耗时逻辑。
 */

#include "app_ppg.h"
#include "app_power.h"
#include "app_config.h"
#include "app_device.h"
#include "app_ble.h"
#include "SpO2.h"
#include "IPA1322.h"
#include "bsp_hw_i2c.h"
#include "bsp_power.h"
#include "CW2015.h"
#include "SC7A20.h"
#include "n32wb452.h"
#include "n32wb452_dma.h"
#include "n32wb452_exti.h"
#include "n32wb452_gpio.h"
#include "n32wb452_rcc.h"
#include "misc.h"
#include <rthw.h>

#define IPA_INT_LINE        EXTI_LINE3
#define DMA_TIMEOUT_MS      20U

/**
 * @brief 环形缓冲项结构体
 */
typedef struct
{
    PPG_Frame frame; /**< 包含红光与红外的采样帧 */
} RingEntry;

/* 环形缓冲区及读写索引变量 */
static RingEntry ring_buffer[PPG_RING_SIZE];
static uint16_t  write_index    = 0U;
static uint16_t  valid_count    = 0U;
static uint16_t  pending_frames = 0U;
static uint32_t  current_epoch  = 0U;
static uint32_t  next_job_end   = 0U;
static uint16_t  session_frames = 0U;
static rt_bool_t period_job_sent = RT_FALSE;
static rt_bool_t sensor_domain_on = RT_FALSE;

/* DMA 批量读取的原始与解析缓冲区 */
static uint8_t      dma_raw_buffer[IPA1322_FIFO_DEPTH * IPA1322_FIFO_ENTRY_BYTES];
static IPA1322_Data parsed_pairs_buffer[IPA1322_FIFO_DEPTH / 2U];

/* RT-Thread 同步与通信对象 */
static struct rt_semaphore   fifo_sem;
static struct rt_semaphore   dma_sem;
struct rt_messagequeue       spo2_job_mq;
static uint8_t               job_pool[APP_PPG_JOB_QUEUE_DEPTH * (sizeof(PPG_Job) + sizeof(void *))];

/* 任务线程控制块与静态任务栈 */
static struct rt_thread      ppg_thread_handle;
ALIGN(RT_ALIGN_SIZE) static rt_uint8_t ppg_thread_stack[APP_PPG_TASK_STACK_SIZE];

/* 全局统计变量 */
volatile uint32_t  PPG_TotalFrames;
volatile uint32_t  PPG_Gaps;
volatile uint32_t  PPG_QueueDrops;
volatile uint32_t  PPG_FifoIrqCount;
volatile uint32_t  PPG_FifoReadCount;
volatile rt_bool_t PPG_Streaming;
volatile uint32_t  PPG_InitAttempts;
volatile uint32_t  PPG_InitFailures;
volatile uint32_t  PPG_FifoOverflowCount;

/* 内部请求开关与私有函数声明 */
static volatile rt_bool_t ppg_requested = RT_FALSE;
static void intb_init(void);
extern volatile uint8_t g_cw2015_init_status;

/**
 * @brief  作废当前正在积累的分析窗口（断流处理）
 * @note   在临界区保护下递增 epoch 并清零待处理计数值，
 *         使得此前已排队或计算中的 job 在回检时均被判定为陈旧失效。
 */
static void invalidate_window(void)
{
    rt_base_t interrupt_level = rt_hw_interrupt_disable();

    current_epoch++;
    valid_count    = 0U;
    pending_frames = 0U;
    next_job_end   = PPG_TotalFrames;

    rt_hw_interrupt_enable(interrupt_level);
    SpO2_FilterReset();
}

/**
 * @brief  将新解析的双波长数据存入环形缓冲区并生成算法作业
 * @param  data  已配对的数据对数组
 * @param  count 数据对条数
 */
static void store_pairs(const IPA1322_Data *data, uint8_t count)
{
    for (uint8_t i = 0U; i < count; i++)
    {
        float red_fir, ir_fir;

        SpO2_FilterSample((float)data[i].red_signal, (float)data[i].ir_signal,
                          &red_fir, &ir_fir);
        PPG_TotalFrames++;

        /* 1. 写入环形缓冲区 */
        ring_buffer[write_index].frame.red = data[i].red_signal;
        ring_buffer[write_index].frame.ir  = data[i].ir_signal;
        ring_buffer[write_index].frame.red_fir = red_fir;
        ring_buffer[write_index].frame.ir_fir  = ir_fir;
        write_index = (uint16_t)((write_index + 1U) % PPG_RING_SIZE);

        if (valid_count < PPG_RING_SIZE)
        {
            valid_count++;
        }
        pending_frames++;
        if (session_frames < 0xFFFFU) session_frames++;

        /* 原始模式只组包当前数据，不保留额外调试镜像。 */
        if (app_device_raw_required())
        {
            app_ble_post_raw(data[i].red_signal, data[i].ir_signal, PPG_TotalFrames);
        }

        /* 连续模式每秒提交一次算法作业。 */
        while (app_device_algorithm_required() && !app_device_periodic_mode() &&
               pending_frames >= PPG_STEP)
        {
            PPG_Job job;

            next_job_end += PPG_STEP;
            job.end_seq   = next_job_end;
            job.epoch     = current_epoch;

            /* 将作业压入消息队列通知算法线程 */
            if (rt_mq_send(&spo2_job_mq, &job, sizeof(job)) != RT_EOK)
            {
                PPG_QueueDrops++;
            }

            pending_frames -= PPG_STEP;
        }

        /* 周期模式在10秒采集窗结束时只提交最后600帧，确保每周期仅一条记录。 */
        if (app_device_algorithm_required() && app_device_periodic_mode() &&
            !period_job_sent && session_frames >= (PPG_STEP * PERIOD_ACQUIRE_SECONDS))
        {
            PPG_Job job;
            job.end_seq = PPG_TotalFrames;
            job.epoch = current_epoch;
            if (rt_mq_send(&spo2_job_mq, &job, sizeof(job)) != RT_EOK) PPG_QueueDrops++;
            period_job_sent = RT_TRUE;
        }
    }
}

static rt_bool_t start_sampling(void)
{
    uint8_t attempt;

    if (!sensor_domain_on)
    {
        bsp_sensor_power_on();
        rt_thread_mdelay(SENSOR_POWER_STABLE_MS);
        g_cw2015_init_status = CW2015_Init();
        if (SC7A20_Init() != 0U) app_device_set_error(APP_ERR_ACCEL_INIT);
        sensor_domain_on = RT_TRUE;
    }
    else
    {
        bsp_afe_power_on();
        rt_thread_mdelay(SENSOR_POWER_STABLE_MS);
    }
    app_device_set_sensor_state(RT_TRUE);
    session_frames = 0U;
    period_job_sent = RT_FALSE;
    invalidate_window();
    for (attempt = 0U; attempt < 3U; attempt++)
    {
        PPG_InitAttempts++;
        if (IPA1322_Init() == 0U) break;

        PPG_InitFailures++;
        rt_thread_mdelay(100U);
    }

    if (attempt == 3U)
    {
        app_device_set_error(APP_ERR_IPA_INIT);
        ppg_requested = RT_FALSE;
        return RT_FALSE;
    }

    intb_init();
    if (IPA1322_Start() != 0U)
    {
        ppg_requested = RT_FALSE;
        return RT_FALSE;
    }

    PPG_Streaming = RT_TRUE;
    if (GPIO_ReadInputDataBit(GPIOB, GPIO_PIN_3) == Bit_RESET)
    {
        rt_sem_release(&fifo_sem);
    }
    return RT_TRUE;
}

static rt_bool_t read_fifo_once(void)
{
    uint8_t entries;
    uint8_t pair_count;

    if ((IPA1322_ReadInterruptFlags() & 0x20U) != 0U)
    {
        PPG_Gaps++;
        PPG_FifoOverflowCount++;
        app_device_set_error(APP_ERR_FIFO_OVERFLOW);
        IPA1322_ClearFIFO();
        invalidate_window();
        return RT_FALSE;
    }

    entries = IPA1322_GetFIFOCount();
    if ((entries == 0U) || (entries > IPA1322_FIFO_DEPTH))
    {
        return RT_FALSE;
    }

    app_power_lock(PM_DMA);
    if (!bsp_hw_i2c_dma_read_start(0x14U, dma_raw_buffer,
                                    (uint16_t)entries * IPA1322_FIFO_ENTRY_BYTES) ||
        (rt_sem_take(&dma_sem, rt_tick_from_millisecond(DMA_TIMEOUT_MS)) != RT_EOK))
    {
        if (bsp_hw_i2c_dma_busy()) bsp_hw_i2c_dma_abort();
        app_power_unlock(PM_DMA);

        PPG_Gaps++;
        IPA1322_ClearFIFO();
        invalidate_window();
        return RT_FALSE;
    }
    app_power_unlock(PM_DMA);
    PPG_FifoReadCount++;

    pair_count = IPA1322_ParseFIFO(dma_raw_buffer, entries, parsed_pairs_buffer,
                                    IPA1322_FIFO_DEPTH / 2U);
    if (pair_count > 0U)
    {
        store_pairs(parsed_pairs_buffer, pair_count);
    }
    else if (entries >= 2U)
    {
        PPG_Gaps++;
        invalidate_window();
    }

    return RT_TRUE;
}

/**
 * @brief  PPG 采集线程主体循环
 */
static void ppg_thread_entry(void *parameter)
{
    (void)parameter;

    while (1)
    {
        (void)rt_sem_take(&fifo_sem, RT_WAITING_FOREVER);
        if (app_shutdown) continue;

        if (!ppg_requested)
        {
            if (PPG_Streaming) app_ppg_stop();
            continue;
        }

        if (!PPG_Streaming)
        {
            (void)start_sampling();
            continue;
        }

        app_power_lock(PM_FIFO);
        while (!app_shutdown && ppg_requested && read_fifo_once()) { }
        app_power_unlock(PM_FIFO);
    }
}

/**
 * @brief  安全拷贝作业对应的 600 帧连续数据
 * @param  job 作业句柄
 * @param  out 目标缓冲区（传 NULL 时仅校验有效性）
 * @return RT_EOK 成功，-RT_ERROR 失败
 */
int app_ppg_copy(const PPG_Job *job, PPG_Frame *out)
{
    uint32_t first_seq;
    uint32_t current_seq;
    uint16_t ring_idx;
    uint16_t i;
    rt_err_t status = RT_EOK;

    if (!job)
    {
        return -RT_ERROR;
    }

    /* ring_buffer、PPG_TotalFrames 与 current_epoch 均只由 ppg 线程写入。
     * 锁定调度器可避免高优先级 ppg 线程在“校验→复制”之间推进窗口或作废 epoch；
     * PB3/DMA ISR 仍可运行，只会累积信号量，不会在此处改写缓冲区。 */
    rt_enter_critical();

    if ((job->epoch != current_epoch) || (job->end_seq > PPG_TotalFrames) ||
        (job->end_seq < PPG_WINDOW) || ((PPG_TotalFrames - job->end_seq) >= PPG_RING_SIZE))
    {
        status = -RT_ERROR;
    }
    else if (out)
    {
        first_seq = job->end_seq - PPG_WINDOW + 1U;

        for (i = 0U; i < PPG_WINDOW; i++)
        {
            current_seq = first_seq + i;
            ring_idx    = (uint16_t)((current_seq - 1U) % PPG_RING_SIZE);

            out[i] = ring_buffer[ring_idx].frame;
        }
    }

    if (status == RT_EOK && job->epoch != current_epoch)
    {
        status = -RT_ERROR;
    }

    rt_exit_critical();
    return status;
}

/**
 * @brief  初始化 PB3 / EXTI3 下降沿水线中断
 */
static void intb_init(void)
{
    GPIO_InitType gpio_init_struct;
    EXTI_InitType exti_init_struct;
    NVIC_InitType nvic_init_struct;

    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOB | RCC_APB2_PERIPH_AFIO, ENABLE);

    /* 释放 PB3 上的 JTAG 引脚复用，保留 SWD 调试功能 */
    GPIO_ConfigPinRemap(GPIO_RMP_SW_JTAG_SW_ENABLE, ENABLE);

    /* 配置 PB3 为内部上拉输入 (IPA1322 INTB 为开漏输出，板上无外部上拉电阻) */
    GPIO_InitStruct(&gpio_init_struct);
    gpio_init_struct.Pin        = GPIO_PIN_3;
    gpio_init_struct.GPIO_Mode  = GPIO_Mode_IPU;
    gpio_init_struct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(GPIOB, &gpio_init_struct);

    /* 配置 EXTI Line 3 路由到 PB3 */
    GPIO_ConfigEXTILine(GPIOB_PORT_SOURCE, GPIO_PIN_SOURCE3);
    EXTI_ClrITPendBit(IPA_INT_LINE);

    /* 配置为下降沿中断 */
    EXTI_InitStruct(&exti_init_struct);
    exti_init_struct.EXTI_Line    = IPA_INT_LINE;
    exti_init_struct.EXTI_Mode    = EXTI_Mode_Interrupt;
    exti_init_struct.EXTI_Trigger = EXTI_Trigger_Falling;
    exti_init_struct.EXTI_LineCmd = ENABLE;
    EXTI_InitPeripheral(&exti_init_struct);

    /* 配置 NVIC 抢占优先级 1，子优先级 1 */
    nvic_init_struct.NVIC_IRQChannel                   = EXTI3_IRQn;
    nvic_init_struct.NVIC_IRQChannelPreemptionPriority = 1U;
    nvic_init_struct.NVIC_IRQChannelSubPriority        = 1U;
    nvic_init_struct.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic_init_struct);
}

/**
 * @brief  初始化 PPG 采集模块
 */
int app_ppg_init(void)
{
    if ((rt_sem_init(&fifo_sem, "ipa", 0, RT_IPC_FLAG_FIFO) != RT_EOK) ||
        (rt_sem_init(&dma_sem, "dma", 0, RT_IPC_FLAG_FIFO) != RT_EOK) ||
        (rt_mq_init(&spo2_job_mq, "spo2q", job_pool, sizeof(PPG_Job),
                    sizeof(job_pool), RT_IPC_FLAG_FIFO) != RT_EOK))
    {
        return -1;
    }

    next_job_end  = 0U;
    ppg_requested = RT_FALSE;
    PPG_Streaming = RT_FALSE;

    if (rt_thread_init(&ppg_thread_handle, "ppg", ppg_thread_entry, RT_NULL,
                       ppg_thread_stack, sizeof(ppg_thread_stack),
                       APP_PPG_TASK_PRIORITY, APP_PPG_TASK_TIMESLICE) != RT_EOK)
    {
        return -1;
    }

    if (rt_thread_startup(&ppg_thread_handle) != RT_EOK)
    {
        return -1;
    }

    return 0;
}

/**
 * @brief  通知蓝牙连接状态变化
 */
void app_ppg_set_connected(rt_bool_t connected)
{
    app_ppg_set_enabled(connected);
}

void app_ppg_set_enabled(rt_bool_t enabled)
{
    ppg_requested = enabled;
    rt_sem_release(&fifo_sem);
}

/**
 * @brief  PB3 水满中断通知
 */
void app_ppg_irq(void)
{
    PPG_FifoIrqCount++;
    rt_sem_release(&fifo_sem);
}

/**
 * @brief  查询采集是否空闲（用于低功耗判断）
 */
rt_bool_t app_ppg_idle(void)
{
    return (!PPG_Streaming) ||
           ((!bsp_hw_i2c_dma_busy()) && (GPIO_ReadInputDataBit(GPIOB, GPIO_PIN_3) == Bit_SET));
}

/**
 * @brief  停止采样
 */
void app_ppg_stop(void)
{
    EXTI_InitType exti_init_struct;

    ppg_requested = RT_FALSE;

    /* 禁用 PB3 中断 */
    EXTI_InitStruct(&exti_init_struct);
    exti_init_struct.EXTI_Line    = IPA_INT_LINE;
    exti_init_struct.EXTI_LineCmd = DISABLE;
    EXTI_InitPeripheral(&exti_init_struct);

    if (bsp_hw_i2c_dma_busy())
    {
        bsp_hw_i2c_dma_abort();
    }

    if (PPG_Streaming)
    {
        IPA1322_Stop();
        IPA1322_ClearFIFO();
    }

    PPG_Streaming = RT_FALSE;
    invalidate_window();

    if (app_device_session_required() && app_device_periodic_mode() &&
        (app_device_algorithm_required() || app_device_raw_required()))
    {
        /* 周期等待期间保留加速度计的低功耗运动中断，只关闭光学AFE。 */
        bsp_afe_power_off();
    }
    else
    {
        SC7A20_DeInit();
        bsp_sensor_power_off();
        sensor_domain_on = RT_FALSE;
        app_device_set_sensor_state(RT_FALSE);
    }
    rt_sem_release(&fifo_sem);
}

void app_ppg_dma_irq(void)
{
    if (DMA_GetIntStatus(DMA1_INT_TXC7, DMA1) != RESET)
    {
        bsp_hw_i2c_dma_finish_isr();
        rt_sem_release(&dma_sem);
    }
    else
    {
        DMA_ClrIntPendingBit(DMA1_INT_GLB7, DMA1);
        bsp_hw_i2c_dma_abort();
    }
}
