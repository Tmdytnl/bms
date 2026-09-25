#ifndef FML_DEBUG_H
#define FML_DEBUG_H

#include <stdbool.h>
#include <stdint.h>

/*
 * 使用既有 USART1 的只读诊断 telemetry。APL CAN Tx task 是 sole caller：每 1 s 捕获
 * 一次一致诊断投影，但每个 10 ms service 最多尝试发送 8 B。只调用
 * APL 的非阻塞 UART adapter；TX busy 时保留 line offset，下次从 partial line 继续，
 * 立即把 CPU 还给 CAN service。这里刻意没有 command parser，UART input 无法
 * 到达 fault、FET、balance、policy 或 persistence。
 *
 * 不使用 printf 整行阻塞发送：115200 8N1 每个字节约占 10 bit time，数百字节
 * 会消耗数十毫秒。诊断允许跨多个周期慢慢输出，安全控制与 CAN mailbox 却不能
 * 等待 UART hardware；因此“debug 可以延后，控制绝不等待 debug”。
 */
void FML_Debug_Init(void);
/* 按周期生成只读 BMS1 诊断行，不阻塞等待 UART 发送。 */
bool FML_Debug_PrepareSnapshot(uint32_t now_ms,
                               uint32_t free_heap_bytes,
                               uint32_t minimum_heap_bytes);
/* 读取当前诊断发送位置的字节而不推进偏移。 */
bool FML_Debug_PeekByte(uint8_t *value);
/* 确认一个字节已由 UART 接收并推进发送偏移。 */
void FML_Debug_ConsumeByte(void);

#endif /* FML_DEBUG_H：头文件防重复包含 */
