#ifndef APL_CAN_H
#define APL_CAN_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_policy.h"

bool APL_Can_BindTarget(const BMS_Policy_t *policy);
bool APL_Can_EnableRx(void);
void APL_Can_QueuePeriodic(uint32_t now_ms);
void APL_Can_TxHardwareService(uint32_t now_ms);
void APL_Can_RecordRxFifoOverrunFromISR(void);
void APL_Can_RecordRxQueueDropFromISR(void);

#endif /* APL_CAN_H */
