/*
 * mklink 精简版 RT-Thread SystemView 适配器（替代 RT-Thread 包自带的重型适配）。
 *
 * 设计取舍：只用 rt_scheduler_sethook（所有 RT-Thread 版本都有）捕获任务切换，
 * 产生 task_start_exec / task_stop_exec 事件 —— SystemView 的核心价值：任务甘特
 * 时间轴 + 每任务 CPU 占用。不依赖 RT-Thread 包系统的 RTT_TRACE_ID_* 内核对象
 * 钩子（信号量/互斥量/事件/队列事件），跨版本兼容、不触发 AC5 的
 * "function call in constant expression" 报错。
 *
 * 本文件同时提供 SEGGER_SYSVIEW_OS_API（_cbGetTime + _cbSendTaskList），供
 * Config_RTThread.c 的 SEGGER_SYSVIEW_Conf() 在 Init 时引用（否则链接报
 * SYSVIEW_X_OS_TraceApi undefined）。任务名字段访问带 RT_VERSION_CHECK 守卫，
 * 兼容 RT-Thread 4.x / 5.x。
 *
 * 用 USE_SYSTEMVIEW（mklink 手动集成）或 PKG_USING_SYSTEMVIEW（Env 包）启用。
 * RT-Thread 在 INIT_COMPONENT 阶段自动调用 rt_trace_init，立即 Start()——
 * 目标端启动即把事件写入 RTT 通道 1。
 */
#include "rtthread.h"
#include "SEGGER_SYSVIEW.h"
#include "SEGGER_RTT.h"

#if defined(USE_SYSTEMVIEW) || defined(PKG_USING_SYSTEMVIEW)

static rt_thread_t tidle;

/* ---- SEGGER_SYSVIEW_OS_API 实现 ---- */

static U64 _cbGetTime(void)
{
    return (U64)(rt_tick_get() * 1000 / RT_TICK_PER_SECOND);
}

static void _cbSendTaskInfo(const rt_thread_t thread)
{
    SEGGER_SYSVIEW_TASKINFO Info;

    rt_enter_critical();
    rt_memset(&Info, 0, sizeof(Info));
    Info.TaskID = (U32)thread;
    /* 不用 RT_VERSION_CHECK()——RT-Thread 4.x 未定义该宏，AC5 预处理器会报 #59。
     * 用 RT_VERSION 整数比较（4.x=4, 5.x=5），AC5 安全。 */
#if defined(RT_VERSION) && (RT_VERSION >= 5)
    Info.sName = thread->parent.name;
#else
    Info.sName = thread->name;
#endif
#if defined(RT_VERSION) && (RT_VERSION >= 5)
    Info.Prio = RT_SCHED_PRIV(thread).current_priority;
#else
    Info.Prio = thread->current_priority;
#endif
    Info.StackBase = (U32)thread->stack_addr;
    Info.StackSize = thread->stack_size;

    SEGGER_SYSVIEW_SendTaskInfo(&Info);
    rt_exit_critical();
}

static void _cbSendTaskList(void)
{
    struct rt_thread *thread;
    struct rt_list_node *node;
    struct rt_list_node *list;
    struct rt_object_information *info;

    info = rt_object_get_information(RT_Object_Class_Thread);
    list = &info->object_list;

    tidle = rt_thread_idle_gethandler();

    rt_enter_critical();
    for (node = list->next; node != list; node = node->next)
    {
#if defined(RT_VERSION) && (RT_VERSION >= 5)
        thread = (struct rt_thread *)rt_list_entry(node, struct rt_object, list);
#else
        thread = rt_list_entry(node, struct rt_thread, list);
#endif
        if (thread != tidle)
            _cbSendTaskInfo(thread);
    }
    rt_exit_critical();
}

/* 提供给 SEGGER_SYSVIEW_Conf() 引用的 OS API。
 * 注意：Config_RTThread.c 引用的符号是 SYSVIEW_X_OS_TraceAPI（API 全大写），
 * 必须与此完全一致（C 区分大小写）。 */
const SEGGER_SYSVIEW_OS_API SYSVIEW_X_OS_TraceAPI =
{
    _cbGetTime, _cbSendTaskList,
};

/* ---- 调度器钩子：任务切换 → SystemView 执行区间事件 ---- */

static void _mklink_scheduler_hook(rt_thread_t from, rt_thread_t to)
{
    (void)from;
    SEGGER_SYSVIEW_OnTaskStopExec();         /* 上一个任务停止运行 */
    SEGGER_SYSVIEW_OnTaskStartExec((U32)to); /* 新任务开始运行 */
    if (to == tidle) {
        SEGGER_SYSVIEW_OnIdle();             /* 切到空闲 → idle 事件（供空闲率统计）*/
    }
}

static void _mklink_thread_inited_hook(rt_thread_t thread)
{
    SEGGER_SYSVIEW_OnTaskCreate((U32)thread);
}

/* ---- ISR / 软件定时器钩子（ISR 延迟 / 中断占用分析需要） ----
 * 注意：中断在一个 busy RTOS 上极高频（实测 STM32F405 ~71K 次/秒），会灌满
 * RTT 缓冲导致溢出丢包、并偏向丢失忙时事件 → 任务 CPU%/空闲率失真。故 ISR/
 * timer 钩子默认**关闭**（只为任务切换跟踪时，数据干净、空闲率可信）。需要 ISR
 * 延迟/占用分析时，在工程 Define 里加 SYSVIEW_TRACE_ISR 开启（同时建议把
 * SEGGER_SYSVIEW_RTT_BUFFER_SIZE 调到 32K+）。 */
#if defined(SYSVIEW_TRACE_ISR)

static void _mklink_irq_enter_hook(void)
{
    SEGGER_SYSVIEW_RecordEnterISR();
}

static void _mklink_irq_leave_hook(void)
{
    SEGGER_SYSVIEW_RecordExitISR();
}

static void _mklink_timer_enter_hook(rt_timer_t timer)
{
    SEGGER_SYSVIEW_RecordEnterTimer((U32)timer);
}

static void _mklink_timer_exit_hook(rt_timer_t timer)
{
    (void)timer;
    SEGGER_SYSVIEW_RecordExitTimer();
}

#endif /* SYSVIEW_TRACE_ISR */

void rt_trace_init(void)
{
    tidle = rt_thread_idle_gethandler();
    SEGGER_SYSVIEW_Conf();                    /* Init + SetRAMBase（Config_RTThread.c） */
    rt_scheduler_sethook(_mklink_scheduler_hook);
    rt_thread_inited_sethook(_mklink_thread_inited_hook);
#if defined(SYSVIEW_TRACE_ISR)
    rt_interrupt_enter_sethook(_mklink_irq_enter_hook);
    rt_interrupt_leave_sethook(_mklink_irq_leave_hook);
    rt_timer_enter_sethook(_mklink_timer_enter_hook);
    rt_timer_exit_sethook(_mklink_timer_exit_hook);
#endif
    SEGGER_SYSVIEW_Start();                   /* mklink：目标端启动即跟踪 */
    SEGGER_SYSVIEW_SendTaskList();            /* 上报当前任务（含名称） */
    rt_kprintf("[SystemView] tracing started, RTT CB @ 0x%x\n",
               (unsigned)(size_t)&_SEGGER_RTT);
}

#endif /* USE_SYSTEMVIEW || PKG_USING_SYSTEMVIEW */
/*************************** End of file ****************************/
