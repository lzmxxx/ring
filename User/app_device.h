/**
 * @file    app_device.h
 * @brief   设备统一配置、RTC 时间与采集状态机
 */
#ifndef __APP_DEVICE_H__
#define __APP_DEVICE_H__

#include <rtthread.h>
#include <stdint.h>

typedef enum
{
    APP_OUTPUT_RAW_ONLY  = 0x01U,
    APP_OUTPUT_ALGO_ONLY = 0x02U,
    APP_OUTPUT_RAW_ALGO  = 0x03U
} app_output_mode_t;

typedef enum
{
    APP_STATE_BOOT = 0U,
    APP_STATE_ADVERTISING,
    APP_STATE_CONNECTED,
    APP_STATE_ACQUIRING,
    APP_STATE_PERIOD_WAIT,
    APP_STATE_FLASH_SYNC,
    APP_STATE_STANDBY
} app_state_t;

typedef enum
{
    APP_ERR_NONE = 0U,
    APP_ERR_RTC_INVALID,
    APP_ERR_IPA_INIT,
    APP_ERR_ACCEL_INIT,
    APP_ERR_FIFO_OVERFLOW,
    APP_ERR_FLASH,
    APP_ERR_FLASH_FULL,
    APP_ERR_PROTOCOL,
    APP_ERR_INVALID_MODE,
    APP_ERR_INVALID_PERIOD,
    APP_ERR_SENSOR_POWER
} app_error_t;

typedef struct
{
    uint8_t  output_mode;
    uint16_t sample_period_sec;
    uint8_t  record_enable;
    uint8_t  rtc_valid;
    uint8_t  motion_state;
    uint8_t  sensor_state;
    uint8_t  connected;
    uint8_t  subscribed;
    uint8_t  state;
    uint8_t  error;
    int16_t  timezone_min;
    uint32_t last_time_sync;
} app_device_status_t;

int app_device_init(void);
void app_device_set_connected(rt_bool_t connected);
void app_device_set_subscribed(rt_bool_t subscribed);
void app_device_set_recording(rt_bool_t enabled);
void app_device_set_error(app_error_t error);
void app_device_set_sensor_state(rt_bool_t enabled);
void app_device_set_motion_state(rt_bool_t moving);
void app_device_set_sync_state(rt_bool_t enabled);
void app_device_set_flash_busy(rt_bool_t busy);
uint8_t app_device_set_output_mode(uint8_t mode);
uint8_t app_device_set_sample_period(uint16_t period_sec);
void app_device_rtc_wakeup(void);

rt_bool_t app_device_algorithm_required(void);
rt_bool_t app_device_raw_required(void);
rt_bool_t app_device_periodic_mode(void);
rt_bool_t app_device_session_required(void);
void app_device_get_status(app_device_status_t *status);

uint8_t app_device_rtc_set(uint32_t unix_time, int16_t timezone_min);
uint32_t app_device_rtc_now(void);

#endif /* __APP_DEVICE_H__ */
