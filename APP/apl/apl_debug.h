#ifndef APL_DEBUG_H
#define APL_DEBUG_H

#include <stdint.h>

/* CANTxTask 调用的只读、非阻塞 UART telemetry service；不提供 command path。 */
void APL_Debug_Service(uint32_t now_ms);

#endif /* APL_DEBUG_H */
