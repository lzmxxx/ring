/*
 * rtt_heartbeat_template.c — RTT 心跳输出（验证用）
 *
 * mklink-flash 自动复制此文件到用户项目的 applications/ 目录
 * 并注册到 Keil uvprojx 的 applications 文件组。
 *
 * 行为：每秒通过 SEGGER_RTT_printf 输出 1 帧心跳，mklink-flash HIL 测试
 * 用此验证 RTT 流通道可达。
 *
 * 关闭：删除此文件并从 uvprojx 移除引用。
 */

#include <rtthread.h>
#include <stdint.h>

#ifdef USE_RTT
#include "SEGGER_RTT.h"
#endif

static struct rt_timer hb_timer;
static uint32_t hb_counter = 0;

static void rtt_heartbeat_cb(void *param) {
    (void)param;
#ifdef USE_RTT
    SEGGER_RTT_printf(0, "[HB] tick=%lu seq=%u\n",
                      (unsigned long)rt_tick_get(),
                      (unsigned)(++hb_counter));
#endif
}

int rtt_heartbeat_init(void) {
    rt_timer_init(&hb_timer, "rtt_hb",
                  rtt_heartbeat_cb, RT_NULL,
                  RT_TICK_PER_SECOND,
                  RT_TIMER_FLAG_PERIODIC);
    if (rt_timer_start(&hb_timer) != RT_EOK) {
        return -1;
    }
    return 0;
}
INIT_APP_EXPORT(rtt_heartbeat_init);
