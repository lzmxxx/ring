/**
 * @file    MySPI2.h
 * @brief   SPI2 硬件总线底层驱动头文件 (SC7A20 加速度计专用)
 * @details 负责 N32WB452 SPI2 硬件外设配置及全双工单字节传输。
 */

#ifndef __MY_SPI2_H__
#define __MY_SPI2_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化 SPI2 硬件控制器及对应 GPIO 引脚
 * @note   引脚连接：
 *         - PB13: SPI2_SCK (复用推挽输出)
 *         - PB14: SPI2_MISO (浮空输入)
 *         - PB15: SPI2_MOSI (复用推挽输出)
 *         SPI 模式：主模式、8位全双工、CPOL=0、CPHA=0 (Mode 0)、软件片选 (NSS)。
 */
void MySPI2_Init(void);

/**
 * @brief  通过 SPI2 执行单字节全双工数据收发
 * @param  ByteSend 待通过 MOSI 发送的字节
 * @return 同时通过 MISO 接收到的从机返回字节
 * @note   带超时保护，若通信异常返回 0xFF。
 */
uint8_t MySPI2_TransferByte(uint8_t ByteSend);

#ifdef __cplusplus
}
#endif

#endif /* __MY_SPI2_H__ */
