#ifndef BSP_TIMER_H
#define BSP_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#define BSP_TIME_DELTA_US16(now_us_, start_us_) \
    ((uint16_t)((uint16_t)(now_us_) - (uint16_t)(start_us_)))

void BSP_Timer_Init(void);
bool BSP_Timer_IsInitialized(void);
uint16_t BSP_TimeUs16(void);
uint16_t BSP_TimeDeltaUs16(uint16_t now_us, uint16_t start_us);
uint16_t BSP_TimeElapsedUs16(uint16_t start_us);
bool BSP_DelayUs(uint32_t delay_us);

#endif /* BSP_TIMER_H */
