#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdbool.h>
#include <stdint.h>

bool BSP_UART1_Init115200(void);
bool BSP_UART1_TryWriteByte(uint8_t value);
bool BSP_UART1_TryReadByte(uint8_t *value);
uint16_t BSP_UART1_Write(const uint8_t *data, uint16_t length);

#endif /* BSP_UART_H */
