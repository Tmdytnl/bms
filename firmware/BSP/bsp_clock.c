#include "bsp_clock.h"

#include "bsp_board_config.h"
#include "stm32f10x.h"
#include "stm32f10x_rcc.h"

/* 核对目标时钟树是否满足本板 72 MHz 与外设分频要求。 */
BSP_ClockStatus_t BSP_Clock_Verify(void)
{
    RCC_ClocksTypeDef clocks;
    uint32_t cfgr;

    if ((RCC->CR & (RCC_CR_HSEON | RCC_CR_HSERDY)) !=
        (RCC_CR_HSEON | RCC_CR_HSERDY))
    {
        return BSP_CLOCK_STATUS_HSE_NOT_READY;
    }
    if ((RCC->CR & (RCC_CR_PLLON | RCC_CR_PLLRDY)) !=
        (RCC_CR_PLLON | RCC_CR_PLLRDY))
    {
        return BSP_CLOCK_STATUS_PLL_NOT_READY;
    }

    cfgr = RCC->CFGR;
    if (((cfgr & RCC_CFGR_SW) != RCC_CFGR_SW_PLL) ||
        ((cfgr & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) ||
        ((cfgr & RCC_CFGR_PLLSRC) == 0U) ||
        ((cfgr & RCC_CFGR_PLLXTPRE) != 0U) ||
        ((cfgr & RCC_CFGR_PLLMULL) != RCC_CFGR_PLLMULL9))
    {
        return BSP_CLOCK_STATUS_SOURCE_MISMATCH;
    }

    if (((cfgr & RCC_CFGR_HPRE) != RCC_CFGR_HPRE_DIV1) ||
        ((cfgr & RCC_CFGR_PPRE1) != RCC_CFGR_PPRE1_DIV2) ||
        ((cfgr & RCC_CFGR_PPRE2) != RCC_CFGR_PPRE2_DIV1))
    {
        return BSP_CLOCK_STATUS_DIVIDER_MISMATCH;
    }

    RCC_GetClocksFreq(&clocks);
    if ((clocks.SYSCLK_Frequency != BSP_BOARD_SYSCLK_HZ) ||
        (clocks.HCLK_Frequency != BSP_BOARD_HCLK_HZ) ||
        (clocks.PCLK1_Frequency != BSP_BOARD_PCLK1_HZ) ||
        (clocks.PCLK2_Frequency != BSP_BOARD_PCLK2_HZ))
    {
        return BSP_CLOCK_STATUS_FREQUENCY_MISMATCH;
    }

    return BSP_CLOCK_STATUS_OK;
}
