/**
 * @file    MySPI2.c
 * @brief   SPI2 硬件外设底层驱动实现 (SC7A20 加速度计专用)
 * 
 * 模块职责：
 * 1. 初始化 N32WB452 SPI2 硬件控制器及对应引脚；
 * 2. 提供高效、带超时保护的单字节全双工硬件传输接口 `MySPI2_TransferByte`；
 * 3. 供上层 `SC7A20.c` 驱动调用以完成加速度计寄存器读写与 FIFO 提取。
 * 
 * 硬件连接与时序规范：
 * - PB13: SPI2_SCK  (时钟，复用推挽输出，空闲为低电平)
 * - PB14: SPI2_MISO (主机输入，浮空输入)
 * - PB15: SPI2_MOSI (主机输出，复用推挽输出)
 * - 时钟极性与相位：CPOL=0, CPHA=0 (SPI Mode 0，第一个跳变沿采样，适合 SC7A20)；
 * - 波特率分频：PCLK1 (32MHz) / 16 = 2MHz，保证高速读取的同时信号完整性良好。
 * 
 * 中断与线程安全：
 * - 片选信号 CS 由上层 `SC7A20.c` 控制 PB12，本模块专注总线字节收发。
 */

#include "MySPI2.h"
#include "n32wb452_gpio.h"
#include "n32wb452_rcc.h"
#include "n32wb452_spi.h"

/* 发送与接收缓冲区等待超时计数 */
#define MY_SPI2_TIMEOUT 100000U

/**
 * @brief  初始化 SPI2 控制器与 GPIO 引脚
 */
void MySPI2_Init(void)
{
    GPIO_InitType gpio_init_struct;
    SPI_InitType  spi_init_struct;

    /* 1. 使能 GPIOB、AFIO 与 SPI2 外设时钟 */
    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOB | RCC_APB2_PERIPH_AFIO, ENABLE);
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_SPI2, ENABLE);

    /* 2. 配置 PB13 (SCK) 与 PB15 (MOSI) 为复用推挽输出 (50MHz) */
    GPIO_InitStruct(&gpio_init_struct);
    gpio_init_struct.Pin        = GPIO_PIN_13 | GPIO_PIN_15;
    gpio_init_struct.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio_init_struct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(GPIOB, &gpio_init_struct);

    /* 3. 配置 PB14 (MISO) 为浮空输入 */
    gpio_init_struct.Pin       = GPIO_PIN_14;
    gpio_init_struct.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_InitPeripheral(GPIOB, &gpio_init_struct);

    /* 4. 配置 SPI2 控制器参数 */
    SPI_I2S_DeInit(SPI2);
    SPI_InitStruct(&spi_init_struct);
    spi_init_struct.DataDirection = SPI_DIR_DOUBLELINE_FULLDUPLEX; /* 全双工 */
    spi_init_struct.SpiMode       = SPI_MODE_MASTER;               /* 主机模式 */
    spi_init_struct.DataLen       = SPI_DATA_SIZE_8BITS;           /* 8位数据帧 */
    spi_init_struct.CLKPOL        = SPI_CLKPOL_LOW;                /* CPOL=0: 空闲时SCK为低电平 */
    spi_init_struct.CLKPHA        = SPI_CLKPHA_FIRST_EDGE;         /* CPHA=0: 奇数沿(第一个边沿)数据被采样 */
    spi_init_struct.NSS           = SPI_NSS_SOFT;                  /* 软件控制片选 */
    spi_init_struct.BaudRatePres  = SPI_BR_PRESCALER_16;           /* 32MHz / 16 = 2MHz 通信速率 */
    spi_init_struct.FirstBit      = SPI_FB_MSB;                    /* 高位先行 (MSB First) */
    spi_init_struct.CRCPoly       = 7U;
    SPI_Init(SPI2, &spi_init_struct);

    /* 5. 内部拉高 NSS 引脚并使能 SPI2 外设 */
    SPI_SetNssLevel(SPI2, SPI_NSS_HIGH);
    SPI_Enable(SPI2, ENABLE);
}

/**
 * @brief  全双工发送并接收 1 字节数据
 * @param  ByteSend 待发送数据
 * @return 接收到的数据（若超时则返回 0xFF）
 */
uint8_t MySPI2_TransferByte(uint8_t ByteSend)
{
    uint32_t timeout = MY_SPI2_TIMEOUT;

    /* 1. 等待发送缓冲区空 (TE) */
    while (SPI_I2S_GetStatus(SPI2, SPI_I2S_TE_FLAG) == RESET)
    {
        if (--timeout == 0U)
        {
            return 0xFFU;
        }
    }

    /* 2. 写入待发送数据到数据寄存器 */
    SPI_I2S_TransmitData(SPI2, ByteSend);

    /* 3. 等待接收缓冲区非空 (RNE) */
    timeout = MY_SPI2_TIMEOUT;
    while (SPI_I2S_GetStatus(SPI2, SPI_I2S_RNE_FLAG) == RESET)
    {
        if (--timeout == 0U)
        {
            return 0xFFU;
        }
    }

    /* 4. 读取接收数据并返回 */
    return (uint8_t)SPI_I2S_ReceiveData(SPI2);
}
