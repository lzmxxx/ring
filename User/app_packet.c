#include "app_packet.h"
#include "CW2015.h"
#include <string.h>

static uint16_t ScaleU16(float Value, float Scale)
{
    float scaled = Value * Scale;

    if (scaled <= 0.0f) return 0U;
    if (scaled >= 65535.0f) return 65535U;
    return (uint16_t)(scaled + 0.5f);
}

static void PutU16(uint8_t *Data, uint16_t Value)
{
    Data[0] = (uint8_t)Value;
    Data[1] = (uint8_t)(Value >> 8);
}

static void PutU32(uint8_t *Data, uint32_t Value)
{
    Data[0] = (uint8_t)Value;
    Data[1] = (uint8_t)(Value >> 8);
    Data[2] = (uint8_t)(Value >> 16);
    Data[3] = (uint8_t)(Value >> 24);
}

void AppPacket_Result(const SpO2_Result *Result, uint8_t Packet[APP_PACKET_SIZE])
{
    memset(Packet, 0, APP_PACKET_SIZE);
    Packet[0] = 0xA5U;
    Packet[1] = 0x01U;
    Packet[2] = (Result->valid ? 0x01U : 0U) |
                (Result->calibrated ? 0x02U : 0U) |
                (Result->motion ? 0x04U : 0U);
    Packet[3] = Result->quality;

    PutU32(&Packet[4], Result->seq);
    PutU32(&Packet[8], Result->timestamp);
    PutU16(&Packet[12], ScaleU16(Result->spo2, 100.0f));
    PutU16(&Packet[14], ScaleU16(Result->hr, 10.0f));
    PutU16(&Packet[16], ScaleU16(Result->pi, 100.0f));
    PutU16(&Packet[18], (uint16_t)Result->temperature);
}

void AppPacket_Battery(uint32_t Sequence, uint8_t Packet[APP_PACKET_SIZE])
{
    CW2015_Data battery;

    memset(Packet, 0, APP_PACKET_SIZE);
    Packet[0] = 0xA5U;
    Packet[1] = 0x02U;
    PutU32(&Packet[6], Sequence);

    if (CW2015_ReadData(&battery) == 0U)
    {
        Packet[2] = 0x01U;
        Packet[3] = battery.Capacity;
        PutU16(&Packet[4], battery.VoltageMv);
    }

    /* 10~19 为协议保留字节，保持为零，不再夹带调试变量。 */
}

void AppPacket_Raw(uint32_t FirstSequence, uint32_t Timestamp, int32_t Red0, int32_t Ir0,
                   int32_t Red1, int32_t Ir1, uint8_t Packet[APP_PACKET_SIZE])
{
    memset(Packet, 0, APP_PACKET_SIZE);
    Packet[0] = 0xA5U;
    Packet[1] = 0x04U;
    Packet[2] = 2U;
    Packet[3] = 75U;
    PutU32(&Packet[4], FirstSequence);
    PutU32(&Packet[8], Timestamp);
    /* 20位ADC右移4位装入int16；上位机左移4位恢复量级。 */
    PutU16(&Packet[12], (uint16_t)(int16_t)(Red0 >> 4));
    PutU16(&Packet[14], (uint16_t)(int16_t)(Ir0 >> 4));
    PutU16(&Packet[16], (uint16_t)(int16_t)(Red1 >> 4));
    PutU16(&Packet[18], (uint16_t)(int16_t)(Ir1 >> 4));
}
