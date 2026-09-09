/**
 * @file    MyI2C2.h
 * @brief   I2C2 硬件总线底层驱动头文件 (CW2015 库仑计专用)
 * @details 负责 N32WB452 I2C2 (PB10 SCL, PB11 SDA) 硬件控制器配置及从机寄存器读写。
 */

#ifndef __MY_I2C2_H__
#define __MY_I2C2_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化 I2C2 外设及对应 GPIO (PB10/PB11)
 * @note   100kHz 标准速率，开漏复用输出。
 */
void MyI2C2_Init(void);

/**
 * @brief  探测指定 7 位从机地址是否有响应
 * @param  Address7Bit 7 位从机地址（如 CW2015 地址 0x62）
 * @return 0 成功探测到应答，1 超时或无应答
 */
uint8_t MyI2C2_Probe(uint8_t Address7Bit);

/**
 * @brief  扫描 I2C2 总线上的所有活动从机
 * @param  AddressArray 存储发现地址的数组指针
 * @param  Capacity     数组容量
 * @return 发现的设备总数
 */
uint8_t MyI2C2_Scan(uint8_t *AddressArray, uint8_t Capacity);

/**
 * @brief  通过 I2C2 写入目标从机连续寄存器
 * @param  Address7Bit 7 位从机地址
 * @param  RegAddress  目标起始寄存器地址
 * @param  DataArray   待写入数据数组
 * @param  Count       写入字节数
 * @return 0 成功，1 失败
 */
uint8_t MyI2C2_WriteRegister(uint8_t Address7Bit, uint8_t RegAddress,
                             const uint8_t *DataArray, uint16_t Count);

/**
 * @brief  通过 I2C2 读取目标从机连续寄存器
 * @param  Address7Bit 7 位从机地址
 * @param  RegAddress  目标起始寄存器地址
 * @param  DataArray   存放读取数据的数组
 * @param  Count       读取字节数
 * @return 0 成功，1 失败
 */
uint8_t MyI2C2_ReadRegister(uint8_t Address7Bit, uint8_t RegAddress,
                            uint8_t *DataArray, uint16_t Count);

#ifdef __cplusplus
}
#endif

#endif /* __MY_I2C2_H__ */
