#ifndef BSP_TIMER_H
#define BSP_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#define BSP_TIME_DELTA_US16(now_us_, start_us_) \
    ((uint16_t)((uint16_t)(now_us_) - (uint16_t)(start_us_)))

/* 初始化 TIM3 微秒时间基准，供软件 I2C 使用。 */
void BSP_Timer_Init(void);
/* 读取微秒计时器已配置标志。 */
bool BSP_Timer_IsInitialized(void);
/* 读取 TIM3 的 16 位微秒计数值。 */
uint16_t BSP_TimeUs16(void);
/* 按 16 位回绕语义计算两个微秒计数值之间的差。 */
uint16_t BSP_TimeDeltaUs16(uint16_t now_us, uint16_t start_us);
/* 以 16 位回绕差判断微秒等待是否到期。 */
uint16_t BSP_TimeElapsedUs16(uint16_t start_us);
/* 用 TIM3 执行有界微秒延时，失败时向调用者报告。 */
bool BSP_DelayUs(uint32_t delay_us);

#endif /* BSP_TIMER_H：include guard */
