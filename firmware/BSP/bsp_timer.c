#include "bsp_timer.h"

#include "bms_config.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_tim.h"

static bool s_timer_initialized;

void BSP_Timer_Init(void)
{
    TIM_TimeBaseInitTypeDef timer;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    TIM_TimeBaseStructInit(&timer);
    timer.TIM_Prescaler = BMS_TIM3_PRESCALER;
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    timer.TIM_Period = BMS_TIM3_AUTORELOAD;
    timer.TIM_ClockDivision = TIM_CKD_DIV1;
    timer.TIM_RepetitionCounter = 0U;
    TIM_TimeBaseInit(TIM3, &timer);
    TIM_GenerateEvent(TIM3, TIM_EventSource_Update);
    TIM_SetCounter(TIM3, 0U);
    TIM_ClearFlag(TIM3, TIM_FLAG_Update);
    TIM_Cmd(TIM3, ENABLE);
    s_timer_initialized = true;
}

bool BSP_Timer_IsInitialized(void)
{
    return s_timer_initialized;
}

uint16_t BSP_TimeUs16(void)
{
    return (uint16_t)TIM_GetCounter(TIM3);
}

uint16_t BSP_TimeElapsedUs16(uint16_t start_us)
{
    return BSP_TimeDeltaUs16(BSP_TimeUs16(), start_us);
}

uint16_t BSP_TimeDeltaUs16(uint16_t now_us, uint16_t start_us)
{
    return BSP_TIME_DELTA_US16(now_us, start_us);
}

bool BSP_DelayUs(uint32_t delay_us)
{
    if (!s_timer_initialized)
    {
        return false;
    }

    while (delay_us != 0UL)
    {
        uint32_t guard;
        uint16_t start;
        uint16_t chunk;

        chunk = (delay_us > 32767UL) ? 32767U : (uint16_t)delay_us;
        start = BSP_TimeUs16();
        guard = ((uint32_t)chunk + 1UL) *
                BMS_TIM3_DELAY_SPIN_GUARD_PER_US;
        while (((uint16_t)(BSP_TimeUs16() - start) < chunk) &&
               (guard != 0UL))
        {
            --guard;
        }
        if (guard == 0UL)
        {
            return false;
        }
        delay_us -= (uint32_t)chunk;
    }
    return true;
}
