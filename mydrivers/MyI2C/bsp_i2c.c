/**
 * @file    bsp_i2c.c
 * @brief   I2C1 软件模拟开漏 GPIO 驱动实现
 * 
 * 模块职责：
 * 1. 提供基于软件 GPIO 翻转的 I2C 底层时序（Start、Stop、WriteByte、ReadByte）；
 * 2. 在上电或从机锁死时执行 9 个时钟脉冲（9-Clock）序列，强制从机释放被拉低的 SDA 数据线；
 * 3. 作为硬件 I2C 的备用诊断通路，供 `IPA1322.c` 在硬件 I2C 探测失败时代际对比。
 * 
 * 硬件与电气规范：
 * - 引脚：PB6 (SCL) 与 PB7 (SDA)；
 * - 工作模式：严格配置为 Open-Drain (开漏输出)，禁止使用推挽输出！
 * - 供电安全：板载 R7/R8 (4.7k) 接在 1.8V 供电域，开漏输出可确保高电平绝对不超过 1.8V，
 *   彻底防止 3.3V GPIO 逻辑电平倒灌损坏 IPA1322 的 1.8V 耐压端口。
 */

#include "bsp_i2c.h"
#include "bsp_systick.h"
#include "n32wb452_gpio.h"
#include "n32wb452_rcc.h"

/* 寄存器级快速引脚电平控制宏 */
#define I2C_SCL_HIGH()  (I2C_PORT->PBSC = I2C_SCL_PIN)
#define I2C_SCL_LOW()   (I2C_PORT->PBC  = I2C_SCL_PIN)
#define I2C_SDA_HIGH()  (I2C_PORT->PBSC = I2C_SDA_PIN)
#define I2C_SDA_LOW()   (I2C_PORT->PBC  = I2C_SDA_PIN)
#define I2C_SDA_READ()  ((I2C_PORT->PID & I2C_SDA_PIN) != 0U)
#define I2C_SCL_READ()  ((I2C_PORT->PID & I2C_SCL_PIN) != 0U)

/**
 * @brief  软件 I2C 微秒级半周期延时
 * @note   在 64MHz/72MHz 主频下循环约 45 次产生约 2.5us 延时，折合 SCL 频率约为 200kHz。
 */
static void i2c_delay(void)
{
    for (volatile int loop = 0; loop < 45; loop++)
    {
        __NOP();
    }
}

/**
 * @brief  初始化软件 I2C 引脚并执行 9-Clock 恢复序列
 */
void bsp_i2c_init(void)
{
    GPIO_InitType gpio_init_struct;

    RCC_EnableAPB2PeriphClk(I2C_CLK | RCC_APB2_PERIPH_AFIO, ENABLE);

    /* 配置 PB6 (SCL) 与 PB7 (SDA) 为开漏输出模式 (50MHz) */
    GPIO_InitStruct(&gpio_init_struct);
    gpio_init_struct.Pin        = I2C_SCL_PIN | I2C_SDA_PIN;
    gpio_init_struct.GPIO_Mode  = GPIO_Mode_Out_OD;
    gpio_init_struct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(I2C_PORT, &gpio_init_struct);

    /* 默认释放总线为高阻态 */
    I2C_SDA_HIGH();
    I2C_SCL_HIGH();
    i2c_delay();

    /* 
     * 9-Clock 总线恢复序列：
     * 若从机在上次通信被复位打断导致内部状态机仍处于向主机发 0 (拉低 SDA) 状态，
     * 主机连续发送 9 个时钟脉冲，从机在时钟驱动下移位完毕后会释放 SDA，从而解开总线锁死。
     */
    for (int cycle = 0; cycle < 9; cycle++)
    {
        I2C_SCL_LOW();
        i2c_delay();
        I2C_SCL_HIGH();
        i2c_delay();
    }

    /* 发送 STOP 条件确立空闲态 */
    bsp_i2c_stop();
}

/**
 * @brief  产生起始信号
 */
void bsp_i2c_start(void)
{
    I2C_SDA_HIGH();
    i2c_delay();
    I2C_SCL_HIGH();
    i2c_delay();
    I2C_SDA_LOW();
    i2c_delay();
    I2C_SCL_LOW();
    i2c_delay();
}

/**
 * @brief  产生终止信号
 */
void bsp_i2c_stop(void)
{
    I2C_SCL_LOW();
    i2c_delay();
    I2C_SDA_LOW();
    i2c_delay();
    I2C_SCL_HIGH();
    i2c_delay();
    I2C_SDA_HIGH();
    i2c_delay();
}

/**
 * @brief  写入 1 个字节并读取从机 ACK 响应
 * @param  data 待发送字节
 * @return 0 收到 ACK (SDA为低)，1 收到 NACK (SDA为高)
 */
uint8_t bsp_i2c_write_byte(uint8_t data)
{
    for (int bit = 0; bit < 8; bit++)
    {
        I2C_SCL_LOW();
        i2c_delay();

        if ((data & 0x80U) != 0U)
        {
            I2C_SDA_HIGH();
        }
        else
        {
            I2C_SDA_LOW();
        }

        data <<= 1;
        i2c_delay();
        I2C_SCL_HIGH();
        i2c_delay();
    }

    /* 释放 SDA，第 9 个时钟周期采样从机应答 */
    I2C_SCL_LOW();
    i2c_delay();
    I2C_SDA_HIGH();
    i2c_delay();
    I2C_SCL_HIGH();
    i2c_delay();

    uint8_t ack = I2C_SDA_READ() ? 1U : 0U;

    I2C_SCL_LOW();
    i2c_delay();

    return ack;
}

/**
 * @brief  读取 1 个字节并向从机返回应答
 * @param  ack 1 发送 ACK (继续读)，0 发送 NACK (停止读)
 * @return 读到的数据
 */
uint8_t bsp_i2c_read_byte(uint8_t ack)
{
    uint8_t data = 0U;

    I2C_SDA_HIGH();

    for (int bit = 0; bit < 8; bit++)
    {
        data <<= 1;
        I2C_SCL_LOW();
        i2c_delay();
        I2C_SCL_HIGH();
        i2c_delay();

        if (I2C_SDA_READ())
        {
            data |= 0x01U;
        }
    }

    /* 发送主机应答位 */
    I2C_SCL_LOW();
    i2c_delay();

    if (ack)
    {
        I2C_SDA_LOW();
    }
    else
    {
        I2C_SDA_HIGH();
    }

    i2c_delay();
    I2C_SCL_HIGH();
    i2c_delay();
    I2C_SCL_LOW();
    i2c_delay();
    I2C_SDA_HIGH();

    return data;
}

/**
 * @brief  向目标从机写入单字节寄存器
 * @param  dev_addr 7位从机地址
 * @param  reg_addr 寄存器地址
 * @param  data     写入数据
 * @return 0 成功，1~3 阶段错误
 */
uint8_t bsp_i2c_write_reg(uint8_t dev_addr, uint8_t reg_addr, uint8_t data)
{
    bsp_i2c_start();

    if (bsp_i2c_write_byte((uint8_t)(dev_addr << 1)) != 0U)
    {
        bsp_i2c_stop();
        return 1U;
    }

    if (bsp_i2c_write_byte(reg_addr) != 0U)
    {
        bsp_i2c_stop();
        return 2U;
    }

    if (bsp_i2c_write_byte(data) != 0U)
    {
        bsp_i2c_stop();
        return 3U;
    }

    bsp_i2c_stop();
    return 0U;
}

/**
 * @brief  读取目标从机单字节寄存器
 * @param  dev_addr 7位从机地址
 * @param  reg_addr 寄存器地址
 * @param  data     存放输出数据的指针
 * @return 0 成功，非 0 失败
 */
uint8_t bsp_i2c_read_reg(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data)
{
    if (!data)
    {
        return 4U;
    }

    bsp_i2c_start();

    if (bsp_i2c_write_byte((uint8_t)(dev_addr << 1)) != 0U)
    {
        bsp_i2c_stop();
        return 1U;
    }

    if (bsp_i2c_write_byte(reg_addr) != 0U)
    {
        bsp_i2c_stop();
        return 2U;
    }

    bsp_i2c_start();

    if (bsp_i2c_write_byte((uint8_t)((dev_addr << 1) | 0x01U)) != 0U)
    {
        bsp_i2c_stop();
        return 3U;
    }

    *data = bsp_i2c_read_byte(0U); /* 单字节读取后发送 NACK */
    bsp_i2c_stop();

    return 0U;
}

/**
 * @brief  连续读取目标从机多个字节寄存器
 * @param  dev_addr 7位从机地址
 * @param  reg_addr 寄存器地址
 * @param  buf      目标缓冲区
 * @param  len      读取长度
 * @return 0 成功，非 0 失败
 */
uint8_t bsp_i2c_read_burst(uint8_t dev_addr, uint8_t reg_addr, uint8_t *buf, uint16_t len)
{
    if (!buf || (len == 0U))
    {
        return 4U;
    }

    bsp_i2c_start();

    if (bsp_i2c_write_byte((uint8_t)(dev_addr << 1)) != 0U)
    {
        bsp_i2c_stop();
        return 1U;
    }

    if (bsp_i2c_write_byte(reg_addr) != 0U)
    {
        bsp_i2c_stop();
        return 2U;
    }

    bsp_i2c_start();

    if (bsp_i2c_write_byte((uint8_t)((dev_addr << 1) | 0x01U)) != 0U)
    {
        bsp_i2c_stop();
        return 3U;
    }

    for (uint16_t index = 0U; index < len; index++)
    {
        buf[index] = bsp_i2c_read_byte((index == (len - 1U)) ? 0U : 1U);
    }

    bsp_i2c_stop();
    return 0U;
}
