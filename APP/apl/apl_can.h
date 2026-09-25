#ifndef APL_CAN_H
#define APL_CAN_H

#include <stdbool.h>
#include <stdint.h>

#include "fml_policy.h"

/*
 * APL CAN binding：把 FML frame/diagnostic 语义接到 bxCAN queue、ISR 与 10 ms
 * hardware service。它不解析安全策略，也不拥有 Protect/FET/State authority。
 */
bool APL_Can_BindTarget(const BMS_Policy_t *policy);
/* 在队列准备完成后开启 bxCAN 接收中断。 */
bool APL_Can_EnableRx(void);
/* 把 FML 编码的周期诊断帧投递到 APL 发送队列。 */
void APL_Can_QueuePeriodic(uint32_t now_ms);
/* 有界处理发送队列、邮箱与 bus-off 硬件恢复。 */
void APL_Can_TxHardwareService(uint32_t now_ms);
/* 在中断上下文累计一次 CAN RX FIFO 溢出。 */
void APL_Can_RecordRxFifoOverrunFromISR(void);
/* 在中断上下文累计一次 CAN RX 队列丢帧。 */
void APL_Can_RecordRxQueueDropFromISR(void);

#endif /* APL_CAN_H */
