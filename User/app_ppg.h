/**
 * @file    app_ppg.h
 * @brief   PPG 数据采集与环形缓冲区管理头文件
 * @details 负责管理基于硬件中断与 DMA 的双波长 PPG 高速采集流水线，维护 768 深度环形缓冲并分发算法计算作业。
 */

#ifndef __APP_PPG_H__
#define __APP_PPG_H__

#include <rtthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 单帧双通道 PPG 脉搏波信号
 */
typedef struct
{
    int32_t red;       /**< 660nm 红光原始采样值 */
    int32_t ir;        /**< 905nm 红外原始采样值 */
    float   red_fir;   /**< 660nm 连续0.3~5Hz FIR输出 */
    float   ir_fir;    /**< 905nm 连续0.3~5Hz FIR输出 */
} PPG_Frame;

/**
 * @brief 算法计算作业描述结构体
 */
typedef struct
{
    uint32_t end_seq; /**< 对应 600 帧窗口末尾的最新帧全局序列号 */
    uint32_t epoch;   /**< 当前数据流的代数（若发生断流则递增，用以检测数据窗口连续性） */
} PPG_Job;

/* 全局统计与状态观测变量 */
extern volatile uint32_t  PPG_TotalFrames;      /**< 系统启动以来累计接收到的有效 PPG 帧总数 */
extern volatile uint32_t  PPG_Gaps;             /**< 发生数据断流（FIFO 溢出、失步或 DMA 失败）的总次数 */
extern volatile uint32_t  PPG_QueueDrops;       /**< 因算法队列满导致的丢包次数 */
extern volatile uint32_t  PPG_FifoIrqCount;     /**< PB3 EXTI3 FIFO 水线中断总触发次数 */
extern volatile uint32_t  PPG_FifoReadCount;    /**< 成功执行 DMA 批量读取的批次总数 */
extern volatile rt_bool_t PPG_Streaming;        /**< 当前 AFE 是否正在持续采集流式传输中 */
extern volatile uint32_t  PPG_InitAttempts;     /**< AFE 芯片尝试初始化次数 */
extern volatile uint32_t  PPG_InitFailures;     /**< AFE 初始化失败次数 */
extern volatile uint32_t  PPG_FifoOverflowCount; /**< FIFO 硬件溢出次数 */

/* 算法作业消息队列（由采集任务发送，由算法任务消费） */
extern struct rt_messagequeue spo2_job_mq;

/**
 * @brief  初始化 PPG 数据采集任务及相关 IPC 资源
 * @return 0 成功，-1 失败
 */
int app_ppg_init(void);

/**
 * @brief  通知 PPG 模块蓝牙连接状态变化
 * @param  connected RT_TRUE 表示已连接，RT_FALSE 表示已断开
 * @note   连接成功后唤醒采集任务以启动 AFE 采样；断开后停止采样以达最佳省电效果。
 */
void app_ppg_set_connected(rt_bool_t connected);
void app_ppg_set_enabled(rt_bool_t enabled);

/**
 * @brief  PB3 / EXTI3 中断触发时的通知回调
 * @note   运行于中断上下文，极简处理，仅释放信号量唤醒采集线程。
 */
void app_ppg_irq(void);

/** DMA1 Channel 7 完成中断通知。仅供 interrupt.c 调用。 */
void app_ppg_dma_irq(void);

/**
 * @brief  停止 PPG 采样并关闭外部中断与 DMA
 */
void app_ppg_stop(void);

/**
 * @brief  查询 PPG 采集模块当前是否处于总线与中断空闲状态
 * @return RT_TRUE 空闲（可安全进入 STOP0 休眠），RT_FALSE 正在读取或水线引脚正处于低电平
 */
rt_bool_t app_ppg_idle(void);

/**
 * @brief  从环形缓冲区安全拷贝指定作业的 600 帧连续波形
 * @param  job 作业信息（包含 end_seq 与期望的 epoch）
 * @param  out 目标输出缓冲区（若传 RT_NULL 则仅校验 epoch 是否匹配）
 * @return RT_EOK 成功，-RT_ERROR 失败（数据已覆盖或代数失步）
 */
int app_ppg_copy(const PPG_Job *job, PPG_Frame *out);

#ifdef __cplusplus
}
#endif

#endif /* __APP_PPG_H__ */
