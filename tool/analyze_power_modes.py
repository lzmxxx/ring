"""
Power Mode & SystemView Timing Analyzer for SpO2 Ring
Calculates precise time spent in PM_SLEEP (User ID 0 / WFI) and PM_STOP (User ID 1 / STOP0).
Correlates SystemView event timestamps with hardware counters and RTOS tasks.
"""

import sys
import time
import struct
import numpy as np
import mklink
from mklink.systemview_analyzer import analyze_events

# Ensure UTF-8 output in Windows console
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding='utf-8')
    except Exception:
        pass

PORT = 'COM7'
AXF_PATH = 'MDK-ARM/Objects/N32WB452_BLE_RTOS.axf'
ADDR_LOCKS = 0x20000044
LOCK_NAMES = ["PM_FIFO", "PM_DMA", "PM_ALGO", "PM_BLE", "PM_SYSTEM"]


def get_snapshot():
    dev = mklink.connect(port=PORT, project_root='.', axf=AXF_PATH)
    try:
        locks_raw = list(dev.read_memory(ADDR_LOCKS, 5))
        snap = {
            "time": time.time(),
            "rt_tick": dev.read_variable("rt_tick"),
            "g_pm_mode": dev.read_variable("g_pm_mode"),
            "g_pm_sleep_count": dev.read_variable("g_pm_sleep_count"),
            "g_pm_stop_count": dev.read_variable("g_pm_stop_count"),
            "g_pm_sleep_ticks": dev.read_variable("g_pm_sleep_ticks"),
            "PPG_Streaming": dev.read_variable("PPG_Streaming"),
            "locks": locks_raw,
        }
    finally:
        dev.close()
    return snap


def analyze_user_intervals(events, user_target_id):
    """Pair user_start and user_stop events for user_target_id and return duration stats."""
    intervals = []
    start_time = None

    for e in events:
        kind = e.get("kind")
        uid = e.get("user_id")
        t_us = e.get("t_us")

        if t_us is None:
            continue

        if kind == "user_start" and uid == user_target_id:
            start_time = t_us
        elif kind == "user_stop" and uid == user_target_id:
            if start_time is not None:
                dur = t_us - start_time
                if dur >= 0:
                    intervals.append(dur)
                start_time = None

    return intervals


def main():
    duration = 8.0
    if len(sys.argv) > 1:
        try:
            duration = float(sys.argv[1])
        except ValueError:
            pass

    print("=" * 68)
    print("      N32WB452 + IPA1322 低功耗与 SystemView 运行态深度分析")
    print("=" * 68)

    # 1. 采集前变量快照
    print("[*] 读取采样前系统变量快照...")
    snap_before = get_snapshot()
    time.sleep(0.3)

    print(f"\n[1] 采样前系统变量快照:")
    print(f"    - 系统当前运行模式 g_pm_mode  : {snap_before['g_pm_mode']} (0:Run, 1:Sleep/WFI, 2:STOP0)")
    print(f"    - PPG 数据流状态 PPG_Streaming: {snap_before['PPG_Streaming']}")
    print(f"    - 系统当前滴答 rt_tick        : {snap_before['rt_tick']} (RT-Thread 1ms/tick)")
    print(f"    - 累计 Sleep 次数 / Ticks    : {snap_before['g_pm_sleep_count']} 次 / {snap_before['g_pm_sleep_ticks']} ticks")
    print(f"    - 累计 STOP0 次数            : {snap_before['g_pm_stop_count']} 次")
    print(f"    - 电源管理锁 locks[5]        : {snap_before['locks']}")
    for i, name in enumerate(LOCK_NAMES):
        state = "锁定 (LOCKED)" if snap_before['locks'][i] else "释放 (CLEAR)"
        print(f"      • {name:<10}: {state}")

    # 2. 启动 SystemView 跟踪
    print(f"\n[2] 启动 SystemView 追踪 (采样时长: {duration} 秒)...")
    dev = mklink.connect(port=PORT, project_root='.', axf=AXF_PATH)
    try:
        dev.systemview_start()
        res = dev.systemview_read(duration=duration)
        dev.systemview_stop()
    finally:
        dev.close()

    time.sleep(0.3)

    # 3. 采集后变量快照
    print("[*] 读取采样后系统变量快照...")
    snap_after = get_snapshot()

    events = res.get("events", [])
    print(f"[OK] SystemView 采集完成，共捕获 {len(events)} 个事件。")

    # 4. 解析 User Events 0 (PM_SLEEP) 和 1 (PM_STOP)
    sleep_intervals_us = analyze_user_intervals(events, user_target_id=0)
    stop_intervals_us = analyze_user_intervals(events, user_target_id=1)

    # 计算观测时间跨度
    t_start = None
    t_end = None
    for e in events:
        t = e.get("t_us")
        if t is not None:
            if t_start is None:
                t_start = t
            t_end = t

    total_trace_us = (t_end - t_start) if (t_start and t_end) else (duration * 1_000_000.0)
    total_trace_s = total_trace_us / 1_000_000.0

    total_sleep_us = sum(sleep_intervals_us)
    total_sleep_s = total_sleep_us / 1_000_000.0
    sleep_pct = (total_sleep_us / total_trace_us * 100.0) if total_trace_us > 0 else 0.0

    total_stop_us = sum(stop_intervals_us)
    total_stop_s = total_stop_us / 1_000_000.0
    stop_pct = (total_stop_us / total_trace_us * 100.0) if total_trace_us > 0 else 0.0

    print("\n" + "=" * 68)
    print("              低功耗时间占比分析 (SystemView 高精度)")
    print("=" * 68)
    print(f"有效观测时间跨度 : {total_trace_s:.3f} 秒 ({total_trace_us:,.1f} µs)")
    print("-" * 68)

    print(f"【PM_SLEEP (WFI 浅睡眠, User ID 0)】:")
    print(f"  • 进入总次数   : {len(sleep_intervals_us)} 次 (平均 {len(sleep_intervals_us)/total_trace_s:.1f} 次/秒)")
    print(f"  • 累计停留时长 : {total_sleep_s:.3f} 秒 ({total_sleep_us:,.1f} µs)")
    print(f"  • 时间占比     : 【{sleep_pct:.2f} %】  <---")
    if sleep_intervals_us:
        arr = np.array(sleep_intervals_us)
        print(f"  • 单次睡眠时长 : 平均 {np.mean(arr):.1f} µs, 中位数 {np.median(arr):.1f} µs")
        print(f"  • 极值分布     : 最短 {np.min(arr):.1f} µs, 最长 {np.max(arr):.1f} µs, 95分位 {np.percentile(arr, 95):.1f} µs")

    print("\n【PM_STOP (STOP0 深睡眠, User ID 1)】:")
    print(f"  • 进入总次数   : {len(stop_intervals_us)} 次")
    print(f"  • 累计停留时长 : {total_stop_s:.3f} 秒 ({total_stop_us:,.1f} µs)")
    print(f"  • 时间占比     : 【{stop_pct:.2f} %】")

    # 5. RTOS 任务 CPU 占用分析
    print("\n" + "=" * 68)
    print("                 RTOS 任务调度与 CPU 占用")
    print("=" * 68)
    report = analyze_events(events)
    tasks = report.get("tasks", [])
    print(f"{'任务名':<16} {'CPU%':<8} {'运行时间(ms)':<14} {'切换次数':<10} {'平均切片(µs)'}")
    print("-" * 68)
    for t in tasks:
        name = t.get("name", "Unknown")
        pct = t.get("cpu_pct", 0.0)
        dur_ms = t.get("total_us", 0.0) / 1000.0
        switches = t.get("switches", 0)
        avg_us = t.get("avg_slice_us", 0.0)
        print(f"{name:<16} {pct:<8.2f} {dur_ms:<14.2f} {switches:<10} {avg_us:.1f}")

    # 6. 硬件变量增量比对
    d_ticks = snap_after['rt_tick'] - snap_before['rt_tick']
    d_sleep_ticks = snap_after['g_pm_sleep_ticks'] - snap_before['g_pm_sleep_ticks']
    d_sleep_count = snap_after['g_pm_sleep_count'] - snap_before['g_pm_sleep_count']
    d_stop_count = snap_after['g_pm_stop_count'] - snap_before['g_pm_stop_count']

    tick_sleep_pct = (d_sleep_ticks / d_ticks * 100.0) if d_ticks > 0 else 0.0

    print("\n" + "=" * 68)
    print("              硬件变量增量统计 (Target Variable Snapshot)")
    print("=" * 68)
    print(f"系统运行流逝 Ticks : {d_ticks} ticks (~{d_ticks/1000.0:.2f} 秒)")
    print(f"硬件记录 Sleep Ticks: {d_sleep_ticks} ticks ({tick_sleep_pct:.2f} %)")
    print(f"硬件记录 Sleep 唤醒 : {d_sleep_count} 次 (~{d_sleep_count / (d_ticks/1000.0):.1f} 次/秒)")
    print(f"硬件记录 STOP0 次数 : {d_stop_count} 次")

    print("\n" + "=" * 68)
    print("                       深度分析与结论")
    print("=" * 68)
    print(f"1. 当前低功耗状态:")
    print(f"   • PM_SLEEP (WFI) : 占比高达 【{sleep_pct:.2f}%】 (硬件 tick 统计为 {tick_sleep_pct:.2f}%)")
    print(f"   • PM_STOP (STOP0): 占比为 【{stop_pct:.2f}%】")
    print("\n2. 为什么完全没有进入 STOP0 模式？")
    reasons = []
    if snap_before['PPG_Streaming']:
        reasons.append("• 【主要根因】PPG_Streaming = 1：IPA1322 处于持续光电采集状态（75Hz 帧率，PB3 FIFO 阈值水线中断唤醒 + I2C DMA1_CH7 批量接收）。")
    if not any(snap_before['locks']):
        reasons.append("• 此时 locks 状态均为 0 (CLEAR)，说明算法窗和 DMA 无死锁。")
    reasons.append("• 根据 app_power.c(L205) 设计逻辑：")
    reasons.append("    if (PPG_Streaming || !locks_clear() || !app_ble_idle() || ...)")
    reasons.append("        -> 执行 WFI (PM_SLEEP) 进入浅睡眠，保持时钟与外设 DMA 运转；")
    reasons.append("  只有当 PPG 停止流式采样 (PPG_Streaming=0) 且 BLE 协议栈允许空闲时，系统才会关停外设并进入 STOP0！")
    for r in reasons:
        print(f"   {r}")
    print("=" * 68)


if __name__ == '__main__':
    main()
