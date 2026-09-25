#include "bsp_uart.h"

/*
 * USART1 固定 115200 8N1。TryWriteByte/TryReadByte 只观察一次状态位，适合
 * best-effort runtime service；Write 会等待每个 TXE，调用者必须选择允许阻塞
 * 的上下文。驱动不解析命令，也不拥有任何安全状态。
 */

#include <stddef.h>

#include "bsp_board_config.h"

#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_usart.h"

#define BSP_UART1_BAUDRATE                      (115200UL)
#define BSP_UART1_EXPECTED_BRR                  (0x0271U)
#define BSP_UART1_TX_SPIN_LIMIT                 (100000UL)

BSP_BUILD_ASSERT(BSP_BOARD_UART_TX_PIN == 9U,
                 uart_tx_pin_is_pa9);
BSP_BUILD_ASSERT(BSP_BOARD_UART_RX_PIN == 10U,
                 uart_rx_pin_is_pa10);
BSP_BUILD_ASSERT(BSP_BOARD_PCLK2_HZ == 72000000UL,
                 uart_pclk2_is_seventy_two_mhz);

/* USART1 初始化并完成寄存器配置回读的标志。 */
static bool s_initialized;

/* 配置 USART1 引脚和 115200 波特率，供有界调试输出使用。 */
bool BSP_UART1_Init115200(void)
{
    /* 当前访问的 GPIO 端口。 */
    GPIO_InitTypeDef gpio;
    /* STM32 UART 外设初始化参数。 */
    USART_InitTypeDef uart;

    /* 配置期间先撤销可用身份；回读确认前 TryRead/TryWrite 都会 fail fast。 */
    s_initialized = false;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_USART1, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_9;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &gpio);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    USART_StructInit(&uart);
    uart.USART_BaudRate = BSP_UART1_BAUDRATE;
    uart.USART_WordLength = USART_WordLength_8b;
    uart.USART_StopBits = USART_StopBits_1;
    uart.USART_Parity = USART_Parity_No;
    uart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    uart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &uart);
    USART_Cmd(USART1, ENABLE);

    /* 固定时钟下 BRR 是可审核证据，同时确认收发器确已 enable。 */
    s_initialized = (USART1->BRR == BSP_UART1_EXPECTED_BRR) &&
        ((USART1->CR1 & (USART_CR1_UE | USART_CR1_TE | USART_CR1_RE)) ==
         (USART_CR1_UE | USART_CR1_TE | USART_CR1_RE));
    return s_initialized;
}

/* 尝试向 USART1 写一个字节，不等待发送寄存器空闲。 */
bool BSP_UART1_TryWriteByte(uint8_t value)
{
    if (!s_initialized ||
        (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET))
    {
        return false;
    }
    /* 只在 TXE 当下为真时装入 DR，绝不轮询，所以不会拖慢安全任务。 */
    USART_SendData(USART1, value);
    return true;
}

/* 非阻塞读取 USART1 的一个已到达字节；无数据时保持输出不变。 */
bool BSP_UART1_TryReadByte(uint8_t *value)
{
    if (!s_initialized || (value == NULL) ||
        (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) == RESET))
    {
        return false;
    }
    /* 先验证 RXNE 再提交 caller 输出；无数据与未初始化都保持原值。 */
    *value = (uint8_t)USART_ReceiveData(USART1);
    return true;
}

/* 通过 USART1 有界写出缓冲区内容并返回实际写入字节数。 */
uint16_t BSP_UART1_Write(const uint8_t *data, uint16_t length)
{
    /* UART 当前已经提交的字节数。 */
    uint16_t written;
    /* 等待 UART 发送完成的最大循环计数。 */
    uint32_t spins;

    if ((data == NULL) && (length != 0U))
    {
        return 0U;
    }
    written = 0U;
    while (written < length)
    {
        /* 每个 byte 单独设置上限；超时返回已完成长度，调用者可识别短写。 */
        spins = 0UL;
        while (!BSP_UART1_TryWriteByte(data[written]))
        {
            ++spins;
            if (spins >= BSP_UART1_TX_SPIN_LIMIT)
            {
                return written;
            }
        }
        ++written;
    }
    return written;
}
