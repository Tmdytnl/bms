#ifndef BMS_DEBUG_H
#define BMS_DEBUG_H

#include <stdint.h>

/*
 * Read-only bring-up telemetry over the already configured USART1 binding.
 * Task_CANTx is the sole caller. Output is drained in bounded chunks so the
 * diagnostic path does not monopolize the 10 ms CAN service loop. There is
 * deliberately no command parser and no path from UART input to faults, FETs,
 * balancing, policy or persistence.
 */
void BMS_Debug_Init(void);
void BMS_Debug_Service(uint32_t now_ms);

#endif /* BMS_DEBUG_H */
