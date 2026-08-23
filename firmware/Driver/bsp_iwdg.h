#ifndef BSP_IWDG_H
#define BSP_IWDG_H

#include <stdbool.h>
#include <stdint.h>

/* Nominal only: real timeout depends on target LSI tolerance. */
bool BSP_IWDG_StartNominal(uint32_t timeout_ms);
void BSP_IWDG_Feed(void);

#endif /* BSP_IWDG_H */
