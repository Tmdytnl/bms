#ifndef APL_CAN_H
#define APL_CAN_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_policy.h"

/*
 * APL CAN binding：把 FML frame/diagnostic 语义接到 bxCAN queue、ISR 与 10 ms
 * hardware service。它不解析安全策略，也不拥有 Protect/FET/State authority。
 */
bool APL_Can_BindTarget(const BMS_Policy_t *policy);
bool APL_Can_EnableRx(void);
void APL_Can_QueuePeriodic(uint32_t now_ms);
void APL_Can_TxHardwareService(uint32_t now_ms);
void APL_Can_RecordRxFifoOverrunFromISR(void);
void APL_Can_RecordRxQueueDropFromISR(void);

#endif /* APL_CAN_H */
