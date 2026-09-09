/**
 * @file    app_protocol.c
 * @brief   统一 BLE 应用帧的编码、解码与 CRC16 校验
 */
#include "app_protocol.h"
#include <string.h>

uint16_t AppProtocol_Crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t bit;

    while (length--)
    {
        crc ^= (uint16_t)(*data++) << 8;
        for (bit = 0U; bit < 8U; bit++)
        {
            crc = (crc & 0x8000U) ?
                  (uint16_t)((crc << 1) ^ 0x1021U) :
                  (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint8_t AppProtocol_Decode(const uint8_t *data, uint16_t length,
                           app_protocol_frame_t *frame)
{
    uint16_t payload_length;
    uint16_t expected_crc;

    if (!data || !frame || length < 10U || data[0] != 0xAAU ||
        data[1] != 0x55U || data[2] != APP_PROTOCOL_VERSION)
    {
        return 1U;
    }

    payload_length = (uint16_t)data[6] | ((uint16_t)data[7] << 8);
    if (payload_length > APP_PROTOCOL_MAX_PAYLOAD ||
        length != payload_length + 10U)
    {
        return 1U;
    }

    expected_crc = (uint16_t)data[length - 2U] |
                   ((uint16_t)data[length - 1U] << 8);
    if (AppProtocol_Crc16(data, (uint16_t)(length - 2U)) != expected_crc)
    {
        return 1U;
    }

    frame->command = data[3];
    frame->sequence = (uint16_t)data[4] | ((uint16_t)data[5] << 8);
    frame->payload_length = payload_length;
    if (payload_length != 0U)
    {
        memcpy(frame->payload, &data[8], payload_length);
    }
    return 0U;
}

uint8_t AppProtocol_Build(uint8_t command, uint16_t sequence,
                          const uint8_t *payload, uint8_t payload_length,
                          uint8_t output[APP_PROTOCOL_MAX_FRAME])
{
    uint8_t frame_length;
    uint16_t crc;

    if (!output || payload_length > APP_PROTOCOL_MAX_PAYLOAD)
    {
        return 0U;
    }

    frame_length = (uint8_t)(payload_length + 10U);
    output[0] = 0xAAU;
    output[1] = 0x55U;
    output[2] = APP_PROTOCOL_VERSION;
    output[3] = command;
    output[4] = (uint8_t)sequence;
    output[5] = (uint8_t)(sequence >> 8);
    output[6] = payload_length;
    output[7] = 0U;
    if (payload_length != 0U && payload)
    {
        memcpy(&output[8], payload, payload_length);
    }

    crc = AppProtocol_Crc16(output, (uint16_t)(frame_length - 2U));
    output[frame_length - 2U] = (uint8_t)crc;
    output[frame_length - 1U] = (uint8_t)(crc >> 8);
    return frame_length;
}
