#ifndef __APP_PACKET_H__
#define __APP_PACKET_H__

#include <stdint.h>
#include "SpO2.h"

#define APP_PACKET_SIZE 20U

void AppPacket_Result(const SpO2_Result *Result, uint8_t Packet[APP_PACKET_SIZE]);
void AppPacket_Algorithm(const SpO2_Result *Result, uint8_t Packet[APP_PACKET_SIZE]);
void AppPacket_Acceleration(uint32_t Sequence, uint8_t Packet[APP_PACKET_SIZE]);
void AppPacket_Battery(uint32_t Sequence, uint8_t Packet[APP_PACKET_SIZE]);
void AppPacket_Raw(uint32_t FirstSequence, int32_t Red0, int32_t Ir0,
                   int32_t Red1, int32_t Ir1, uint8_t Packet[APP_PACKET_SIZE]);

#endif
