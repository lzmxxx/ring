/**
 * @file    app_record.c
 * @brief   200 KiB 片内 Flash 记录管理
 * @details 两个独立 Metadata 页保存会话头；数据区启动时预擦除，之后每15秒
 *          仅编程一条16字节记录。掉电后以逐条 CRC 扫描恢复写指针。
 */
#include "app_record.h"
#include "app_ble.h"
#include "app_config.h"
#include "app_device.h"
#include "app_power.h"
#include "n32wb452_flash.h"
#include <rtthread.h>
#include <string.h>

#define RECORD_MAGIC              0x52474E31UL /* "RGN1" */
#define RECORD_FORMAT_VERSION     1U
#define RECORD_META_A             APP_RECORD_FLASH_BASE
#define RECORD_META_B             (APP_RECORD_FLASH_BASE + APP_RECORD_FLASH_PAGE_SIZE)
#define RECORD_DATA_BASE          (APP_RECORD_FLASH_BASE + 2U * APP_RECORD_FLASH_PAGE_SIZE)
#define RECORD_DATA_BYTES         (APP_RECORD_FLASH_TOTAL - 2U * APP_RECORD_FLASH_PAGE_SIZE)
#define RECORD_CAPACITY           (RECORD_DATA_BYTES / sizeof(app_record_t))
#define RECORD_QUEUE_DEPTH        4U

typedef struct
{
    uint32_t magic;
    uint32_t version_size;
    uint32_t session_id;
    uint32_t start_time;
    uint32_t immutable_crc;
    uint32_t record_count;
    uint32_t end_time;
    uint32_t state_word;
} record_metadata_t;

typedef enum { REC_MSG_RESULT, REC_MSG_START, REC_MSG_STOP, REC_MSG_ERASE, REC_MSG_SYNC } record_msg_type_t;
typedef struct
{
    uint8_t type;
    uint16_t start_sequence;
    SpO2_Result result;
} record_message_t;

typedef char record_size_must_be_16[(sizeof(app_record_t) == 16U) ? 1 : -1];

static struct rt_messagequeue record_mq;
static uint8_t record_pool[RECORD_QUEUE_DEPTH * (sizeof(record_message_t) + sizeof(void *))];
static struct rt_thread record_thread;
ALIGN(RT_ALIGN_SIZE) static uint8_t record_stack[APP_RECORD_TASK_STACK_SIZE];
static app_record_info_t record_info;
static uint32_t write_address = RECORD_DATA_BASE;
static uint8_t initialized;

static uint16_t crc16_ccitt(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    while (length--)
    {
        crc ^= (uint16_t)(*data++) << 8;
        for (i = 0U; i < 8U; i++) crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
    }
    return crc;
}

static uint8_t record_valid(const app_record_t *record)
{
    return (record->timestamp != 0xFFFFFFFFUL &&
            crc16_ccitt((const uint8_t *)record, 14U) == record->crc16) ? 1U : 0U;
}

static uint8_t flash_program(uint32_t address, const void *data, uint16_t length)
{
    const uint32_t *words = (const uint32_t *)data;
    uint16_t word_count = (uint16_t)((length + 3U) / 4U);
    uint16_t i;
    FLASH_STS status = FLASH_COMPL;

    FLASH_Unlock();
    for (i = 0U; i < word_count && status == FLASH_COMPL; i++)
        status = FLASH_ProgramWord(address + (uint32_t)i * 4U, words[i]);
    FLASH_Lock();
    return (status == FLASH_COMPL) ? 0U : 1U;
}

static uint8_t erase_record_area(void)
{
    uint32_t address;
    FLASH_STS status = FLASH_COMPL;

    record_info.erasing = 1U;
    app_ble_post_event(APP_BLE_EVENT_RECORD_ERASING, 0U);
    rt_thread_mdelay(30U); /* 先给 BLE 任务一次发送状态的机会。 */
    FLASH_Unlock();
    for (address = APP_RECORD_FLASH_BASE;
         address < APP_RECORD_FLASH_BASE + APP_RECORD_FLASH_TOTAL;
         address += APP_RECORD_FLASH_PAGE_SIZE)
    {
        status = FLASH_EraseOnePage(address);
        if (status != FLASH_COMPL) break;
        rt_thread_mdelay(1U); /* 页间让高优先级 BLE/FIFO 任务运行。 */
    }
    FLASH_Lock();
    record_info.erasing = 0U;
    if (status != FLASH_COMPL)
    {
        app_device_set_error(APP_ERR_FLASH);
        return 1U;
    }
    return 0U;
}

static void write_metadata_start(void)
{
    record_metadata_t metadata;
    memset(&metadata, 0xFF, sizeof(metadata));
    metadata.magic = RECORD_MAGIC;
    metadata.version_size = ((uint32_t)sizeof(app_record_t) << 16) | RECORD_FORMAT_VERSION;
    metadata.session_id = record_info.session_id;
    metadata.start_time = record_info.first_timestamp;
    metadata.immutable_crc = crc16_ccitt((const uint8_t *)&metadata, 16U);
    (void)flash_program(RECORD_META_A, &metadata, sizeof(metadata));
    (void)flash_program(RECORD_META_B, &metadata, sizeof(metadata));
}

static void write_metadata_stop(uint32_t state_word)
{
    uint32_t values[3];
    values[0] = record_info.record_count;
    values[1] = record_info.last_timestamp;
    values[2] = state_word;
    (void)flash_program(RECORD_META_A + 20U, values, sizeof(values));
    (void)flash_program(RECORD_META_B + 20U, values, sizeof(values));
}

static void recover_records(void)
{
    const record_metadata_t *meta_a = (const record_metadata_t *)RECORD_META_A;
    const record_metadata_t *meta_b = (const record_metadata_t *)RECORD_META_B;
    const record_metadata_t *metadata = RT_NULL;
    uint32_t index;

    memset(&record_info, 0, sizeof(record_info));
    record_info.flash_total = RECORD_DATA_BYTES;
    if (meta_a->magic == RECORD_MAGIC &&
        (uint16_t)meta_a->version_size == RECORD_FORMAT_VERSION) metadata = meta_a;
    else if (meta_b->magic == RECORD_MAGIC &&
             (uint16_t)meta_b->version_size == RECORD_FORMAT_VERSION) metadata = meta_b;
    if (!metadata) return;

    record_info.session_id = metadata->session_id;
    for (index = 0U; index < RECORD_CAPACITY; index++)
    {
        const app_record_t *record = (const app_record_t *)(RECORD_DATA_BASE + index * sizeof(app_record_t));
        if (!record_valid(record)) break;
        if (index == 0U) record_info.first_timestamp = record->timestamp;
        record_info.last_timestamp = record->timestamp;
    }
    record_info.record_count = index;
    record_info.flash_used = index * sizeof(app_record_t);
    write_address = RECORD_DATA_BASE + record_info.flash_used;
}

static void append_result(const SpO2_Result *result)
{
    app_record_t record;
    if (!record_info.recording) return;
    if (record_info.record_count >= RECORD_CAPACITY)
    {
        record_info.recording = 0U;
        app_device_set_recording(RT_FALSE);
        app_device_set_error(APP_ERR_FLASH_FULL);
        write_metadata_stop(1U);
        app_ble_post_event(APP_BLE_EVENT_FLASH_FULL, record_info.record_count);
        return;
    }

    memset(&record, 0, sizeof(record));
    record.timestamp = result->timestamp;
    record.sequence = (uint16_t)record_info.record_count;
    record.spo2 = (uint8_t)(result->spo2 > 100.0f ? 100U : result->spo2 + 0.5f);
    record.heart_rate = (uint8_t)(result->hr > 255.0f ? 255U : result->hr + 0.5f);
    record.motion = result->motion;
    record.quality = result->quality;
    record.temperature = result->temperature;
    record.flags = (result->valid ? 0x01U : 0U) | (result->calibrated ? 0x02U : 0U);
    record.crc16 = crc16_ccitt((const uint8_t *)&record, 14U);

    if (flash_program(write_address, &record, sizeof(record)) != 0U)
    {
        app_device_set_error(APP_ERR_FLASH);
        app_ble_post_error(0x20U, APP_ERR_FLASH);
        return;
    }
    write_address += sizeof(record);
    record_info.record_count++;
    record_info.flash_used += sizeof(record);
    if (record_info.record_count == 1U) record_info.first_timestamp = record.timestamp;
    record_info.last_timestamp = record.timestamp;
}

static void handle_start(void)
{
    app_device_status_t status;
    app_device_get_status(&status);
    if (!status.rtc_valid)
    {
        app_ble_post_error(0x20U, APP_ERR_RTC_INVALID);
        return;
    }
    app_device_set_recording(RT_FALSE);
    if (erase_record_area() != 0U)
    {
        app_ble_post_error(0x20U, APP_ERR_FLASH);
        return;
    }
    record_info.session_id++;
    if (record_info.session_id == 0U) record_info.session_id = 1U;
    record_info.record_count = 0U;
    record_info.flash_used = 0U;
    record_info.first_timestamp = app_device_rtc_now();
    record_info.last_timestamp = 0U;
    write_address = RECORD_DATA_BASE;
    write_metadata_start();
    record_info.recording = 1U;
    app_device_set_recording(RT_TRUE);
    app_ble_post_event(APP_BLE_EVENT_RECORD_READY, 0U);
    app_ble_post_event(APP_BLE_EVENT_RECORD_STARTED, record_info.session_id);
}

static void handle_stop(void)
{
    if (record_info.recording)
    {
        record_info.recording = 0U;
        app_device_set_recording(RT_FALSE);
        write_metadata_stop(0U);
    }
    app_ble_post_event(APP_BLE_EVENT_RECORD_STOPPED, record_info.record_count);
}

static void handle_sync(uint16_t start_sequence)
{
    uint32_t index;
    app_device_set_sync_state(RT_TRUE);
    for (index = start_sequence; index < record_info.record_count; index++)
    {
        const app_record_t *record = (const app_record_t *)(RECORD_DATA_BASE + index * sizeof(app_record_t));
        while (!app_ble_post_history(record))
        {
            if (!app_device_session_required()) goto sync_exit;
            rt_thread_mdelay(20U);
        }
    }
sync_exit:
    app_device_set_sync_state(RT_FALSE);
    app_ble_post_event(APP_BLE_EVENT_SYNC_END, index);
}

static void record_thread_entry(void *parameter)
{
    record_message_t message;
    (void)parameter;
    while (1)
    {
        if (rt_mq_recv(&record_mq, &message, sizeof(message), RT_WAITING_FOREVER) != RT_EOK) continue;
        if (message.type == REC_MSG_RESULT) append_result(&message.result);
        else if (message.type == REC_MSG_START) handle_start();
        else if (message.type == REC_MSG_STOP) handle_stop();
        else if (message.type == REC_MSG_ERASE)
        {
            handle_stop();
            if (erase_record_area() == 0U)
            {
                memset(&record_info, 0, sizeof(record_info));
                record_info.flash_total = RECORD_DATA_BYTES;
                write_address = RECORD_DATA_BASE;
                app_ble_post_event(APP_BLE_EVENT_RECORD_ERASED, 0U);
            }
        }
        else if (message.type == REC_MSG_SYNC) handle_sync(message.start_sequence);
    }
}

static uint8_t send_control(record_msg_type_t type, uint16_t start_sequence)
{
    record_message_t message;
    memset(&message, 0, sizeof(message));
    message.type = (uint8_t)type;
    message.start_sequence = start_sequence;
    return (rt_mq_send(&record_mq, &message, sizeof(message)) == RT_EOK) ? 0U : 1U;
}

int app_record_init(void)
{
    recover_records();
    if (rt_mq_init(&record_mq, "record", record_pool, sizeof(record_message_t),
                   sizeof(record_pool), RT_IPC_FLAG_FIFO) != RT_EOK) return -1;
    if (rt_thread_init(&record_thread, "record", record_thread_entry, RT_NULL,
                       record_stack, sizeof(record_stack), APP_RECORD_TASK_PRIORITY,
                       APP_RECORD_TASK_TIMESLICE) != RT_EOK) return -1;
    initialized = 1U;
    return rt_thread_startup(&record_thread);
}

void app_record_post_result(const SpO2_Result *result)
{
    record_message_t message;
    if (!initialized || !record_info.recording || !result) return;
    memset(&message, 0, sizeof(message));
    message.type = REC_MSG_RESULT;
    message.result = *result;
    if (rt_mq_send(&record_mq, &message, sizeof(message)) != RT_EOK) app_device_set_error(APP_ERR_FLASH);
}

uint8_t app_record_start(void) { return send_control(REC_MSG_START, 0U); }
uint8_t app_record_stop(void) { return send_control(REC_MSG_STOP, 0U); }
uint8_t app_record_erase(void) { return send_control(REC_MSG_ERASE, 0U); }
uint8_t app_record_sync(uint16_t start_sequence) { return send_control(REC_MSG_SYNC, start_sequence); }
void app_record_get_info(app_record_info_t *info) { if (info) *info = record_info; }
void app_record_shutdown_flush(void) { if (record_info.recording) { record_info.recording = 0U; write_metadata_stop(0U); } }
