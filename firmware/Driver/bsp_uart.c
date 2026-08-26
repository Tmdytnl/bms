#include "bsp_uart.h"

/*
 * USART1 固定 115200 8N1。TryWriteByte/TryReadByte 只观察一次状态位，适合
 * best-effort runtime service；Write 会等待每个 TXE，调用者必须选择允许阻塞
 * 的上下文。驱动不解析命令，也不拥有任何安全状态。
 */

#include <stddef.h>

#include "bms_build_assert.h"
#include "bms_config.h"

#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_usart.h"

#define BSP_UART1_BAUDRATE                      (115200UL)
#define BSP_UART1_EXPECTED_BRR                  (0x0271U)
#define BSP_UART1_TX_SPIN_LIMIT                 (100000UL)

BMS_BUILD_ASSERT(BMS_UART_TX_PORT_ID == BMS_GPIO_PORT_A_ID,
                 uart_tx_pin_is_on_port_a);
BMS_BUILD_ASSERT(BMS_UART_TX_PIN == 9U,
                 uart_tx_pin_is_pa9);
BMS_BUILD_ASSERT(BMS_UART_RX_PORT_ID == BMS_GPIO_PORT_A_ID,
                 uart_rx_pin_is_on_port_a);
BMS_BUILD_ASSERT(BMS_UART_RX_PIN == 10U,
                 uart_rx_pin_is_pa10);
BMS_BUILD_ASSERT(BMS_PCLK2_HZ == 72000000UL,
                 uart_pclk2_is_seventy_two_mhz);

static bool s_initialized;

bool BSP_UART1_Init115200(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef uart;

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

    s_initialized = (USART1->BRR == BSP_UART1_EXPECTED_BRR) &&
        ((USART1->CR1 & (USART_CR1_UE | USART_CR1_TE | USART_CR1_RE)) ==
         (USART_CR1_UE | USART_CR1_TE | USART_CR1_RE));
    return s_initialized;
}

bool BSP_UART1_TryWriteByte(uint8_t value)
{
    if (!s_initialized ||
        (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET))
    {
        return false;
    }
    USART_SendData(USART1, value);
    return true;
}

bool BSP_UART1_TryReadByte(uint8_t *value)
{
    if (!s_initialized || (value == NULL) ||
        (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) == RESET))
    {
        return false;
    }
    *value = (uint8_t)USART_ReceiveData(USART1);
    return true;
}

uint16_t BSP_UART1_Write(const uint8_t *data, uint16_t length)
{
    uint16_t written;
    uint32_t spins;

    if ((data == NULL) && (length != 0U))
    {
        return 0U;
    }
    written = 0U;
    while (written < length)
    {
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
