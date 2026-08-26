#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdbool.h>
#include <stdint.h>

bool BSP_UART1_Init115200(void);
/* 非阻塞尝试：TXE 未就绪立即返回 false，runtime debug 只能使用此接口。 */
bool BSP_UART1_TryWriteByte(uint8_t value);
bool BSP_UART1_TryReadByte(uint8_t *value);
/* 普通阻塞写接口，适合启动/受控输出；不得用于 10 ms runtime telemetry。 */
uint16_t BSP_UART1_Write(const uint8_t *data, uint16_t length);

#endif /* BSP_UART_H：include guard */
