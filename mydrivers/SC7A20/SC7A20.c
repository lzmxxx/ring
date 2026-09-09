/* SC7A20 motion driver: SPI2 + INT1 (PA9). */
#include "SC7A20.h"
#include "SC7A20_Reg.h"
#include "MySPI2.h"
#include "n32wb452_exti.h"
#include "n32wb452_gpio.h"
#include "n32wb452_rcc.h"
#include "misc.h"
#include <rtthread.h>

#define SC7A20_CS_LOW()     GPIO_ResetBits(GPIOB, GPIO_PIN_12)
#define SC7A20_CS_HIGH()    GPIO_SetBits(GPIOB, GPIO_PIN_12)

static volatile uint8_t motion_pending;
static uint8_t sensor_ready;
static uint8_t motion_threshold = SC7A20_MOTION_THRESHOLD;
static uint8_t motion_duration = SC7A20_MOTION_DURATION;

static void SC7A20_WriteRegister(uint8_t reg, uint8_t value)
{
    SC7A20_CS_LOW();
    (void)MySPI2_TransferByte(reg & 0x3FU);
    (void)MySPI2_TransferByte(value);
    SC7A20_CS_HIGH();
}

static uint8_t SC7A20_ReadRegister(uint8_t reg)
{
    uint8_t value;

    SC7A20_CS_LOW();
    (void)MySPI2_TransferByte(reg | SC7A20_SPI_READ);
    value = MySPI2_TransferByte(0xFFU);
    SC7A20_CS_HIGH();
    return value;
}

static void SC7A20_ConfigurePA9Interrupt(void)
{
    GPIO_InitType gpio;
    EXTI_InitType exti;
    NVIC_InitType nvic;

    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA | RCC_APB2_PERIPH_AFIO, ENABLE);

    GPIO_InitStruct(&gpio);
    gpio.Pin = GPIO_PIN_9;
    gpio.GPIO_Mode = GPIO_Mode_IPD;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(GPIOA, &gpio);

    GPIO_ConfigEXTILine(GPIOA_PORT_SOURCE, GPIO_PIN_SOURCE9);
    EXTI_ClrITPendBit(EXTI_LINE9);

    EXTI_InitStruct(&exti);
    exti.EXTI_Line = EXTI_LINE9;
    exti.EXTI_Mode = EXTI_Mode_Interrupt;
    exti.EXTI_Trigger = EXTI_Trigger_Rising;
    exti.EXTI_LineCmd = ENABLE;
    EXTI_InitPeripheral(&exti);

    nvic.NVIC_IRQChannel = EXTI9_5_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 2U;
    nvic.NVIC_IRQChannelSubPriority = 1U;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
}

void SC7A20_InterruptReInit(void)
{
    SC7A20_ConfigurePA9Interrupt();
}

uint8_t SC7A20_Init(void)
{
    GPIO_InitType gpio;

    MySPI2_Init();

    GPIO_InitStruct(&gpio);
    gpio.Pin = GPIO_PIN_12;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_SetBits(GPIOB, GPIO_PIN_12);
    GPIO_InitPeripheral(GPIOB, &gpio);

    sensor_ready = 0U;
    if (SC7A20_ReadRegister(SC7A20_REG_WHO_AM_I) != SC7A20_WHO_AM_I)
    {
        return 1U;
    }

    /* Keep INT1 disabled while the high-pass filter settles. */
    SC7A20_WriteRegister(SC7A20_REG_CTRL1, SC7A20_CTRL1_VALUE);
    SC7A20_WriteRegister(SC7A20_REG_CTRL2, SC7A20_CTRL2_VALUE);
    SC7A20_WriteRegister(SC7A20_REG_CTRL3, 0U);
    SC7A20_WriteRegister(SC7A20_REG_CTRL4, 0U);
    SC7A20_WriteRegister(SC7A20_REG_CTRL5, 0U);
    SC7A20_WriteRegister(SC7A20_REG_CTRL6, 0U);
    SC7A20_WriteRegister(SC7A20_REG_INT1_CFG, 0U);
    SC7A20_WriteRegister(SC7A20_REG_INT2_CFG, 0U);
    SC7A20_WriteRegister(SC7A20_REG_INT1_THS, motion_threshold);
    SC7A20_WriteRegister(SC7A20_REG_INT1_DURATION, motion_duration);

    rt_thread_mdelay(300U);
    (void)SC7A20_ReadRegister(SC7A20_REG_REFERENCE);
    (void)SC7A20_ReadRegister(SC7A20_REG_INT1_SOURCE);

    SC7A20_WriteRegister(SC7A20_REG_INT1_CFG, SC7A20_INT1_CFG_VALUE);
    SC7A20_WriteRegister(SC7A20_REG_CTRL3, SC7A20_CTRL3_VALUE);

    if ((SC7A20_ReadRegister(SC7A20_REG_CTRL1) != SC7A20_CTRL1_VALUE) ||
        (SC7A20_ReadRegister(SC7A20_REG_CTRL2) != SC7A20_CTRL2_VALUE) ||
        (SC7A20_ReadRegister(SC7A20_REG_CTRL3) != SC7A20_CTRL3_VALUE) ||
        (SC7A20_ReadRegister(SC7A20_REG_INT1_CFG) != SC7A20_INT1_CFG_VALUE) ||
        (SC7A20_ReadRegister(SC7A20_REG_INT1_THS) != motion_threshold) ||
        (SC7A20_ReadRegister(SC7A20_REG_INT1_DURATION) != motion_duration))
    {
        return 1U;
    }

    (void)SC7A20_ReadRegister(SC7A20_REG_INT1_SOURCE);
    motion_pending = 0U;
    sensor_ready = 1U;
    SC7A20_ConfigurePA9Interrupt();
    return 0U;
}

uint8_t SC7A20_ReadAcceleration(SC7A20_Acceleration *acceleration)
{
    uint8_t raw[6];
    int16_t x;
    int16_t y;
    int16_t z;
    uint8_t i;

    if ((acceleration == 0) || (sensor_ready == 0U))
    {
        return 1U;
    }

    SC7A20_CS_LOW();
    (void)MySPI2_TransferByte(SC7A20_REG_OUT_X_L | SC7A20_SPI_READ | SC7A20_SPI_AUTO_INCREMENT);
    for (i = 0U; i < 6U; ++i)
    {
        raw[i] = MySPI2_TransferByte(0xFFU);
    }
    SC7A20_CS_HIGH();

    x = (int16_t)(((uint16_t)raw[1] << 8) | raw[0]);
    y = (int16_t)(((uint16_t)raw[3] << 8) | raw[2]);
    z = (int16_t)(((uint16_t)raw[5] << 8) | raw[4]);
    acceleration->Xmg = (int16_t)((x >> 8) * 16);
    acceleration->Ymg = (int16_t)((y >> 8) * 16);
    acceleration->Zmg = (int16_t)((z >> 8) * 16);
    return 0U;
}

uint8_t SC7A20_GetAndClearMotion(void)
{
    uint32_t primask = __get_PRIMASK();
    uint8_t motion;

    __disable_irq();
    motion = motion_pending;
    motion_pending = 0U;
    __set_PRIMASK(primask);
    return motion;
}

void SC7A20_MotionIRQHandler(void)
{
    motion_pending = 1U;
}

void SC7A20_DeInit(void)
{
    EXTI_InitType exti_init_struct;

    EXTI_InitStruct(&exti_init_struct);
    exti_init_struct.EXTI_Line = EXTI_LINE9;
    exti_init_struct.EXTI_LineCmd = DISABLE;
    EXTI_InitPeripheral(&exti_init_struct);
    (void)SC7A20_WriteRegister(SC7A20_REG_CTRL1, 0x00U);
    motion_pending = 0U;
    sensor_ready = 0U;
}

uint8_t SC7A20_SetMotionParameter(uint8_t threshold, uint8_t duration)
{
    if (threshold == 0U || threshold > 0x7FU || duration > 0x7FU) return 1U;
    motion_threshold = threshold;
    motion_duration = duration;
    if (sensor_ready)
    {
        SC7A20_WriteRegister(SC7A20_REG_INT1_THS, threshold);
        SC7A20_WriteRegister(SC7A20_REG_INT1_DURATION, duration);
    }
    return 0U;
}
