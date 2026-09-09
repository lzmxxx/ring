/* BLE 任务只负责连接、事件处理和发送；数据包格式在 app_packet.c。 */

#include "app_ble.h"
#include "app_power.h"
#include "app_ppg.h"
#include "app_config.h"
#include "app_packet.h"
#include "app_protocol.h"
#include "app_device.h"
#include "app_record.h"
#include "SC7A20.h"
#include "main.h"
#include "n32wb452_ble_api.h"
#include "app.h"
#include "user.h"
#include "ble_monitor.h"
#include <string.h>

/* 蓝牙广播与服务参数定义 */
#define BLE_NAME                "SpO2-Ring"
#define BLE_ADDR                "33:22:33:DB:30:A0"
#define BLE_SERVICE_UUID        0xFEE7U
/* 协议栈与睡眠标志 */
__IO uint8_t flag_bt_enter_sleep = 0U;
__IO uint8_t flag_bt_irq         = 0U;
BT_SERVER_STS gBT_STS            = BT_IDLE;
volatile rt_bool_t BLE_Ready     = RT_FALSE;

/* 统计计数器 */
volatile uint32_t BLE_RunPassCount;
volatile uint8_t  BLE_BusyAfterRun;
volatile uint32_t BLE_ConnectCount;
volatile uint32_t BLE_DisconnectCount;
volatile uint32_t BLE_NotifyAttempts;
volatile uint32_t BLE_NotifyQueued;
volatile uint32_t BLE_NotifyRejected;

/* RT-Thread 内部 IPC 与线程对象 */
static struct rt_semaphore   work_sem;
static struct rt_messagequeue result_mq;
static uint8_t               result_pool[APP_RESULT_QUEUE_DEPTH * (sizeof(SpO2_Result) + sizeof(void *))];
typedef struct
{
    uint8_t kind;
    uint8_t length;
    uint8_t data[APP_PACKET_SIZE];
} BleTxItem;
#define BLE_TX_PACKET   0U
#define BLE_TX_HISTORY  1U
#define BLE_TX_QUEUE_DEPTH 12U
static struct rt_messagequeue tx_mq;
static uint8_t tx_pool[BLE_TX_QUEUE_DEPTH * (sizeof(BleTxItem) + sizeof(void *))];
static struct rt_thread      ble_thread;
ALIGN(RT_ALIGN_SIZE) static uint8_t ble_stack[APP_BLE_TASK_STACK_SIZE];

/* 状态控制标志 */
static volatile rt_bool_t notify_enabled = RT_FALSE;
static volatile rt_bool_t stopping       = RT_FALSE;
static volatile rt_bool_t status_pending = RT_FALSE;
static uint16_t protocol_tx_sequence;
static rt_bool_t raw_half_full;
static int32_t raw_red;
static int32_t raw_ir;
static uint32_t raw_sequence;

/**
 * @brief  循环驱动 BLE 协议栈内部状态机直到空闲
 * @note   最多循环 64 轮，避免单次执行时间过长。
 */
static void run_stack_until_idle(void)
{
    uint8_t pass_count;

    for (pass_count = 0U; pass_count < 64U; pass_count++)
    {
        bt_run_thread();
        BLE_RunPassCount++;
        if (!is_bt_busy())
        {
            break;
        }
    }

    BLE_BusyAfterRun = is_bt_busy() ? 1U : 0U;
}

static void put_u32(uint8_t *data, uint32_t value)
{
    data[0]=(uint8_t)value;data[1]=(uint8_t)(value>>8);data[2]=(uint8_t)(value>>16);data[3]=(uint8_t)(value>>24);
}

static rt_bool_t queue_tx_item(const BleTxItem *item)
{
    if (!BLE_Ready || !item) return RT_FALSE;
    if (rt_mq_send(&tx_mq, item, sizeof(*item)) != RT_EOK) return RT_FALSE;
    ble_wakeup();
    return RT_TRUE;
}

static rt_bool_t queue_protocol(uint8_t command, uint16_t sequence,
                                const uint8_t *payload, uint8_t payload_length)
{
    BleTxItem item;
    memset(&item,0,sizeof(item));item.kind=BLE_TX_PACKET;
    item.length=AppProtocol_Build(command,sequence,payload,payload_length,item.data);
    return item.length ? queue_tx_item(&item) : RT_FALSE;
}

static void queue_ack(uint8_t command, uint16_t sequence, uint8_t value0, uint8_t value1)
{
    uint8_t payload[4]={command,0U,value0,value1};
    (void)queue_protocol(APP_CMD_ACK,sequence,payload,sizeof(payload));
}

void app_ble_post_error(uint8_t command, uint8_t error)
{
    uint8_t payload[2]={command,error};
    (void)queue_protocol(APP_CMD_ERROR,protocol_tx_sequence++,payload,sizeof(payload));
}

void app_ble_post_event(app_ble_event_t event, uint32_t value)
{
    uint8_t payload[7]={0xFFU,0U,(uint8_t)event,0U,0U,0U,0U};
    put_u32(&payload[3],value);
    (void)queue_protocol(APP_CMD_ACK,protocol_tx_sequence++,payload,sizeof(payload));
}

rt_bool_t app_ble_post_history(const app_record_t *record)
{
    BleTxItem item;
    if (!record || !ble_notifications_enabled()) return RT_FALSE;
    memset(&item,0,sizeof(item));item.kind=BLE_TX_HISTORY;item.length=sizeof(*record);
    memcpy(item.data,record,sizeof(*record));
    return queue_tx_item(&item);
}

void app_ble_post_raw(int32_t red, int32_t ir, uint32_t sequence)
{
    BleTxItem item;
    if (!app_device_raw_required()) { raw_half_full=RT_FALSE; return; }
    if (!raw_half_full)
    {
        raw_red=red;raw_ir=ir;raw_sequence=sequence;raw_half_full=RT_TRUE;return;
    }
    memset(&item,0,sizeof(item));item.kind=BLE_TX_PACKET;item.length=APP_PACKET_SIZE;
    AppPacket_Raw(raw_sequence,raw_red,raw_ir,red,ir,item.data);
    (void)queue_tx_item(&item);raw_half_full=RT_FALSE;
}

static void queue_device_status(uint16_t sequence)
{
    app_device_status_t status;uint8_t payload[10];
    app_device_get_status(&status);memset(payload,0,sizeof(payload));
    payload[0]=0U;payload[1]=2U;payload[2]=status.state;payload[3]=status.output_mode;
    payload[4]=(uint8_t)status.sample_period_sec;payload[5]=(uint8_t)(status.sample_period_sec>>8);
    payload[6]=(status.rtc_valid?0x01U:0U)|(status.record_enable?0x02U:0U)|(status.sensor_state?0x04U:0U)|(status.connected?0x08U:0U)|(status.subscribed?0x10U:0U);
    payload[7]=status.error;payload[8]=(uint8_t)status.timezone_min;payload[9]=(uint8_t)(status.timezone_min>>8);
    (void)queue_protocol(APP_CMD_GET_DEVICE_STATUS,sequence,payload,sizeof(payload));
    payload[0]=1U;payload[1]=2U;put_u32(&payload[2],0x20260909UL);put_u32(&payload[6],app_device_rtc_now());
    (void)queue_protocol(APP_CMD_GET_DEVICE_STATUS,sequence,payload,sizeof(payload));
}

static void queue_record_info(uint16_t sequence)
{
    app_record_info_t info;uint8_t bytes[24],payload[10];uint8_t part;
    app_record_get_info(&info);put_u32(&bytes[0],info.session_id);put_u32(&bytes[4],info.record_count);
    put_u32(&bytes[8],info.first_timestamp);put_u32(&bytes[12],info.last_timestamp);
    put_u32(&bytes[16],info.flash_used);put_u32(&bytes[20],info.flash_total);
    for(part=0U;part<3U;part++){payload[0]=part;payload[1]=3U;memcpy(&payload[2],&bytes[part*8U],8U);(void)queue_protocol(APP_CMD_GET_RECORD_INFO,sequence,payload,sizeof(payload));}
}

static uint16_t get_u16(const uint8_t *data){return (uint16_t)data[0]|((uint16_t)data[1]<<8);}
static uint32_t get_u32(const uint8_t *data){return (uint32_t)data[0]|((uint32_t)data[1]<<8)|((uint32_t)data[2]<<16)|((uint32_t)data[3]<<24);}

static void handle_command(const uint8_t *data, uint16_t length)
{
    app_protocol_frame_t frame;uint8_t error=APP_ERR_PROTOCOL;
    if(AppProtocol_Decode(data,length,&frame)!=0U){app_device_set_error(APP_ERR_PROTOCOL);app_ble_post_error(0U,APP_ERR_PROTOCOL);return;}
    if(frame.command==APP_CMD_TIME_SYNC&&frame.payload_length==6U)
    {error=app_device_rtc_set(get_u32(frame.payload),(int16_t)get_u16(&frame.payload[4]));if(!error)queue_ack(frame.command,frame.sequence,1U,0U);}
    else if(frame.command==APP_CMD_SET_OUTPUT_MODE&&frame.payload_length==1U)
    {error=app_device_set_output_mode(frame.payload[0]);if(!error)queue_ack(frame.command,frame.sequence,frame.payload[0],0U);}
    else if(frame.command==APP_CMD_SET_SAMPLE_PERIOD&&frame.payload_length==2U)
    {uint16_t period=get_u16(frame.payload);error=app_device_set_sample_period(period);if(!error)queue_ack(frame.command,frame.sequence,(uint8_t)period,(uint8_t)(period>>8));}
    else if(frame.command==APP_CMD_START_RECORD&&frame.payload_length==0U){error=app_record_start();if(!error)queue_ack(frame.command,frame.sequence,0U,0U);}
    else if(frame.command==APP_CMD_STOP_RECORD&&frame.payload_length==0U){error=app_record_stop();if(!error)queue_ack(frame.command,frame.sequence,0U,0U);}
    else if(frame.command==APP_CMD_ERASE_RECORD&&frame.payload_length==0U){error=app_record_erase();if(!error)queue_ack(frame.command,frame.sequence,0U,0U);}
    else if(frame.command==APP_CMD_GET_RECORD_INFO&&frame.payload_length==0U){queue_record_info(frame.sequence);error=0U;}
    else if(frame.command==APP_CMD_SYNC_RECORD&&frame.payload_length==2U){error=app_record_sync(get_u16(frame.payload));if(!error)queue_ack(frame.command,frame.sequence,0U,0U);}
    else if(frame.command==APP_CMD_GET_DEVICE_STATUS&&frame.payload_length==0U){queue_device_status(frame.sequence);error=0U;}
    else if(frame.command==APP_CMD_SET_MOTION_PARAM&&frame.payload_length==2U){error=SC7A20_SetMotionParameter(frame.payload[0],frame.payload[1]);if(!error)queue_ack(frame.command,frame.sequence,frame.payload[0],frame.payload[1]);}
    if(error)app_ble_post_error(frame.command,error==1U?APP_ERR_PROTOCOL:error);
}

/**
 * @brief  唤醒 BLE 任务处理就绪事件
 */
void ble_wakeup(void)
{
    if (BLE_Ready)
    {
        rt_sem_release(&work_sem);
    }
}

/**
 * @brief  查询当前通知是否使能
 */
rt_bool_t ble_notifications_enabled(void)
{
    return (notify_enabled && g_connnet_start && !stopping);
}

/**
 * @brief  订阅状态变更回调
 */
void ble_subscription_changed(rt_bool_t enabled)
{
    notify_enabled = (enabled && g_connnet_start);

    if (notify_enabled)
    {
        status_pending = RT_TRUE; /* 刚订阅时立即准备推送一次电池状态 */
    }

    app_device_set_subscribed(notify_enabled);

    ble_wakeup();
}

/**
 * @brief  将算法结果推入发送队列
 */
void app_ble_post(const SpO2_Result *result)
{
    if (!result || stopping || !ble_notifications_enabled() ||
        !app_device_algorithm_required())
    {
        return;
    }

    /* 
     * 队列满时弹出最旧结果并写入新结果：
     * 保证网络拥堵或偶尔重传时不造成内存膨胀，且手机端始终接收到最新时刻的生理数据。
     */
    if (rt_mq_send(&result_mq, result, sizeof(*result)) != RT_EOK)
    {
        SpO2_Result old_result;
        (void)rt_mq_recv(&result_mq, &old_result, sizeof(old_result), 0);
        (void)rt_mq_send(&result_mq, result, sizeof(*result));
    }

    ble_wakeup();
}

/**
 * @brief  蓝牙协议栈底层事件异步回调
 */
static void bt_event_callback(bt_event_enum event, const uint8_t *data,
                              uint32_t size, uint32_t characteristic)
{
    (void)data;
    (void)size;
    (void)characteristic;

    if (event == BT_EVENT_CONNECTED)
    {
        BLE_ConnectCount++;
        gBT_STS        = BT_CONNECTED;
        notify_enabled = RT_FALSE;

        app_device_set_connected(RT_TRUE);
    }
    else if (event == BT_EVENT_DISCONNECTD)
    {
        BLE_DisconnectCount++;
        gBT_STS        = BT_DISCONNECTED;
        notify_enabled = RT_FALSE;

        app_device_set_connected(RT_FALSE);
    }
    else if (event == BT_EVENT_RCV_DATA)
    {
        handle_command(data, (uint16_t)size);
    }

    ble_wakeup();
}

static rt_bool_t notify_packet(const uint8_t *Packet, uint8_t Length)
{
    uint32_t bytes_sent;

    BLE_NotifyAttempts++;
    bytes_sent = bt_snd_data((uint8_t *)Packet, Length,
                             USER_IDX_WRITE_NOTIFY_VAL);
    if (bytes_sent == Length)
    {
        BLE_NotifyQueued++;
        return RT_TRUE;
    }

    BLE_NotifyRejected++;
    return RT_FALSE;
}

static void process_stack_events(void)
{
    run_stack_until_idle();

    if (flag_bt_irq)
    {
        flag_bt_irq = 0U;
        bt_handler();
        run_stack_until_idle();
    }

    if (wakeup_flag)
    {
        wakeup_flag = 0U;
        ble_status_monitor();
    }
}

static void send_pending_battery(uint32_t Sequence)
{
    uint8_t packet[APP_PACKET_SIZE];

    if (!ble_notifications_enabled() || !status_pending)
    {
        return;
    }

    AppPacket_Battery(Sequence, packet);
    if (notify_packet(packet, APP_PACKET_SIZE))
    {
        status_pending = RT_FALSE;
    }
    run_stack_until_idle();
}

static void send_tx_item(const BleTxItem *item)
{
    uint8_t packet[APP_PROTOCOL_MAX_FRAME];
    if (!ble_notifications_enabled() || !item) return;
    if (item->kind == BLE_TX_PACKET)
    {
        (void)notify_packet(item->data,item->length);
        return;
    }
    if (item->kind == BLE_TX_HISTORY && item->length == sizeof(app_record_t))
    {
        const app_record_t *record=(const app_record_t *)item->data;uint8_t payload[10];uint8_t part;
        for(part=0U;part<2U;part++)
        {
            payload[0]=part;payload[1]=2U;memcpy(&payload[2],&item->data[part*8U],8U);
            uint8_t length=AppProtocol_Build(APP_CMD_SYNC_RECORD,record->sequence,payload,sizeof(payload),packet);
            if(!notify_packet(packet,length))break;
            run_stack_until_idle();
        }
    }
}

static void send_result_group(const SpO2_Result *Result, uint8_t *ResultCount)
{
    uint8_t packet[APP_PACKET_SIZE];

    AppPacket_Result(Result, packet);
    if (!notify_packet(packet, APP_PACKET_SIZE))
    {
        return;
    }

    (*ResultCount)++;
    run_stack_until_idle();

    if (*ResultCount >= BATTERY_PACKET_INTERVAL)
    {
        run_stack_until_idle();
        AppPacket_Battery(Result->seq, packet);
        if (notify_packet(packet, APP_PACKET_SIZE))
        {
            *ResultCount = 0U;
        }
    }
}

/**
 * @brief  BLE 应用任务主体执行函数
 */
static void ble_thread_entry(void *parameter)
{
    bt_attr_param init_params;
    SpO2_Result   current_result;
    BleTxItem     tx_item;
    uint8_t       result_packet_count = 0U;
    uint32_t      latest_sequence_id  = 0U;

    (void)parameter;

    /* 1. 初始化蓝牙协议栈与 GATT 服务表 */
    memset(&init_params, 0, sizeof(init_params));
    memcpy(init_params.device_name, BLE_NAME, sizeof(BLE_NAME) - 1U);
    memcpy(init_params.device_addr, BLE_ADDR, sizeof(BLE_ADDR) - 1U);

    /* 广播扫描响应数据 */
    init_params.scan_rsp_data[0]   = 2U;
    init_params.scan_rsp_data[1]   = 0x0AU;
    init_params.scan_rsp_data[2]   = 0x00U;
    init_params.scan_rsp_data_len  = 3U;

    /* 注册主服务与通知特征 */
    init_params.service[0].svc_uuid                 = BLE_SERVICE_UUID;
    init_params.service[0].character[0].uuid        = USER_IDX_WRITE_NOTIFY_CHAR;
    init_params.service[0].character[0].permission  = BT_WRITE_PERM | BT_NTF_PERM;

    bt_ware_init(&init_params, bt_event_callback);
    BLE_Ready = RT_TRUE;

    /* 事件循环：处理协议栈 -> 发送数据 -> 阻塞等待。 */
    while (!stopping)
    {
        app_power_lock(PM_BLE);
        process_stack_events();
        while (ble_notifications_enabled() &&
               rt_mq_recv(&tx_mq,&tx_item,sizeof(tx_item),0)==RT_EOK)
        {
            send_tx_item(&tx_item);
            run_stack_until_idle();
        }
        send_pending_battery(latest_sequence_id);

        while (ble_notifications_enabled() &&
               (rt_mq_recv(&result_mq, &current_result, sizeof(current_result), 0) == RT_EOK))
        {
            latest_sequence_id = current_result.seq;
            send_result_group(&current_result, &result_packet_count);
            run_stack_until_idle();
        }

        run_stack_until_idle();
        flag_bt_enter_sleep = 1U;
        app_power_unlock(PM_BLE);
        rt_sem_take(&work_sem, RT_WAITING_FOREVER);
        flag_bt_enter_sleep = 0U;
    }

    BLE_Ready = RT_FALSE;
}

/**
 * @brief  初始化并启动 BLE 应用任务
 */
int app_ble_init(void)
{
    if ((rt_sem_init(&work_sem, "ble", 0, RT_IPC_FLAG_FIFO) != RT_EOK) ||
        (rt_mq_init(&result_mq, "bleres", result_pool, sizeof(SpO2_Result),
                    sizeof(result_pool), RT_IPC_FLAG_FIFO) != RT_EOK) ||
        (rt_mq_init(&tx_mq, "bletx", tx_pool, sizeof(BleTxItem),
                    sizeof(tx_pool), RT_IPC_FLAG_FIFO) != RT_EOK))
    {
        return -1;
    }

    if (rt_thread_init(&ble_thread, "ble", ble_thread_entry, RT_NULL,
                       ble_stack, sizeof(ble_stack), APP_BLE_TASK_PRIORITY,
                       APP_BLE_TASK_TIMESLICE) != RT_EOK)
    {
        return -1;
    }

    return rt_thread_startup(&ble_thread);
}

/**
 * @brief  停止 BLE 任务
 */
void app_ble_stop(void)
{
    stopping       = RT_TRUE;
    notify_enabled = RT_FALSE;
    ble_wakeup();
}

/**
 * @brief  查询 BLE 协议栈是否空闲
 */
rt_bool_t app_ble_idle(void)
{
    return (BLE_Ready && flag_bt_enter_sleep && !flag_bt_irq &&
            !wakeup_flag && !is_bt_busy());
}

/**
 * @brief  查询 BLE 当前是否处于物理连接中
 */
rt_bool_t app_ble_connected(void)
{
    return g_connnet_start ? RT_TRUE : RT_FALSE;
}

/**
 * @brief  查询当前是否允许进入 STOP0
 */
rt_bool_t app_ble_stop_allowed(void)
{
    return (app_ble_idle() && !app_ble_connected());
}
