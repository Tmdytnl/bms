#ifndef BSP_IWDG_H
#define BSP_IWDG_H

#include <stdbool.h>
#include <stdint.h>

/* timeout_ms 用于选择 nominal prescaler/reload；开始后不可停止。 */
bool BSP_IWDG_StartNominal(uint32_t timeout_ms);
void BSP_IWDG_Feed(void);

#endif /* BSP_IWDG_H：include guard */
