#ifndef __SC7A20_H__
#define __SC7A20_H__

#include <stdint.h>

typedef struct
{
    int16_t Xmg;
    int16_t Ymg;
    int16_t Zmg;
} SC7A20_Acceleration;

/* +/-2g low-power mode: one threshold LSB is 16mg. */
#define SC7A20_MOTION_THRESHOLD     8U  /* 128mg */
#define SC7A20_MOTION_DURATION      2U  /* 80ms at 25Hz */

uint8_t SC7A20_Init(void);
void SC7A20_DeInit(void);
uint8_t SC7A20_SetMotionParameter(uint8_t threshold, uint8_t duration);
void SC7A20_InterruptReInit(void);
uint8_t SC7A20_ReadAcceleration(SC7A20_Acceleration *acceleration);
uint8_t SC7A20_GetAndClearMotion(void);
void SC7A20_MotionIRQHandler(void);

#endif
