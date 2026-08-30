#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdbool.h>
#include <stdint.h>

/* 配置固定 PA9/PA10、115200 8N1，并回读 BRR/UE/TE/RE 后才返回 true。 */
bool BSP_UART1_Init115200(void);
/* 非阻塞尝试：TXE 未就绪立即返回 false，runtime debug 只能使用此接口。 */
bool BSP_UART1_TryWriteByte(uint8_t value);
/* RXNE 未置位时立即返回 false；失败保持 *value 不变。 */
bool BSP_UART1_TryReadByte(uint8_t *value);
/*
 * 普通有界阻塞写接口，适合启动/受控输出；不得用于 10 ms runtime telemetry。
 * 返回实际写入字节数，短写表示 TXE 在 spin 上限内未恢复。
 */
uint16_t BSP_UART1_Write(const uint8_t *data, uint16_t length);

#endif /* BSP_UART_H：头文件防重复包含 */
