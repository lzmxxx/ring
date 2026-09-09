/**
 * @file  app_protocol.h
 * @brief BLE 应用层统一帧编解码
 */
#ifndef __APP_PROTOCOL_H__
#define __APP_PROTOCOL_H__

#include <stdint.h>

#define APP_PROTOCOL_VERSION      1U
#define APP_PROTOCOL_MAX_FRAME    20U
#define APP_PROTOCOL_MAX_PAYLOAD  10U

#define APP_CMD_TIME_SYNC          0x01U
#define APP_CMD_SET_OUTPUT_MODE    0x10U
#define APP_CMD_SET_SAMPLE_PERIOD  0x11U
#define APP_CMD_START_RECORD       0x20U
#define APP_CMD_STOP_RECORD        0x21U
#define APP_CMD_GET_RECORD_INFO    0x22U
#define APP_CMD_SYNC_RECORD        0x23U
#define APP_CMD_ERASE_RECORD       0x24U
#define APP_CMD_GET_DEVICE_STATUS  0x30U
#define APP_CMD_SET_MOTION_PARAM   0x40U
#define APP_CMD_ACK                0x80U
#define APP_CMD_ERROR              0x81U

typedef struct
{
    uint8_t command;
    uint16_t sequence;
    uint16_t payload_length;
    uint8_t payload[APP_PROTOCOL_MAX_PAYLOAD];
} app_protocol_frame_t;

uint16_t AppProtocol_Crc16(const uint8_t *data, uint16_t length);
uint8_t AppProtocol_Decode(const uint8_t *data, uint16_t length,
                           app_protocol_frame_t *frame);
uint8_t AppProtocol_Build(uint8_t command, uint16_t sequence,
                          const uint8_t *payload, uint8_t payload_length,
                          uint8_t output[APP_PROTOCOL_MAX_FRAME]);

#endif /* __APP_PROTOCOL_H__ */
