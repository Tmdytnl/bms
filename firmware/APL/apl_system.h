#ifndef APL_SYSTEM_H
#define APL_SYSTEM_H

#include <stdbool.h>

#include "bq76940.h"

bool APL_SystemInit(void);
void APL_SystemStart(void);
void APL_SafeIdle(void);
BQ76940_t *APL_SystemAfeDevice(void);

#endif /* APL_SYSTEM_H */
