/* RT-Thread Configuration */
#ifndef RT_CONFIG_H__
#define RT_CONFIG_H__

#define RT_THREAD_PRIORITY_MAX  32
#define RT_TICK_PER_SECOND      1000
#define RT_ALIGN_SIZE           4
#define RT_NAME_MAX             8

#define RT_USING_USER_MAIN
#define RT_MAIN_THREAD_STACK_SIZE     2048
#define RT_MAIN_THREAD_PRIORITY       10

#define RT_USING_SEMAPHORE
#define RT_USING_EVENT
#define RT_USING_MESSAGEQUEUE

#define RT_USING_TIMER_SOFT
#define RT_TIMER_THREAD_PRIO    4
#define RT_TIMER_THREAD_STACK_SIZE 512

#define RT_USING_HOOK
#define RT_USING_IDLE_HOOK
/* Idle 调用完整时钟/BLE 恢复链路，256B 默认栈不足以容纳上下文。 */
#define IDLE_THREAD_STACK_SIZE 512

#define RT_USING_HEAP
#define RT_USING_SMALL_MEM

#endif /* RT_CONFIG_H__ */
