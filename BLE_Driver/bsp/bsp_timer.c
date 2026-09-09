#include "app_power.h"

/** ----------------------------------------------------------------------------
 *         Nationz Technology Software Support  -  NATIONZ  -
 * -----------------------------------------------------------------------------
 * Copyright (c) 2019, Nationz Corporation  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the disclaiimer below.
 * 
 * - Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the disclaimer below in the documentation and/or
 * other materials provided with the distribution. 
 * 
 * Nationz's name may not be used to endorse or promote products derived from
 * this software without specific prior written permission. 
 * 
 * DISCLAIMER: THIS SOFTWARE IS PROVIDED BY NATIONZ "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * -----------------------------------------------------------------------------
 */

#include "bsp_timer.h"
#include "Eif_timer.h"

uint32_t overtime_count = 0;
 
/********************************
declaraction: TIM3 configuration
parameter   : arr[in]: reload value 
*             psc[in]:division factor
*******************************/
void TIM3_config(uint16_t arr, uint16_t psc)
{
    TIM_TimeBaseInitType TIM_TimeBaseStructure;
    NVIC_InitType NVIC_InitStructure;

    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_TIM3, ENABLE); 

    TIM_TimeBaseStructure.Period        = arr;   
    TIM_TimeBaseStructure.Prescaler     = psc; 
    TIM_TimeBaseStructure.ClkDiv        = 0; 
    TIM_TimeBaseStructure.CntMode       = TIM_CNT_MODE_UP;  
    TIM_InitTimeBase(TIM3, &TIM_TimeBaseStructure); 

    TIM_ConfigInt(TIM3, TIM_INT_UPDATE, ENABLE);
    TIM_ClrIntPendingBit(TIM3, TIM_INT_UPDATE); 

    NVIC_InitStructure.NVIC_IRQChannel                      = TIM3_IRQn; 
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority    = 2; 
    NVIC_InitStructure.NVIC_IRQChannelSubPriority           = 0; 
    NVIC_InitStructure.NVIC_IRQChannelCmd                   = ENABLE;
    NVIC_Init(&NVIC_InitStructure);  

    TIM_Enable(TIM3, ENABLE); 
}

/********************************
declaraction: enable or disable timer3
parameter   : flag[in]:0-disable,1-enable
*******************************/
void TIM3_IRQ_enable(uint8_t flag)
{
    if (flag == 1)
    {
        TIM_ConfigInt(TIM3, TIM_INT_UPDATE, ENABLE);
        TIM_ClrIntPendingBit(TIM3, TIM_INT_UPDATE); 
        TIM_Enable(TIM3, ENABLE);
    }
    else
    {
        TIM_ConfigInt(TIM3, TIM_INT_UPDATE, DISABLE);
        TIM_Enable(TIM3, DISABLE);
    }
}

/********************************
declaraction: set timeout value, 10ms timing base
*******************************/
void TIM3_set_timeout(uint32_t count)
{
    overtime_count = count;
    TIM_ClrIntPendingBit(TIM3, TIM_INT_UPDATE);
    TIM_ConfigInt(TIM3, TIM_INT_UPDATE, ENABLE);
    TIM_Enable(TIM3, ENABLE);  
}

/********************************
declaraction: timer3 interrupt handle
* note: enter interrupt every 10ms
*******************************/
void TIM3_IRQHandler(void)   
{
    if (TIM_GetIntStatus(TIM3, TIM_INT_UPDATE) != RESET) 
    {
        TIM_ClrIntPendingBit(TIM3, TIM_INT_UPDATE);
        if (overtime_count > 0)
        {
            overtime_count--;
        }
        eif_timer_isr();

        
    }    
}

/******************************
*function    : return timeout remaining value, unit ms
******************************/
uint32_t TIM3_get_time(void)
{
    return overtime_count * 10;
}
