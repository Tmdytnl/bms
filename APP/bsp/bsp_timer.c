#include "bsp_timer.h"

#include "bsp_board_config.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_tim.h"

/* TIM3 微秒时基可供软件 I2C 使用的标志。 */
static bool s_timer_initialized;

/* 初始化 TIM3 微秒时间基准，供软件 I2C 使用。 */
void BSP_Timer_Init(void)
{
    /* 当前使用的定时器外设。 */
    TIM_TimeBaseInitTypeDef timer;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    TIM_TimeBaseStructInit(&timer);
    timer.TIM_Prescaler = BSP_BOARD_TIM3_PRESCALER;
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    timer.TIM_Period = BSP_BOARD_TIM3_AUTORELOAD;
    timer.TIM_ClockDivision = TIM_CKD_DIV1;
    timer.TIM_RepetitionCounter = 0U;
    TIM_TimeBaseInit(TIM3, &timer);
    TIM_GenerateEvent(TIM3, TIM_EventSource_Update);
    TIM_SetCounter(TIM3, 0U);
    TIM_ClearFlag(TIM3, TIM_FLAG_Update);
    TIM_Cmd(TIM3, ENABLE);
    s_timer_initialized = true;
}

/* 读取微秒计时器已配置标志。 */
bool BSP_Timer_IsInitialized(void)
{
    return s_timer_initialized;
}

/* 读取 TIM3 的 16 位微秒计数值。 */
uint16_t BSP_TimeUs16(void)
{
    return (uint16_t)TIM_GetCounter(TIM3);
}

/* 以 16 位回绕差判断微秒等待是否到期。 */
uint16_t BSP_TimeElapsedUs16(uint16_t start_us)
{
    return BSP_TimeDeltaUs16(BSP_TimeUs16(), start_us);
}

/* 按 16 位回绕语义计算两个微秒计数值之间的差。 */
uint16_t BSP_TimeDeltaUs16(uint16_t now_us, uint16_t start_us)
{
    return BSP_TIME_DELTA_US16(now_us, start_us);
}

/* 用 TIM3 执行有界微秒延时，失败时向调用者报告。 */
bool BSP_DelayUs(uint32_t delay_us)
{
    if (!s_timer_initialized)
    {
        return false;
    }

    while (delay_us != 0UL)
    {
        /* 限制硬件等待或总线恢复循环的计数器。 */
        uint32_t guard;
        /* 当前硬件延时或总线操作的起始计数。 */
        uint16_t start;
        /* 本轮延时分割出的有界微秒片段。 */
        uint16_t chunk;

        chunk = (delay_us > 32767UL) ? 32767U : (uint16_t)delay_us;
        start = BSP_TimeUs16();
        guard = ((uint32_t)chunk + 1UL) *
                BSP_BOARD_TIM3_DELAY_SPIN_GUARD_PER_US;
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
