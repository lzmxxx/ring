/**
 * @file    bsp_i2c.h
 * @brief   I2C1 软件模拟 GPIO 开漏驱动头文件
 * @details 针对 PB6 (SCL) 与 PB7 (SDA) 提供的备用软件模拟 I2C 驱动。
 *          主要用于硬件 I2C 异常时的总线诊断、9-Clock 强制复位从机序列与备用探测。
 */

#ifndef __BSP_I2C_H__
#define __BSP_I2C_H__

#include "n32wb452.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 软件 I2C 引脚定义 (与硬件 I2C1 共用物理引脚) */
#define I2C_PORT        GPIOB
#define I2C_SCL_PIN     GPIO_PIN_6
#define I2C_SDA_PIN     GPIO_PIN_7
#define I2C_CLK         RCC_APB2_PERIPH_GPIOB

/**
 * @brief  初始化软件模拟 I2C GPIO 端口，并发送 9-Clock 恢复时钟
 * @note   SCL/SDA 配置为开漏输出模式，防止高电平冲高到 3.3V 损坏 1.8V 域传感器。
 */
void bsp_i2c_init(void);

/**
 * @brief  产生 I2C 总线起始信号 (SCL 高电平时 SDA 由高变低)
 */
void bsp_i2c_start(void);

/**
 * @brief  产生 I2C 总线终止信号 (SCL 高电平时 SDA 由低变高)
 */
void bsp_i2c_stop(void);

/**
 * @brief  通过软件模拟时序向总线发送 1 字节
 * @param  data 待发送的 8 位数据
 * @return 从机响应的 ACK 状态：0 表示收到 ACK (低电平)，1 表示 NACK (高电平)
 */
uint8_t bsp_i2c_write_byte(uint8_t data);

/**
 * @brief  通过软件模拟时序从总线接收 1 字节
 * @param  ack 接收后向从机返回的应答位：1 发送 ACK (低电平)，0 发送 NACK (高电平)
 * @return 接收到的 8 位数据
 */
uint8_t bsp_i2c_read_byte(uint8_t ack);

/**
 * @brief  软件模拟写入指定从机寄存器单字节
 * @param  dev_addr 7 位从机设备地址
 * @param  reg_addr 目标寄存器地址
 * @param  data     待写入数据
 * @return 0 成功，非 0 失败错误码
 */
uint8_t bsp_i2c_write_reg(uint8_t dev_addr, uint8_t reg_addr, uint8_t data);

/**
 * @brief  软件模拟读取指定从机寄存器单字节
 * @param  dev_addr 7 位从机设备地址
 * @param  reg_addr 目标寄存器地址
 * @param  data     存放读取结果的指针
 * @return 0 成功，非 0 失败错误码
 */
uint8_t bsp_i2c_read_reg(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data);

/**
 * @brief  软件模拟连续读取指定从机多个字节
 * @param  dev_addr 7 位从机设备地址
 * @param  reg_addr 起始寄存器地址
 * @param  buf      目标缓冲区指针
 * @param  len      读取长度
 * @return 0 成功，非 0 失败错误码
 */
uint8_t bsp_i2c_read_burst(uint8_t dev_addr, uint8_t reg_addr, uint8_t *buf, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_I2C_H__ */
