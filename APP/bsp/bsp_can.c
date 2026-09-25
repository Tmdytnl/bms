#include "bsp_can.h"

#include <stddef.h>
#include <string.h>

#include "bsp_board_config.h"

#include "misc.h"
#include "stm32f10x.h"
#include "stm32f10x_can.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

#define BSP_CAN_PRESCALER                       (9U)
#define BSP_CAN_STD_ID_MAX                      (0x7FFU)
#define BSP_CAN_EXT_ID_MAX                      (0x1FFFFFFFUL)

BSP_BUILD_ASSERT(BSP_BOARD_CAN_RX_PIN == 11U,
                 can_rx_pin_is_pa11);
BSP_BUILD_ASSERT(BSP_BOARD_CAN_TX_PIN == 12U,
                 can_tx_pin_is_pa12);
BSP_BUILD_ASSERT(BSP_BOARD_PCLK1_HZ == 36000000UL,
                 can_pclk1_is_thirty_six_mhz);
BSP_BUILD_ASSERT((BSP_BOARD_PCLK1_HZ /
                  (BSP_CAN_PRESCALER * (1UL + 6UL + 1UL))) ==
                     BSP_BOARD_CAN_BITRATE,
                 can_timing_is_five_hundred_kbit);

/* bxCAN 配置全部完成后的可用标志。 */
static bool s_initialized;
/* RX FIFO0 中断已由 APL 显式开启的标志。 */
static bool s_rx_interrupt_enabled;
/* 当前硬件过滤器接纳的 11-bit 服务帧 ID。 */
static uint16_t s_service_rx_id;

/* 配置 CAN 所需的时钟与 PA11/PA12 引脚复用。 */
static void BSP_CAN_ConfigurePins(void)
{
    /* 当前访问的 GPIO 端口。 */
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_11;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_12;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &gpio);
}

/* 配置 bxCAN 接收过滤器，仅向上层交付允许的服务帧。 */
static void BSP_CAN_ConfigureServiceFilter(uint16_t service_rx_id)
{
    /* 当前 CAN 硬件过滤器配置。 */
    CAN_FilterInitTypeDef filter;
    /* 当前 CAN 帧的标准标识符。 */
    uint32_t identifier;
    /* 当前检查或写入的位掩码。 */
    uint32_t mask;

    /* bxCAN 32-bit filter：STID[10:0] 位于 31:21；同时 mask RTR/IDE，拒绝 remote/extended。 */
    identifier = (uint32_t)service_rx_id << 21U;
    mask = ((uint32_t)BSP_CAN_STD_ID_MAX << 21U) |
        (1UL << 2U) | (1UL << 1U);

    (void)memset(&filter, 0, sizeof(filter));
    filter.CAN_FilterNumber = 0U;
    filter.CAN_FilterMode = CAN_FilterMode_IdMask;
    filter.CAN_FilterScale = CAN_FilterScale_32bit;
    filter.CAN_FilterIdHigh = (uint16_t)(identifier >> 16U);
    filter.CAN_FilterIdLow = (uint16_t)identifier;
    filter.CAN_FilterMaskIdHigh = (uint16_t)(mask >> 16U);
    filter.CAN_FilterMaskIdLow = (uint16_t)mask;
    filter.CAN_FilterFIFOAssignment = CAN_Filter_FIFO0;
    filter.CAN_FilterActivation = ENABLE;
    CAN_FilterInit(&filter);
}

/* 配置 bxCAN 位时序、工作模式及 FIFO 中断条件。 */
static bool BSP_CAN_ConfigurePeripheral(void)
{
    /* STM32 CAN 外设初始化参数。 */
    CAN_InitTypeDef can;

    CAN_DeInit(CAN1);
    CAN_StructInit(&can);
    can.CAN_TTCM = DISABLE;
    can.CAN_ABOM = ENABLE;
    can.CAN_AWUM = DISABLE;
    can.CAN_NART = DISABLE;
    can.CAN_RFLM = DISABLE;
    can.CAN_TXFP = ENABLE;
    can.CAN_Mode = CAN_Mode_Normal;
    can.CAN_SJW = CAN_SJW_1tq;
    can.CAN_BS1 = CAN_BS1_6tq;
    can.CAN_BS2 = CAN_BS2_1tq;
    can.CAN_Prescaler = BSP_CAN_PRESCALER;
    if (CAN_Init(CAN1, &can) != CAN_InitStatus_Success)
    {
        return false;
    }
    BSP_CAN_ConfigureServiceFilter(s_service_rx_id);
    CAN_ClearFlag(CAN1, CAN_FLAG_FF0);
    CAN_ClearFlag(CAN1, CAN_FLAG_FOV0);
    return true;
}

/* 初始化 500 kbit/s 的 CAN 外设并记录可用状态。 */
bool BSP_CAN_Init500K(uint16_t service_rx_id)
{
    if (service_rx_id > BSP_CAN_STD_ID_MAX)
    {
        return false;
    }

    s_initialized = false;
    s_rx_interrupt_enabled = false;
    s_service_rx_id = service_rx_id;
    BSP_CAN_ConfigurePins();
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN1, ENABLE);
    if (!BSP_CAN_ConfigurePeripheral())
    {
        return false;
    }
    s_initialized = true;
    return true;
}

/* 仅在 CAN 已初始化后开启 FIFO0 接收中断。 */
bool BSP_CAN_EnableRxInterrupt(void)
{
    /* CAN 中断的 NVIC 优先级配置。 */
    NVIC_InitTypeDef nvic;

    if (!s_initialized)
    {
        return false;
    }
    CAN_ITConfig(CAN1, CAN_IT_FMP0 | CAN_IT_FOV0, DISABLE);
    CAN_ClearITPendingBit(CAN1, CAN_IT_FOV0);
    NVIC_ClearPendingIRQ(USB_LP_CAN1_RX0_IRQn);

    nvic.NVIC_IRQChannel = USB_LP_CAN1_RX0_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority =
        (uint8_t)(BSP_CAN_RX_LOGICAL_PRIORITY & 0x0FU);
    nvic.NVIC_IRQChannelSubPriority = 0U;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
    CAN_ITConfig(CAN1, CAN_IT_FMP0 | CAN_IT_FOV0, ENABLE);
    s_rx_interrupt_enabled = true;
    return true;
}

/* 读取 CAN 外设和过滤器配置完成标志。 */
bool BSP_CAN_IsInitialized(void)
{
    return s_initialized;
}

/* 尝试占用可用发送邮箱，不阻塞等待总线发送完成。 */
BSP_CanTxResult_t BSP_CAN_TryTransmit(const BSP_CanFrame_t *frame)
{
    /* 准备交给 CAN 发送邮箱的原始报文。 */
    CanTxMsg tx;
    /* CAN 发送硬件分配的邮箱编号。 */
    uint8_t mailbox;

    if (!s_initialized)
    {
        return BSP_CAN_TX_NOT_READY;
    }
    if ((frame == NULL) || (frame->dlc > 8U) ||
        (frame->extended ? (frame->id > BSP_CAN_EXT_ID_MAX) :
                           (frame->id > BSP_CAN_STD_ID_MAX)))
    {
        return BSP_CAN_TX_INVALID;
    }

    (void)memset(&tx, 0, sizeof(tx));
    tx.IDE = frame->extended ? CAN_Id_Extended : CAN_Id_Standard;
    tx.StdId = frame->extended ? 0UL : frame->id;
    tx.ExtId = frame->extended ? frame->id : 0UL;
    tx.RTR = CAN_RTR_Data;
    tx.DLC = frame->dlc;
    (void)memcpy(tx.Data, frame->data, frame->dlc);
    mailbox = CAN_Transmit(CAN1, &tx);
    return mailbox == CAN_TxStatus_NoMailBox ?
        BSP_CAN_TX_NO_MAILBOX : BSP_CAN_TX_ACCEPTED;
}

/* 读取 RX FIFO0 的待收报文数量。 */
bool BSP_CAN_ReceivePending(void)
{
    return s_initialized &&
        (CAN_MessagePending(CAN1, CAN_FIFO0) != 0U);
}

/* 从 RX FIFO0 取出一帧硬件报文并释放 FIFO 邮箱。 */
bool BSP_CAN_Receive(BSP_CanFrame_t *frame)
{
    /* 从 CAN 接收 FIFO 取出的原始报文。 */
    CanRxMsg rx;

    if ((frame == NULL) || !BSP_CAN_ReceivePending())
    {
        return false;
    }
    CAN_Receive(CAN1, CAN_FIFO0, &rx);
    if ((rx.RTR != CAN_RTR_Data) || (rx.DLC > 8U) ||
        ((rx.IDE != CAN_Id_Standard) && (rx.IDE != CAN_Id_Extended)))
    {
        return false;
    }
    frame->extended = rx.IDE == CAN_Id_Extended;
    frame->id = frame->extended ? rx.ExtId : rx.StdId;
    frame->dlc = rx.DLC;
    (void)memcpy(frame->data, rx.Data, rx.DLC);
    return true;
}

/* 检查 RX FIFO0 是否曾溢出，供 APL 记录诊断。 */
bool BSP_CAN_IsRxFifoOverrun(void)
{
    return s_initialized &&
        (CAN_GetFlagStatus(CAN1, CAN_FLAG_FOV0) != RESET);
}

/* 清除 RX FIFO0 的硬件溢出标志。 */
void BSP_CAN_ClearRxFifoOverrun(void)
{
    if (s_initialized)
    {
        CAN_ClearITPendingBit(CAN1, CAN_IT_FOV0);
    }
}

/* 读取 bxCAN bus-off 状态，供 APL 决定恢复时机。 */
bool BSP_CAN_IsBusOff(void)
{
    return s_initialized &&
        (CAN_GetFlagStatus(CAN1, CAN_FLAG_BOF) != RESET);
}

/* 按现有板级配置重新初始化 CAN 外设。 */
bool BSP_CAN_Recover(void)
{
    /* CAN 接收中断是否应重新使能。 */
    bool enable_rx;

    if (!s_initialized || !BSP_CAN_IsBusOff())
    {
        return s_initialized;
    }
    enable_rx = s_rx_interrupt_enabled;
    CAN_ITConfig(CAN1, CAN_IT_FMP0 | CAN_IT_FOV0, DISABLE);
    s_initialized = BSP_CAN_ConfigurePeripheral();
    if (s_initialized && enable_rx)
    {
        CAN_ITConfig(CAN1, CAN_IT_FMP0 | CAN_IT_FOV0, ENABLE);
    }
    return s_initialized;
}
