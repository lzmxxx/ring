/*****************************************************************************
 * Copyright (c) 2019, Nations Technologies Inc.
 *
 * All rights reserved.
 * ****************************************************************************
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the disclaimer below.
 *
 * Nations' name may not be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * DISCLAIMER: THIS SOFTWARE IS PROVIDED BY NATIONS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * DISCLAIMED. IN NO EVENT SHALL NATIONS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ****************************************************************************/

/**
 * @file n32wb452_data_fifo.c
 * @author Nations
 * @version v1.0.0
 *
 * @copyright Copyright (c) 2019, Nations Technologies Inc. All rights reserved.
 */

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include <stdio.h>
#include "n32wb452.h"
#include "n32wb452_data_fifo.h"
#include "n32wb452_log_level.h"
#include "rwble_config.h"


#define APP_DATA_USB_MIN (64)  
#define APP_DATA_BLE_MIN (64)

#define APP_DATA_MIN (APP_DATA_USB_MIN + APP_DATA_BLE_MIN)
#define APP_DATA_EXD_SIZE (512 + 160)

//#define APP_DATA_SIZE (APP_DATA_MIN + APP_DATA_EXD_SIZE + 1) /* no more than 128 bytes */
#define APP_DATA_SIZE (512 + 4) /* must more than 128 bytes */

#define APP_START_PRINTER_SIZE (480)

FIFO_NEW(APP_BtRcvDataBuffer, 1, APP_DATA_SIZE);

static int32_t fifo_write_item(void* pvFifo, const void* pvdata);
static int32_t fifo_read_item(void* pvFifo, void* pvdata);
static int32_t fifo_clear_all(void* pvFifo);

/************************************************************************
* Function Name : fifo_init
* Description   :initialize fifo size in sram
* Date      : 2019/08/01
* Parameter :   void
* Return Code   :void
* author Nations
*************************************************************************/
void fifo_init(void)
{
    ble_log(BLE_DEBUG,"total fifo size=%d,start size = %d.\r\n", APP_DATA_SIZE, APP_START_PRINTER_SIZE);
}


/************************************************************************
* Function Name : fifo_write
* Description   :write data to fifo
* Date      : 2019/03/13
* Parameter :   buf: data buffer pointer
* Parameter :   size .
* Return Code   :E_OVERFLOW, E_OK
* author Nations
*************************************************************************/
int32_t fifo_write(const uint8_t* buf, uint32_t size)
{
    uint32_t i, overflow = 0;

    if ((!buf) || (!size))
    {
        return 1;
    }

    for (i = 0; i < size; i++)
    {
        if (!FIFO_IS_FULL(APP_BtRcvDataBuffer))
        {
            fifo_write_item(APP_BtRcvDataBuffer, buf + i);
        }
        else
        {
            ble_log(BLE_DEBUG,"wr-ferr.\r\n");
            overflow = 1;
            break;
        }
    }

    if (overflow)
    {
        ble_log(BLE_DEBUG,"\r\nCMD&DATA:FIFO OVERFLOW!\r\n");

        fifo_clear_all(APP_BtRcvDataBuffer);
        return E_OVERFLOW;
    }
    else
    {
        return E_OK;
    }
}

/************************************************************************
* Function Name : fifo_read
* Description   :read data from fifo
* Date      : 2019/03/13
* Parameter :   [OUT]buf: read data buffer pointer
* Parameter :   [IN/OUT]size; read data size
* Return Code   :read data size
* author Nations
*************************************************************************/
uint32_t fifo_read(uint8_t* buf, uint32_t* size)
{
    uint32_t i;
    //uint32_t empty = 0;

    if ((!buf) || (!size))
    {
        return 0;
    }

    for (i = 0; i < *size; i++)
    {
        if (!FIFO_IS_EMPTY(APP_BtRcvDataBuffer))
        {
            fifo_read_item(APP_BtRcvDataBuffer, buf + i);
            //log_debuginfo("(%c,%02x) ", *(buf + i), *(buf + i));
            //log_debuginfo("(%02x) ", *(buf + i));
        }
        else
        {
            //empty = 1;
            break;
        }
    }

    return i;
//    if (empty)
//    {
//        //log_debuginfo("CMD&DATA:FIFO is empty!\r\n");
//        return E_NODATA;
//    }
//    else
//    {
//        return E_OK;
//    }
}

/************************************************************************
* Function Name : fifo_is_full
* Description   :whether the fifo is full or not
* Date      : 2019/07/15
* Parameter :   void
* Return Code   :0:not full;        not 0: full
* author Nations
*************************************************************************/
int32_t fifo_is_full(void)
{
    if (FIFO_GET_FREE(APP_BtRcvDataBuffer) >= APP_DATA_MIN)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}

/************************************************************************
* Function Name : fifo_is_usb_full
* Description   :whether the fifo can store a usb endpoint data
* Date      : 2019/07/26
* Parameter :   void
* Return Code   :0:not full;        not 0: full
* author Nations
*************************************************************************/
int32_t fifo_is_usb_full(void)
{
    if (FIFO_GET_FREE(APP_BtRcvDataBuffer) >= APP_DATA_USB_MIN)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}

/************************************************************************
* Function Name : fifo_is_bt_full
* Description   :whether the fifo can store bluetooth signal transfer data
* Date      : 2019/07/26
* Parameter :   void
* Return Code   :0:not full;        not 0: full
* author Nations
*************************************************************************/
int32_t fifo_is_bt_full(void)
{
    if (FIFO_GET_FREE(APP_BtRcvDataBuffer) >= APP_DATA_BLE_MIN)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}


/************************************************************************
* Function Name : fifo_is_enough
* Description   :whether the fifo is sufficient for data printing
* Date      : 2019/07/24
* Parameter :   void
* Return Code   :0:not have enough data        not 0: have enough data
* author Nations
*************************************************************************/
int32_t fifo_is_enough(void)
{
    if (FIFO_GET_TOTAL(APP_BtRcvDataBuffer) >= APP_START_PRINTER_SIZE)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

uint32_t get_fifo_total(void)
{
    return FIFO_GET_TOTAL(APP_BtRcvDataBuffer);
}


uint32_t get_fifo_free(void)
{
    return FIFO_GET_FREE(APP_BtRcvDataBuffer);
}


/************************************************************************
* Function Name : fifo_is_remain_data
* Description   :whether the fifo is sufficient for remain data printing
* Date      : 2019/08/01
* Parameter :   void
* Return Code   :0:not have remain data        not 0: have remain data
* author Nations
*************************************************************************/
int32_t fifo_is_remain_data(void)
{
    if (FIFO_GET_TOTAL(APP_BtRcvDataBuffer) > 0)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}


/************************************************************************
* Function Name : fifo_clear_all
* Description   :clear fifo buffer
* Date      : 2019/01/30
* Parameter :   pvFifo: fifo buffer
* Return Code   :error id
* author Nations
*************************************************************************/
static int32_t fifo_clear_all(void* pvFifo)
{
    prFIFO_ENTRY pstFifo = NULL;

    if (pvFifo == NULL)
    {
        return E_PAR;
    }
    pstFifo = (prFIFO_ENTRY)pvFifo;

    if (FIFO_IS_EMPTY(pstFifo))
    {
        return E_OK;
    }

    pstFifo->head      = 0U;
    pstFifo->tail      = 0U;
    pstFifo->readBusy  = 0U;
    pstFifo->writeBusy = 0U;

    return E_OK;
}

/************************************************************************
* Function Name : fifo_clear
* Description   :clear all fifo data
* Date      : 2019/04/28
* Parameter :   void
* Return Code   :void
* author Nations
*************************************************************************/
void fifo_clear(void)
{
    fifo_clear_all(APP_BtRcvDataBuffer);
}

/************************************************************************
* Function Name : fifo_clear_all
* Description   :clear fifo buffer
* Date      : 2019/01/30
* Parameter :   pvFifo: fifo buffer
* Return Code   :error id
* author Nations
*************************************************************************/
static int32_t fifo_read_item(void* pvFifo, void* pvdata)
{
    prFIFO_ENTRY pstFifo = NULL;
        
    if (pvFifo == NULL)
    {
        return E_PAR;
    }
    if (FIFO_IS_EMPTY(pvFifo))
    {
        return E_NODATA;
    }
    pstFifo = (prFIFO_ENTRY)pvFifo;

    if (pstFifo->readBusy == 1U)
    {
        return E_PAR;
    }
    pstFifo->readBusy = 1U;
    memcpy(pvdata, pstFifo->buf + (pstFifo->head * pstFifo->size), pstFifo->size);
    pstFifo->head++;
    if (pstFifo->head >= pstFifo->length)
    {
        pstFifo->head = 0U;
    }

    pstFifo->readBusy = 0U;

    return E_OK;
}

/************************************************************************
* Function Name : fifo_check_item
* Description   :check fifo item
* Date      : 2019/04/29
* Parameter :   pvFifo: fifo buffer
* Return Code   :error id
* author Nations
*************************************************************************/
int32_t fifo_check_item(uint32_t inogesize, uint8_t const* headerbuf, uint32_t checkitemnum, uint32_t itemsize, uint32_t times)
{
    uint32_t i, j;
    uint8_t item[4]   = {0};
    uint32_t err_flag = 1;
    FIFO_ENTRY backup = {.size = 0};

    /* restore info */
    memcpy(&backup, ((prFIFO_ENTRY)APP_BtRcvDataBuffer), sizeof(backup));
    if (inogesize)
    {
        for (j = 0; j < inogesize; j++)
        {
            if (FIFO_IS_EMPTY(APP_BtRcvDataBuffer))
            {
                memcpy(((prFIFO_ENTRY)APP_BtRcvDataBuffer), &backup, sizeof(backup));
                return E_WAIT;
            }
            if (E_OK != fifo_read_item(APP_BtRcvDataBuffer, item))
            {
                memcpy(((prFIFO_ENTRY)APP_BtRcvDataBuffer), &backup, sizeof(backup));
                return E_WAIT;
            }
        }
    }

    for (i = 0; i < times; i++)
    {
        for (j = 0; j < itemsize; j++)
        {
            if (FIFO_IS_EMPTY(APP_BtRcvDataBuffer))
            {
                break;
            }
            if (E_OK != fifo_read_item(APP_BtRcvDataBuffer, item))
            {
                memcpy(((prFIFO_ENTRY)APP_BtRcvDataBuffer), &backup, sizeof(backup));
                return E_WAIT;
            }

            if (j < checkitemnum)
            {
                if (headerbuf[j] != item[0])
                {
                    break;
                }
            }
        }

        if (j < itemsize)
        {
            break;
        }
    }
    if (i >= times)
    {
        err_flag = 0;
    }

    if (err_flag)
    {
        memcpy(((prFIFO_ENTRY)APP_BtRcvDataBuffer), &backup, sizeof(backup));
        return E_WAIT;
    }
    else
    {
        memcpy(((prFIFO_ENTRY)APP_BtRcvDataBuffer), &backup, sizeof(backup));

        return E_OK;
    }
}

/************************************************************************
* Function Name : fifo_clear_all
* Description   :clear fifo buffer
* Date      : 2019/01/30
* Parameter :   pvFifo: fifo buffer
* Return Code   :error id
* author Nations
*************************************************************************/
static int32_t fifo_write_item(void* pvFifo, const void* pvdata)
{
    prFIFO_ENTRY pstFifo = NULL;

    if (pvFifo == NULL)
    {
        return E_PAR;
    }
    if (FIFO_IS_FULL(pvFifo))
    {
        return E_MSGFULL;
    }
    pstFifo = (prFIFO_ENTRY)pvFifo;

    if (pstFifo->writeBusy == 1U)
    {
        return E_PAR;
    }

    pstFifo->writeBusy = 1U;

    memcpy(pstFifo->buf + (pstFifo->tail * pstFifo->size), pvdata, pstFifo->size);
    pstFifo->tail++;
    if (pstFifo->tail >= pstFifo->length)
    {
        pstFifo->tail = 0U;
    }

    pstFifo->writeBusy = 0U;

    return E_OK;
}

