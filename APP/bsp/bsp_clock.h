#ifndef BSP_CLOCK_H
#define BSP_CLOCK_H

#include <stdint.h>

typedef enum
{
    BSP_CLOCK_STATUS_OK = 0,         /* 时钟树满足目标配置。 */
    BSP_CLOCK_STATUS_HSE_NOT_READY,  /* 外部高速时钟未稳定。 */
    BSP_CLOCK_STATUS_PLL_NOT_READY,  /* PLL 未锁定。 */
    BSP_CLOCK_STATUS_SOURCE_MISMATCH, /* SYSCLK 来源不符合计划。 */
    BSP_CLOCK_STATUS_DIVIDER_MISMATCH, /* 总线分频不符合计划。 */
    BSP_CLOCK_STATUS_FREQUENCY_MISMATCH /* 推导频率不符合目标值。 */
} BSP_ClockStatus_t;

/* 核对目标时钟树是否满足本板 72 MHz 与外设分频要求。 */
BSP_ClockStatus_t BSP_Clock_Verify(void);

#endif /* BSP_CLOCK_H：include guard */
