#include "app_packet.h"
#include "CW2015.h"
#include "SC7A20.h"
#include <string.h>

extern volatile uint8_t g_cw2015_init_status;

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

void AppPacket_Algorithm(const SpO2_Result *Result, uint8_t Packet[APP_PACKET_SIZE])
{
    memset(Packet, 0, APP_PACKET_SIZE);
    Packet[0] = 0xA5U;
    Packet[1] = 0x05U;
    Packet[2] = (Result->time_valid ? 0x01U : 0U) |
                (Result->fft_valid ? 0x02U : 0U);
    Packet[3] = Result->time_pair_count;

    PutU32(&Packet[4], Result->seq);
    PutU16(&Packet[8],  ScaleU16(Result->spo2_time, 100.0f));
    PutU16(&Packet[10], ScaleU16(Result->spo2_fft, 100.0f));
    PutU16(&Packet[14], ScaleU16(Result->ratio_time, 10000.0f));
    PutU16(&Packet[16], ScaleU16(Result->ratio_fft, 10000.0f));
}

void AppPacket_Acceleration(uint32_t Sequence, uint8_t Packet[APP_PACKET_SIZE])
{
    SC7A20_Acceleration acceleration;

    memset(Packet, 0, APP_PACKET_SIZE);
    Packet[0] = 0xA5U;
    Packet[1] = 0x03U;
    PutU32(&Packet[4], Sequence);

    if (SC7A20_ReadAcceleration(&acceleration) == 0U)
    {
        Packet[2] = 0x01U;
        PutU16(&Packet[8],  (uint16_t)acceleration.Xmg);
        PutU16(&Packet[10], (uint16_t)acceleration.Ymg);
        PutU16(&Packet[12], (uint16_t)acceleration.Zmg);
    }
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

    Packet[10] = g_cw2015_i2c_ack;
    Packet[11] = g_cw2015_version;
    Packet[12] = g_cw2015_init_status;
    Packet[16] = g_cw2015_address;
    Packet[17] = g_i2c2_device_count;
}

static void PutS20(uint8_t *Data, int32_t Value)
{
    uint32_t encoded = (uint32_t)Value & 0x000FFFFFUL;
    Data[0] = (uint8_t)encoded;
    Data[1] = (uint8_t)(encoded >> 8);
    Data[2] = (uint8_t)(encoded >> 16);
}

void AppPacket_Raw(uint32_t FirstSequence, int32_t Red0, int32_t Ir0,
                   int32_t Red1, int32_t Ir1, uint8_t Packet[APP_PACKET_SIZE])
{
    memset(Packet, 0, APP_PACKET_SIZE);
    Packet[0] = 0xA5U;
    Packet[1] = 0x04U;
    Packet[2] = 2U;
    Packet[3] = 75U;
    PutU32(&Packet[4], FirstSequence);
    PutS20(&Packet[8], Red0);
    PutS20(&Packet[11], Ir0);
    PutS20(&Packet[14], Red1);
    PutS20(&Packet[17], Ir1);
}
