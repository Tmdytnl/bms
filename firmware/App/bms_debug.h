#ifndef BMS_DEBUG_H
#define BMS_DEBUG_H

#include <stdint.h>

/*
 * 使用既有 USART1 的只读诊断 telemetry。Task_CANTx 是 sole caller：每 1 s 捕获
 * 一次一致诊断投影，但每个 10 ms service 最多尝试发送 8 B。只调用
 * BSP_UART1_TryWriteByte；TX busy 时保留 line offset，下次从 partial line 继续，
 * 立即把 CPU 还给 CAN service。这里刻意没有 command parser，UART input 无法
 * 到达 fault、FET、balance、policy 或 persistence。
 */
void BMS_Debug_Init(void);
void BMS_Debug_Service(uint32_t now_ms);

#endif /* BMS_DEBUG_H：include guard */
