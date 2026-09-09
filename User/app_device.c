/**
 * @file    app_device.c
 * @brief   设备顶层状态机与 RTC 绝对时间调度
 * @note    1秒表示连续采集；其它合法周期以“前一次窗口起点+周期”推进，
 *          不从算法结束时重新计时，避免累计漂移。
 */
#include "app_device.h"
#include "app_config.h"
#include "app_ppg.h"
#include "n32wb452_bkp.h"
#include "n32wb452_pwr.h"
#include "n32wb452_rcc.h"
#include "n32wb452_rtc.h"
#include <rthw.h>
#include <string.h>

#define RTC_VALID_MAGIC          0x52A7U
#define PERIOD_WINDOW_SEC        10U

extern void hlt_rtc_init(void);

static app_device_status_t device_status;
static struct rt_semaphore state_sem;
static struct rt_thread state_thread;
ALIGN(RT_ALIGN_SIZE) static uint8_t state_stack[APP_STATE_TASK_STACK_SIZE];
static uint32_t next_sample_time;
static uint32_t window_start_time;
static rt_bool_t sampling_enabled;
static rt_bool_t schedule_reset;
static rt_bool_t state_ready;
static rt_bool_t flash_busy;

static rt_bool_t is_leap_year(uint16_t year)
{
    return ((year % 4U) == 0U && (((year % 100U) != 0U) || ((year % 400U) == 0U))) ? RT_TRUE : RT_FALSE;
}

static uint8_t days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days[] = {31U,28U,31U,30U,31U,30U,31U,31U,30U,31U,30U,31U};
    return (uint8_t)(days[month - 1U] + ((month == 2U && is_leap_year(year)) ? 1U : 0U));
}

static uint32_t datetime_to_unix(uint16_t year, uint8_t month, uint8_t day,
                                 uint8_t hour, uint8_t minute, uint8_t second)
{
    uint32_t days = 0U;
    uint16_t y;
    uint8_t m;

    for (y = 1970U; y < year; y++)
    {
        days += is_leap_year(y) ? 366U : 365U;
    }
    for (m = 1U; m < month; m++)
    {
        days += days_in_month(year, m);
    }
    days += (uint32_t)day - 1U;
    return days * 86400U + (uint32_t)hour * 3600U + (uint32_t)minute * 60U + second;
}

static void unix_to_datetime(uint32_t unix_time, RTC_DateType *date, RTC_TimeType *time)
{
    uint32_t days = unix_time / 86400U;
    uint32_t seconds = unix_time % 86400U;
    uint16_t year = 1970U;
    uint8_t month = 1U;
    uint16_t year_days;

    while (days >= (year_days = is_leap_year(year) ? 366U : 365U))
    {
        days -= year_days;
        year++;
    }
    while (days >= days_in_month(year, month))
    {
        days -= days_in_month(year, month);
        month++;
    }

    RTC_DateStructInit(date);
    date->Year = (uint8_t)(year - 2000U);
    date->Month = month;
    date->Date = (uint8_t)(days + 1U);
    date->WeekDay = (uint8_t)(((unix_time / 86400U + 3U) % 7U) + 1U);

    RTC_TimeStructInit(time);
    time->Hours = (uint8_t)(seconds / 3600U);
    time->Minutes = (uint8_t)((seconds % 3600U) / 60U);
    time->Seconds = (uint8_t)(seconds % 60U);
}

uint32_t app_device_rtc_now(void)
{
    RTC_DateType first_date, second_date;
    RTC_TimeType time;

    if (!device_status.rtc_valid) return 0U;
    do
    {
        RTC_GetDate(RTC_FORMAT_BIN, &first_date);
        RTC_GetTime(RTC_FORMAT_BIN, &time);
        RTC_GetDate(RTC_FORMAT_BIN, &second_date);
    }
    while (memcmp(&first_date, &second_date, sizeof(first_date)) != 0);

    return datetime_to_unix((uint16_t)second_date.Year + 2000U,
                            second_date.Month, second_date.Date,
                            time.Hours, time.Minutes, time.Seconds);
}

uint8_t app_device_rtc_set(uint32_t unix_time, int16_t timezone_min)
{
    RTC_DateType date;
    RTC_TimeType time;

    if (unix_time < 946684800UL || unix_time > 4102444799UL) return 1U;
    unix_to_datetime(unix_time, &date, &time);

    PWR_BackupAccessEnable(ENABLE);
    if ((RTC_SetDate(RTC_FORMAT_BIN, &date) == ERROR) ||
        (RTC_ConfigTime(RTC_FORMAT_BIN, &time) == ERROR)) return 1U;

    BKP_WriteBkpData(BKP_DAT1, RTC_VALID_MAGIC);
    BKP_WriteBkpData(BKP_DAT2, (uint16_t)unix_time);
    BKP_WriteBkpData(BKP_DAT3, (uint16_t)(unix_time >> 16));
    device_status.rtc_valid = 1U;
    device_status.timezone_min = timezone_min;
    device_status.last_time_sync = unix_time;
    schedule_reset = RT_TRUE;
    rt_sem_release(&state_sem);
    return 0U;
}

static void set_sampling(rt_bool_t enabled)
{
    if (sampling_enabled == enabled) return;
    sampling_enabled = enabled;
    device_status.state = enabled ? APP_STATE_ACQUIRING : APP_STATE_PERIOD_WAIT;
    app_ppg_set_enabled(enabled);
}

static void update_sampling_state(void)
{
    rt_bool_t requested = !flash_busy && (device_status.record_enable ||
                           (device_status.connected && device_status.subscribed));
    uint32_t now;

    if (!requested)
    {
        set_sampling(RT_FALSE);
        device_status.state = device_status.connected ? APP_STATE_CONNECTED : APP_STATE_ADVERTISING;
        schedule_reset = RT_TRUE;
        return;
    }

    if (device_status.sample_period_sec == 1U && !device_status.record_enable)
    {
        set_sampling(RT_TRUE);
        return;
    }

    now = app_device_rtc_now();
    if (now == 0U) return;
    if (schedule_reset)
    {
        next_sample_time = now;
        window_start_time = 0U;
        schedule_reset = RT_FALSE;
    }

    if (sampling_enabled && (now - window_start_time >= PERIOD_WINDOW_SEC))
    {
        set_sampling(RT_FALSE);
    }

    if (!sampling_enabled && now >= next_sample_time)
    {
        window_start_time = next_sample_time;
        do { next_sample_time += device_status.sample_period_sec; }
        while (next_sample_time <= now);
        set_sampling(RT_TRUE);
    }
}

static void state_thread_entry(void *parameter)
{
    (void)parameter;
    while (1)
    {
        (void)rt_sem_take(&state_sem, RT_WAITING_FOREVER);
        update_sampling_state();
    }
}

int app_device_init(void)
{
    memset(&device_status, 0, sizeof(device_status));
    device_status.output_mode = APP_OUTPUT_ALGO_ONLY;
    device_status.sample_period_sec = 1U;
    device_status.state = APP_STATE_BOOT;

    /* 厂商原实现每次启动都会复位备份域；已改为只配置时钟而保留 RTC。 */
    hlt_rtc_init();
    if (BKP_ReadBkpData(BKP_DAT1) == RTC_VALID_MAGIC)
    {
        device_status.rtc_valid = 1U;
        device_status.last_time_sync = (uint32_t)BKP_ReadBkpData(BKP_DAT2) |
                                       ((uint32_t)BKP_ReadBkpData(BKP_DAT3) << 16);
    }
    else
    {
        device_status.error = APP_ERR_RTC_INVALID;
    }

    if (rt_sem_init(&state_sem, "state", 0, RT_IPC_FLAG_FIFO) != RT_EOK) return -1;
    if (rt_thread_init(&state_thread, "state", state_thread_entry, RT_NULL,
                       state_stack, sizeof(state_stack), APP_STATE_TASK_PRIORITY,
                       APP_STATE_TASK_TIMESLICE) != RT_EOK) return -1;
    state_ready = RT_TRUE;
    return rt_thread_startup(&state_thread);
}

void app_device_set_connected(rt_bool_t connected)
{
    device_status.connected = connected ? 1U : 0U;
    if (!connected) device_status.subscribed = 0U;
    schedule_reset = RT_TRUE;
    rt_sem_release(&state_sem);
}

void app_device_set_subscribed(rt_bool_t subscribed)
{
    device_status.subscribed = subscribed ? 1U : 0U;
    schedule_reset = RT_TRUE;
    rt_sem_release(&state_sem);
}

void app_device_set_recording(rt_bool_t enabled)
{
    device_status.record_enable = enabled ? 1U : 0U;
    if (enabled) device_status.sample_period_sec = 15U;
    schedule_reset = RT_TRUE;
    rt_sem_release(&state_sem);
}

uint8_t app_device_set_output_mode(uint8_t mode)
{
    if (mode < APP_OUTPUT_RAW_ONLY || mode > APP_OUTPUT_RAW_ALGO)
    {
        app_device_set_error(APP_ERR_INVALID_MODE);
        return 1U;
    }
    device_status.output_mode = mode;
    return 0U;
}

uint8_t app_device_set_sample_period(uint16_t period_sec)
{
    if (period_sec != 1U && period_sec < 15U)
    {
        app_device_set_error(APP_ERR_INVALID_PERIOD);
        return 1U;
    }
    if (device_status.record_enable) return 1U;
    device_status.sample_period_sec = period_sec;
    schedule_reset = RT_TRUE;
    rt_sem_release(&state_sem);
    return 0U;
}

void app_device_set_error(app_error_t error)
{
    device_status.error = (uint8_t)error;
}

void app_device_set_sensor_state(rt_bool_t enabled)
{
    device_status.sensor_state = enabled ? 1U : 0U;
}

void app_device_set_motion_state(rt_bool_t moving)
{
    device_status.motion_state = moving ? 1U : 0U;
}

void app_device_set_sync_state(rt_bool_t enabled)
{
    if (enabled)
    {
        device_status.state = APP_STATE_FLASH_SYNC;
    }
    else if (sampling_enabled)
    {
        device_status.state = APP_STATE_ACQUIRING;
    }
    else
    {
        device_status.state = device_status.connected ? APP_STATE_CONNECTED
                                                       : APP_STATE_ADVERTISING;
    }
}

void app_device_set_flash_busy(rt_bool_t busy)
{
    flash_busy = busy;
    schedule_reset = RT_TRUE;
    if (state_ready)
    {
        rt_sem_release(&state_sem);
    }
}
void app_device_rtc_wakeup(void)
{
    if (state_ready)
    {
        rt_sem_release(&state_sem);
    }
}

rt_bool_t app_device_algorithm_required(void)
{
    return (device_status.record_enable || device_status.output_mode != APP_OUTPUT_RAW_ONLY) ? RT_TRUE : RT_FALSE;
}

rt_bool_t app_device_raw_required(void)
{
    return (device_status.connected && device_status.subscribed &&
            device_status.output_mode != APP_OUTPUT_ALGO_ONLY) ? RT_TRUE : RT_FALSE;
}

rt_bool_t app_device_periodic_mode(void)
{
    return (device_status.record_enable || device_status.sample_period_sec != 1U) ? RT_TRUE : RT_FALSE;
}

rt_bool_t app_device_session_required(void)
{
    return (device_status.record_enable ||
            (device_status.connected && device_status.subscribed)) ? RT_TRUE : RT_FALSE;
}

void app_device_get_status(app_device_status_t *status)
{
    rt_base_t level;
    if (!status) return;
    level = rt_hw_interrupt_disable();
    *status = device_status;
    rt_hw_interrupt_enable(level);
}
