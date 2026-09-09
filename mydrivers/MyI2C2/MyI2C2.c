/**
 * @file    MyI2C2.c
 * @brief   I2C2 硬件总线底层驱动实现 (CW2015 库仑计专用)
 * 
 * 模块职责：
 * 1. 驱动 N32WB452 I2C2 硬件外设与 CW2015 电池电量计通信；
 * 2. 实现规范的硬件 I2C 起始、设备寻址、字节收发与停止时序；
 * 3. 严格遵循 N32 硬件 I2C 单字节、双字节及多字节读取规程（POS/ACK 配置与 STOP 产生时序）；
 * 4. 提供通信超时与总线死锁自动恢复机制。
 * 
 * 引脚映射：
 * - PB10: I2C2_SCL (开漏复用输出)
 * - PB11: I2C2_SDA (开漏复用输出)
 * 
 * 实时性与中断安全：
 * - 仅在关键状态切换（清除 ADDR、产生 STOP 前）使用 `__get_PRIMASK` 施加极短（< 1us）的局部保护；
 * - 绝不长时间关中断，确保 BLE 协议栈正常收发。
 */

#include "MyI2C2.h"
#include "n32wb452.h"
#include "n32wb452_gpio.h"
#include "n32wb452_i2c.h"
#include "n32wb452_rcc.h"

/* 循环超时门限 */
#define MY_I2C2_TIMEOUT 100000U

/* 硬件总线错误标志掩码 */
#define MY_I2C2_ERRORS  0x0F00U

/**
 * @brief  等待 I2C2 标志位达到期望状态
 * @param  flag 目标标志位
 * @param  set  1 等待置位，0 等待清零
 * @return 0 成功，1 超时或硬件故障
 */
static uint8_t MyI2C2_WaitFlag(uint32_t flag, uint8_t set)
{
    uint32_t timeout = MY_I2C2_TIMEOUT;

    while (((I2C_GetFlag(I2C2, flag) != RESET) ? 1U : 0U) != set)
    {
        if (((I2C2->STS1 & MY_I2C2_ERRORS) != 0U) || (--timeout == 0U))
        {
            return 1U;
        }
    }

    return 0U;
}

/**
 * @brief  清除 I2C2 ADDR (地址发送完成) 标志
 */
static void MyI2C2_ClearAddress(void)
{
    (void)I2C2->STS1;
    (void)I2C2->STS2;
}

/**
 * @brief  I2C2 通信异常时的总线终止与复位
 */
static void MyI2C2_Abort(void)
{
    I2C_GenerateStop(I2C2, ENABLE);
    MyI2C2_Init();
}

/**
 * @brief  发送起始信号并发送 7 位从机地址
 * @param  address_7bit 7位从机地址
 * @param  direction    传输方向
 * @return 0 收到应答，1 失败
 */
static uint8_t MyI2C2_Address(uint8_t address_7bit, uint8_t direction)
{
    I2C_GenerateStart(I2C2, ENABLE);

    if (MyI2C2_WaitFlag(I2C_FLAG_STARTBF, 1U) != 0U)
    {
        return 1U;
    }

    I2C_SendAddr7bit(I2C2, (uint8_t)(address_7bit << 1), direction);
    return MyI2C2_WaitFlag(I2C_FLAG_ADDRF, 1U);
}

/**
 * @brief  初始化 I2C2 硬件控制器及 GPIO
 */
void MyI2C2_Init(void)
{
    GPIO_InitType gpio_init_struct;
    I2C_InitType  i2c_init_struct;

    /* 1. 使能 GPIOB、AFIO 与 I2C2 时钟 */
    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOB | RCC_APB2_PERIPH_AFIO, ENABLE);
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_I2C2, ENABLE);

    /* 2. 配置 PB10 (SCL) 与 PB11 (SDA) 为开漏复用输出 */
    GPIO_InitStruct(&gpio_init_struct);
    gpio_init_struct.Pin        = GPIO_PIN_10 | GPIO_PIN_11;
    gpio_init_struct.GPIO_Mode  = GPIO_Mode_AF_OD;
    gpio_init_struct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_SetBits(GPIOB, gpio_init_struct.Pin);
    GPIO_InitPeripheral(GPIOB, &gpio_init_struct);

    /* 3. 配置 I2C2 控制器 */
    I2C_DeInit(I2C2);
    I2C_InitStruct(&i2c_init_struct);
    i2c_init_struct.ClkSpeed    = 100000U;
    i2c_init_struct.BusMode     = I2C_BUSMODE_I2C;
    i2c_init_struct.FmDutyCycle = I2C_FMDUTYCYCLE_2;
    i2c_init_struct.OwnAddr1    = 0U;
    i2c_init_struct.AckEnable   = I2C_ACKEN;
    i2c_init_struct.AddrMode    = I2C_ADDR_MODE_7BIT;
    I2C_Init(I2C2, &i2c_init_struct);

    /* 4. 使能 I2C2 */
    I2C_Enable(I2C2, ENABLE);
}

/**
 * @brief  探测指定 7 位从机地址是否有响应
 * @param  Address7Bit 7位从机地址
 * @return 0 存在，1 不存在或超时
 */
uint8_t MyI2C2_Probe(uint8_t Address7Bit)
{
    if ((Address7Bit > 0x7FU) || (MyI2C2_WaitFlag(I2C_FLAG_BUSY, 0U) != 0U) ||
        (MyI2C2_Address(Address7Bit, I2C_DIRECTION_SEND) != 0U))
    {
        goto Error;
    }

    MyI2C2_ClearAddress();
    I2C_GenerateStop(I2C2, ENABLE);

    if (MyI2C2_WaitFlag(I2C_FLAG_BUSY, 0U) != 0U)
    {
        goto Error;
    }

    return 0U;

Error:
    MyI2C2_Abort();
    return 1U;
}

/**
 * @brief  扫描 I2C2 活动从机
 * @param  AddressArray 存储发现地址的数组
 * @param  Capacity     数组容量
 * @return 发现设备数
 */
uint8_t MyI2C2_Scan(uint8_t *AddressArray, uint8_t Capacity)
{
    uint8_t current_address;
    uint8_t found_count = 0U;

    for (current_address = 0x08U; current_address <= 0x77U; current_address++)
    {
        if (MyI2C2_Probe(current_address) == 0U)
        {
            if (AddressArray && (found_count < Capacity))
            {
                AddressArray[found_count] = current_address;
            }
            found_count++;
        }
    }

    return found_count;
}

/**
 * @brief  写入从机寄存器
 * @param  Address7Bit 7位从机地址
 * @param  RegAddress  寄存器地址
 * @param  DataArray   写入数据数组
 * @param  Count       写入字节数
 * @return 0 成功，1 失败
 */
uint8_t MyI2C2_WriteRegister(uint8_t Address7Bit, uint8_t RegAddress,
                             const uint8_t *DataArray, uint16_t Count)
{
    if (!DataArray || (Count == 0U) || (MyI2C2_WaitFlag(I2C_FLAG_BUSY, 0U) != 0U) ||
        (MyI2C2_Address(Address7Bit, I2C_DIRECTION_SEND) != 0U))
    {
        goto Error;
    }

    MyI2C2_ClearAddress();
    I2C_SendData(I2C2, RegAddress);

    if (MyI2C2_WaitFlag(I2C_FLAG_BYTEF, 1U) != 0U)
    {
        goto Error;
    }

    while (Count--)
    {
        I2C_SendData(I2C2, *DataArray++);
        if (MyI2C2_WaitFlag(I2C_FLAG_BYTEF, 1U) != 0U)
        {
            goto Error;
        }
    }

    I2C_GenerateStop(I2C2, ENABLE);

    if (MyI2C2_WaitFlag(I2C_FLAG_BUSY, 0U) != 0U)
    {
        goto Error;
    }

    return 0U;

Error:
    MyI2C2_Abort();
    return 1U;
}

/**
 * @brief  读取从机寄存器
 * @param  Address7Bit 7位从机地址
 * @param  RegAddress  起始寄存器地址
 * @param  DataArray   读取缓冲区
 * @param  Count       读取字节数
 * @return 0 成功，1 失败
 */
uint8_t MyI2C2_ReadRegister(uint8_t Address7Bit, uint8_t RegAddress,
                            uint8_t *DataArray, uint16_t Count)
{
    uint16_t remaining = Count;
    uint32_t interrupt_state;

    if (!DataArray || (Count == 0U) || (MyI2C2_WaitFlag(I2C_FLAG_BUSY, 0U) != 0U) ||
        (MyI2C2_Address(Address7Bit, I2C_DIRECTION_SEND) != 0U))
    {
        goto Error;
    }

    MyI2C2_ClearAddress();
    I2C_SendData(I2C2, RegAddress);

    if ((MyI2C2_WaitFlag(I2C_FLAG_BYTEF, 1U) != 0U) ||
        (MyI2C2_Address(Address7Bit, I2C_DIRECTION_RECV) != 0U))
    {
        goto Error;
    }

    I2C_ConfigNackLocation(I2C2, I2C_NACK_POS_CURRENT);
    I2C_ConfigAck(I2C2, ENABLE);

    if (remaining == 1U)
    {
        /* 单字节读取流程 */
        interrupt_state = __get_PRIMASK();
        __disable_irq();
        I2C_ConfigAck(I2C2, DISABLE);
        MyI2C2_ClearAddress();
        I2C_GenerateStop(I2C2, ENABLE);
        __set_PRIMASK(interrupt_state);

        if (MyI2C2_WaitFlag(I2C_FLAG_RXDATNE, 1U) != 0U)
        {
            goto Error;
        }
        *DataArray = I2C_RecvData(I2C2);
    }
    else if (remaining == 2U)
    {
        /* 双字节读取流程 */
        interrupt_state = __get_PRIMASK();
        __disable_irq();
        I2C_ConfigNackLocation(I2C2, I2C_NACK_POS_NEXT);
        MyI2C2_ClearAddress();
        I2C_ConfigAck(I2C2, DISABLE);
        __set_PRIMASK(interrupt_state);

        if (MyI2C2_WaitFlag(I2C_FLAG_BYTEF, 1U) != 0U)
        {
            goto Error;
        }

        interrupt_state = __get_PRIMASK();
        __disable_irq();
        I2C_GenerateStop(I2C2, ENABLE);
        *DataArray++ = I2C_RecvData(I2C2);
        *DataArray   = I2C_RecvData(I2C2);
        __set_PRIMASK(interrupt_state);
    }
    else
    {
        /* 多字节读取流程 */
        MyI2C2_ClearAddress();

        while (remaining > 3U)
        {
            if (MyI2C2_WaitFlag(I2C_FLAG_RXDATNE, 1U) != 0U)
            {
                goto Error;
            }
            *DataArray++ = I2C_RecvData(I2C2);
            remaining--;
        }

        if (MyI2C2_WaitFlag(I2C_FLAG_BYTEF, 1U) != 0U)
        {
            goto Error;
        }
        I2C_ConfigAck(I2C2, DISABLE);
        *DataArray++ = I2C_RecvData(I2C2);

        if (MyI2C2_WaitFlag(I2C_FLAG_BYTEF, 1U) != 0U)
        {
            goto Error;
        }

        interrupt_state = __get_PRIMASK();
        __disable_irq();
        I2C_GenerateStop(I2C2, ENABLE);
        *DataArray++ = I2C_RecvData(I2C2);
        *DataArray   = I2C_RecvData(I2C2);
        __set_PRIMASK(interrupt_state);
    }

    I2C_ConfigNackLocation(I2C2, I2C_NACK_POS_CURRENT);
    I2C_ConfigAck(I2C2, ENABLE);

    if (MyI2C2_WaitFlag(I2C_FLAG_BUSY, 0U) != 0U)
    {
        goto Error;
    }

    return 0U;

Error:
    MyI2C2_Abort();
    return 1U;
}
