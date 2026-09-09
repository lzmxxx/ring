/**
 * @file    bsp_hw_i2c.c
 * @brief   I2C1 硬件总线及 DMA1 接收驱动实现
 * 
 * 模块职责：
 * 1. 驱动硬件 I2C1（PB6 SCL, PB7 SDA）与 IPA1322 AFE 芯片的高速通信；
 * 2. 严格按照 STM32/N32 硬件 I2C 控制规范实现单字节、双字节及多字节寄存器收发；
 * 3. 实现基于 DMA1 Channel 7 的 I2C 硬件接收通道，实现 64 entry FIFO 的非阻塞 DMA 搬运；
 * 4. 提供通信超时检测与总线死锁自动恢复机制（生成 STOP 并重新初始化外设）。
 * 
 * 与其他模块交互：
 * - 向上提供 `VT2102_WriteRegisters` 与 `VT2102_ReadRegisters` 供 `vt2102_hal.c` 及 `IPA1322.c` 调用；
 * - 供 `app_ppg.c` 的采集线程调用 `bsp_hw_i2c_dma_read_start()` 进行零 CPU 负荷批量读取；
 * - 在 `DMA1_Channel7_IRQHandler` 中回调 `bsp_hw_i2c_dma_finish_isr()`。
 * 
 * 中断与实时性考量：
 * - 在 1 字节和 2 字节读取时，硬件要求在清除 ADDR 标志前必须先配置 ACK/NACK 与 STOP 位；
 *   为了避免在此关键时刻被高优先级 BLE 中断打断导致从机多发字节，仅在两三行寄存器配置处加极短的
 *   `__disable_irq()` 保护，最大关中断时间 < 1us，绝不影响 BLE 射频时序。
 * 
 * 低功耗考量：
 * - DMA 接收期间由上层持有 `PM_DMA` 锁，完成后释放；
 * - 休眠唤醒后系统主频恢复，`app_power.c` 会重新调用 `bsp_hw_i2c_init()` 恢复硬件外设状态。
 */

#include "bsp_hw_i2c.h"
#include "n32wb452.h"
#include "n32wb452_i2c.h"
#include "n32wb452_gpio.h"
#include "n32wb452_rcc.h"
#include "n32wb452_dma.h"
#include "misc.h"
#include "vt2102.h"
#include <string.h>

/* IPA1322 7位设备地址 0x48，左移1位作为8位写地址 0x90 */
#define IPA1322_I2C_ADDR_7BIT       0x48U
#define IPA1322_I2C_ADDR_WRITE      (IPA1322_I2C_ADDR_7BIT << 1)

/* 总线超时判定计数值（基于循环轮询步数） */
#define I2C_TIMEOUT_CYCLES          100000U

/* I2C1 错误状态掩码：涵盖总线错误(BERR)、仲裁丢失(ARLO)、应答失败(AF)、溢出(OVR)等 */
#define I2C_STS1_ERROR_MASK         0x0F00U

/* 全局调试状态统计 */
volatile uint8_t  g_hw_i2c_last_err;
volatile uint32_t g_hw_i2c_error_count;
volatile uint16_t g_hw_i2c_error_status;
static volatile bool dma_rx_busy = false;

/**
 * @brief  初始化 I2C1 外设与相关引脚
 * @note   PB6/PB7 严格配置为开漏复用 (AF_OD)，由外部 1.8V 域上拉电阻提供高电平。
 */
void bsp_hw_i2c_init(void)
{
    GPIO_InitType gpio_init_struct;
    I2C_InitType  i2c_init_struct;

    /* 1. 使能 I2C1、GPIOB 与 AFIO 外设时钟 */
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_I2C1, ENABLE);
    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOB | RCC_APB2_PERIPH_AFIO, ENABLE);

    /* 2. 配置 PB6 (SCL) 与 PB7 (SDA) 为开漏复用输出模式 (50MHz) */
    GPIO_InitStruct(&gpio_init_struct);
    gpio_init_struct.Pin        = GPIO_PIN_6 | GPIO_PIN_7;
    gpio_init_struct.GPIO_Mode  = GPIO_Mode_AF_OD;
    gpio_init_struct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_SetBits(GPIOB, gpio_init_struct.Pin);
    GPIO_InitPeripheral(GPIOB, &gpio_init_struct);

    /* 3. 复位并重新配置 I2C1 控制器 */
    I2C_DeInit(I2C1);
    I2C_InitStruct(&i2c_init_struct);
    i2c_init_struct.ClkSpeed    = 100000U;                /* 100kHz 标准通信速率 */
    i2c_init_struct.BusMode     = I2C_BUSMODE_I2C;
    i2c_init_struct.FmDutyCycle = I2C_FMDUTYCYCLE_2;
    i2c_init_struct.OwnAddr1    = 0U;
    i2c_init_struct.AckEnable   = I2C_ACKEN;
    i2c_init_struct.AddrMode    = I2C_ADDR_MODE_7BIT;
    I2C_Init(I2C1, &i2c_init_struct);

    /* 4. 使能 I2C1 控制器 */
    I2C_Enable(I2C1, ENABLE);

    /* 5. 提前开启 DMA1 时钟备用 */
    RCC_EnableAHBPeriphClk(RCC_AHB_PERIPH_DMA1, ENABLE);
}

/**
 * @brief  等待指定 I2C 硬件状态标志位置位或清零
 * @param  flag  I2C 标志位 (如 I2C_FLAG_STARTBF, I2C_FLAG_ADDRF, I2C_FLAG_BYTEF 等)
 * @param  set   期望的状态：true 为等待置位，false 为等待清零
 * @param  stage 当前等待所处的逻辑阶段（用于记录故障码）
 * @return true 表示在超时前达到期望状态，false 表示超时或硬件出错
 */
static bool wait_flag(uint32_t flag, bool set, uint8_t stage)
{
    uint32_t remaining = I2C_TIMEOUT_CYCLES;

    while ((I2C_GetFlag(I2C1, flag) != RESET) != set)
    {
        /* 检查是否存在总线错误（如从机 NACK、总线仲裁丢失）或达到超时门限 */
        if (((I2C1->STS1 & I2C_STS1_ERROR_MASK) != 0U) || (--remaining == 0U))
        {
            g_hw_i2c_last_err     = stage;
            g_hw_i2c_error_status = I2C1->STS1;
            return false;
        }
    }

    return true;
}

/**
 * @brief  清除 I2C ADDR (地址发送完成) 标志
 * @note   根据 N32/STM32 硬件规范，顺序读取 STS1 与 STS2 寄存器即可硬件自动清零 ADDR 标志。
 */
static void clear_address(void)
{
    (void)I2C1->STS1;
    (void)I2C1->STS2;
}

/**
 * @brief  通信故障时的总线安全中止与复位流程
 */
static void abort_transfer(void)
{
    g_hw_i2c_error_count++;
    I2C_GenerateStop(I2C1, ENABLE);
    bsp_hw_i2c_init();
}

/**
 * @brief  发送 I2C 起始位并发送从机设备地址
 * @param  address   7位从机设备地址
 * @param  direction 传输方向：I2C_DIRECTION_SEND 或 I2C_DIRECTION_RECV
 * @return true 成功收到从机应答，false 超时或收到 NACK
 */
static bool address_phase(uint8_t address, uint8_t direction)
{
    /* 生成 START 信号并等待主机模式就绪 */
    I2C_GenerateStart(I2C1, ENABLE);
    if (!wait_flag(I2C_FLAG_STARTBF, true, 2U))
    {
        return false;
    }

    /* 发送从机地址与读写位 */
    I2C_SendAddr7bit(I2C1, address, direction);

    /* 等待从机响应 ACK 并置位 ADDR 标志 */
    return wait_flag(I2C_FLAG_ADDRF, true, 3U);
}

/**
 * @brief  发送起始条件并写入目标从机寄存器地址
 * @param  reg 目标寄存器地址
 * @return true 寄存器地址成功发送并应答，false 失败
 */
static bool begin_register(uint8_t reg)
{
    g_hw_i2c_last_err = 0U;

    I2C_ConfigNackLocation(I2C1, I2C_NACK_POS_CURRENT);
    I2C_ConfigAck(I2C1, ENABLE);

    /* 等待总线空闲 */
    if (!wait_flag(I2C_FLAG_BUSY, false, 1U))
    {
        return false;
    }

    /* 发送 IPA1322 写地址 */
    if (!address_phase(IPA1322_I2C_ADDR_WRITE, I2C_DIRECTION_SEND))
    {
        return false;
    }

    /* 清除地址匹配标志 */
    clear_address();

    /* 发送待访问的寄存器偏移地址 */
    I2C_SendData(I2C1, reg);

    /* 等待单字节发送完成 (BYTEF) */
    return wait_flag(I2C_FLAG_BYTEF, true, 4U);
}

/**
 * @brief  探测总线上是否存在指定 7 位从机地址
 * @param  address 7位从机地址
 * @return 1 存在且应答，0 不存在或总线超时
 */
uint8_t bsp_hw_i2c_probe_addr(uint8_t address)
{
    g_hw_i2c_last_err = 0U;

    if ((address > 0x7FU) || !wait_flag(I2C_FLAG_BUSY, false, 1U) ||
        !address_phase((uint8_t)(address << 1), I2C_DIRECTION_SEND))
    {
        abort_transfer();
        return 0U;
    }

    clear_address();
    I2C_GenerateStop(I2C1, ENABLE);

    if (!wait_flag(I2C_FLAG_BUSY, false, 8U))
    {
        abort_transfer();
        return 0U;
    }

    return 1U;
}

/**
 * @brief  扫描 I2C1 总线上的活动设备
 * @param  addresses 存储探测到的地址数组
 * @param  capacity  数组容量上限
 * @return 发现的设备总数
 */
uint8_t bsp_hw_i2c_scan(uint8_t *addresses, uint8_t capacity)
{
    uint8_t found_count = 0U;
    uint8_t current_address;

    for (current_address = 8U; current_address < 0x78U; current_address++)
    {
        if (bsp_hw_i2c_probe_addr(current_address) != 0U)
        {
            if (addresses && (found_count < capacity))
            {
                addresses[found_count] = current_address;
            }
            found_count++;
        }
    }

    return found_count;
}

/**
 * @brief  向从机连续写入多个寄存器字节（阻塞式，内部供 IPA1322 驱动层调用）
 * @param  reg  起始寄存器地址
 * @param  data 待写入数据指针
 * @param  size 写入字节数
 */
void VT2102_WriteRegisters(uint8_t reg, const uint8_t *data, uint32_t size)
{
    if (!data || (size == 0U))
    {
        return;
    }

    if (!begin_register(reg))
    {
        goto fail;
    }

    while (size--)
    {
        I2C_SendData(I2C1, *data++);
        if (!wait_flag(I2C_FLAG_BYTEF, true, 5U))
        {
            goto fail;
        }
    }

    I2C_GenerateStop(I2C1, ENABLE);
    if (!wait_flag(I2C_FLAG_BUSY, false, 8U))
    {
        goto fail;
    }

    return;

fail:
    abort_transfer();
}

/**
 * @brief  从从机连续读取多个寄存器字节（严格遵循硬件 I2C 硅缺陷规避时序）
 * @param  reg  起始寄存器地址
 * @param  data 存放读取数据的缓冲区指针
 * @param  size 读取字节数
 */
void VT2102_ReadRegisters(uint8_t reg, uint8_t *data, uint32_t size)
{
    uint8_t *out_ptr = data;
    uint32_t remaining = size;
    uint32_t primask_state;

    if (!data || (size == 0U))
    {
        return;
    }

    memset(data, 0, size);

    /* 1. 先写起始寄存器地址 */
    if (!begin_register(reg))
    {
        goto fail;
    }

    /* 2. 发送重复起始条件 (Repeated Start) 与读地址 */
    if (!address_phase(IPA1322_I2C_ADDR_WRITE, I2C_DIRECTION_RECV))
    {
        goto fail;
    }

    /* 
     * 3. 按照 STM32/N32 硬件规范分流处理单字节、双字节与多字节读取：
     * 保持 ADDR 置位直到配置好 ACK/NACK 和 STOP，并在最关键的 2~3 行寄存器操作处
     * 仅短暂关闭全局中断，防止在此期间被中断打断导致时钟线 SCL 继续翻转。
     */
    if (remaining == 1U)
    {
        /* 单字节读取：清 ADDR 前关闭 ACK 并发出 STOP 信号 */
        primask_state = __get_PRIMASK();
        __disable_irq();
        I2C_ConfigAck(I2C1, DISABLE);
        clear_address();
        I2C_GenerateStop(I2C1, ENABLE);
        __set_PRIMASK(primask_state);

        /* 等待接收数据就绪并读取 */
        if (!wait_flag(I2C_FLAG_RXDATNE, true, 6U))
        {
            goto fail;
        }
        *out_ptr = I2C_RecvData(I2C1);
    }
    else if (remaining == 2U)
    {
        /* 双字节读取：配置 POS 为下一个字节 NACK，在清 ADDR 前关闭 ACK */
        primask_state = __get_PRIMASK();
        __disable_irq();
        I2C_ConfigNackLocation(I2C1, I2C_NACK_POS_NEXT);
        clear_address();
        I2C_ConfigAck(I2C1, DISABLE);
        __set_PRIMASK(primask_state);

        /* 等待两个字节接收完毕 (BYTEF 置位) */
        if (!wait_flag(I2C_FLAG_BYTEF, true, 7U))
        {
            goto fail;
        }

        /* 产生 STOP 信号并依次读取两个数据字节 */
        primask_state = __get_PRIMASK();
        __disable_irq();
        I2C_GenerateStop(I2C1, ENABLE);
        *out_ptr++ = I2C_RecvData(I2C1);
        *out_ptr   = I2C_RecvData(I2C1);
        __set_PRIMASK(primask_state);
    }
    else
    {
        /* 3 字节及以上的多字节读取 */
        clear_address();

        while (remaining > 3U)
        {
            if (!wait_flag(I2C_FLAG_RXDATNE, true, 6U))
            {
                goto fail;
            }
            *out_ptr++ = I2C_RecvData(I2C1);
            remaining--;
        }

        /* 剩余 3 字节时：等待倒数第 3 字节完全到达 (BYTEF) */
        if (!wait_flag(I2C_FLAG_BYTEF, true, 7U))
        {
            goto fail;
        }
        I2C_ConfigAck(I2C1, DISABLE);
        *out_ptr++ = I2C_RecvData(I2C1);

        /* 等待最后 2 字节移位完成并拉低 SCL (BYTEF) */
        if (!wait_flag(I2C_FLAG_BYTEF, true, 7U))
        {
            goto fail;
        }

        primask_state = __get_PRIMASK();
        __disable_irq();
        I2C_GenerateStop(I2C1, ENABLE);
        *out_ptr++ = I2C_RecvData(I2C1);
        *out_ptr   = I2C_RecvData(I2C1);
        __set_PRIMASK(primask_state);
    }

    if (!wait_flag(I2C_FLAG_BUSY, false, 8U))
    {
        goto fail;
    }

    /* 恢复 ACK 与 POS 配置为默认状态备用 */
    I2C_ConfigNackLocation(I2C1, I2C_NACK_POS_CURRENT);
    I2C_ConfigAck(I2C1, ENABLE);
    return;

fail:
    memset(data, 0, size);
    abort_transfer();
}

/**
 * @brief  配置并启动 I2C1 DMA1 Channel 7 接收流程
 * @param  reg  目标寄存器地址
 * @param  data 接收缓冲区首地址
 * @param  size 接收字节数（>= 2）
 * @return 1 成功启动，0 失败
 */
uint8_t bsp_hw_i2c_dma_read_start(uint8_t reg, uint8_t *data, uint16_t size)
{
    DMA_InitType  dma_init_struct;
    NVIC_InitType nvic_init_struct;

    if (!data || (size < 2U) || dma_rx_busy)
    {
        return 0U;
    }

    /* 1. 先写目标寄存器地址 */
    if (!begin_register(reg))
    {
        goto fail;
    }

    /* 2. 发送重复起始条件与读命令 */
    if (!address_phase(IPA1322_I2C_ADDR_WRITE, I2C_DIRECTION_RECV))
    {
        goto fail;
    }

    /* 3. 配置 DMA1 Channel 7 (I2C1_RX 专用通道) */
    DMA_DeInit(DMA1_CH7);
    DMA_StructInit(&dma_init_struct);
    dma_init_struct.PeriphAddr     = (uint32_t)&I2C1->DAT;
    dma_init_struct.MemAddr        = (uint32_t)data;
    dma_init_struct.Direction      = DMA_DIR_PERIPH_SRC;
    dma_init_struct.BufSize        = size;
    dma_init_struct.PeriphInc      = DMA_PERIPH_INC_DISABLE;
    dma_init_struct.DMA_MemoryInc  = DMA_MEM_INC_ENABLE;
    dma_init_struct.PeriphDataSize = DMA_PERIPH_DATA_SIZE_BYTE;
    dma_init_struct.MemDataSize    = DMA_MemoryDataSize_Byte;
    dma_init_struct.CircularMode   = DMA_MODE_NORMAL;
    dma_init_struct.Priority       = DMA_PRIORITY_VERY_HIGH;
    dma_init_struct.Mem2Mem        = DMA_M2M_DISABLE;
    DMA_Init(DMA1_CH7, &dma_init_struct);

    /* 清除中断挂起位并使能传输完成和错误中断 */
    DMA_ClrIntPendingBit(DMA1_INT_GLB7, DMA1);
    DMA_ConfigInt(DMA1_CH7, DMA_INT_TXC | DMA_INT_ERR, ENABLE);

    /* 4. 配置 NVIC 允许 DMA1 Channel 7 中断 */
    nvic_init_struct.NVIC_IRQChannel                   = DMA1_Channel7_IRQn;
    nvic_init_struct.NVIC_IRQChannelPreemptionPriority = 1U;
    nvic_init_struct.NVIC_IRQChannelSubPriority        = 2U;
    nvic_init_struct.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic_init_struct);

    /* 5. 开启 I2C ACK、清零 ADDR、开启 DMA Last 自动停止支持 */
    I2C_ConfigAck(I2C1, ENABLE);
    clear_address();
    I2C_EnableDmaLastSend(I2C1, ENABLE);

    /* 6. 标记 DMA 忙并正式启动 DMA 与 I2C 联动搬运 */
    dma_rx_busy = true;
    DMA_EnableChannel(DMA1_CH7, ENABLE);
    I2C_EnableDMA(I2C1, ENABLE);

    return 1U;

fail:
    abort_transfer();
    return 0U;
}

/**
 * @brief  DMA1 Channel 7 正常完成后的中断清理
 */
void bsp_hw_i2c_dma_finish_isr(void)
{
    I2C_EnableDMA(I2C1, DISABLE);
    DMA_EnableChannel(DMA1_CH7, DISABLE);
    I2C_EnableDmaLastSend(I2C1, DISABLE);
    I2C_GenerateStop(I2C1, ENABLE);
    DMA_ClrIntPendingBit(DMA1_INT_GLB7, DMA1);
    dma_rx_busy = false;
}

/**
 * @brief  DMA 异常中断或超时时的强制终止
 */
void bsp_hw_i2c_dma_abort(void)
{
    I2C_EnableDMA(I2C1, DISABLE);
    DMA_EnableChannel(DMA1_CH7, DISABLE);
    I2C_EnableDmaLastSend(I2C1, DISABLE);
    DMA_ClrIntPendingBit(DMA1_INT_GLB7, DMA1);
    dma_rx_busy = false;
    abort_transfer();
}

/**
 * @brief  查询 DMA 通道是否处于传输中
 */
bool bsp_hw_i2c_dma_busy(void)
{
    return dma_rx_busy;
}
