/**
 * @file    app_record.h
 * @brief   片内 Flash 长期记录与断点同步接口
 */
#ifndef __APP_RECORD_H__
#define __APP_RECORD_H__

#include "SpO2.h"
#include <stdint.h>

#define APP_RECORD_FLASH_BASE       0x0804E000UL
#define APP_RECORD_FLASH_TOTAL      0x00032000UL /* 200 KiB */
#define APP_RECORD_FLASH_PAGE_SIZE  0x00000800UL /* N32WB452: 2 KiB */

#pragma pack(push, 1)
typedef struct
{
    uint32_t timestamp;
    uint16_t sequence;
    uint8_t  spo2;
    uint8_t  heart_rate;
    uint8_t  motion;
    uint8_t  quality;
    int16_t  temperature;
    uint8_t  flags;
    uint8_t  reserved;
    uint16_t crc16;
} app_record_t;
#pragma pack(pop)

typedef struct
{
    uint32_t session_id;
    uint32_t record_count;
    uint32_t first_timestamp;
    uint32_t last_timestamp;
    uint32_t flash_used;
    uint32_t flash_total;
    uint8_t  recording;
    uint8_t  erasing;
} app_record_info_t;

int app_record_init(void);
void app_record_post_result(const SpO2_Result *result);
uint8_t app_record_start(void);
uint8_t app_record_stop(void);
uint8_t app_record_erase(void);
uint8_t app_record_sync(uint16_t start_sequence);
void app_record_get_info(app_record_info_t *info);
void app_record_shutdown_flush(void);

#endif /* __APP_RECORD_H__ */
