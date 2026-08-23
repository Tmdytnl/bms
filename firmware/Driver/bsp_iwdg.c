#include "bsp_iwdg.h"

#include "stm32f10x.h"

#define BSP_IWDG_NOMINAL_LSI_HZ                 (40000UL)
#define BSP_IWDG_PRESCALER                      (256UL)
#define BSP_IWDG_PRESCALER_CODE                 (6UL)
#define BSP_IWDG_RELOAD_MAX                     (0x0FFFUL)
#define BSP_IWDG_UPDATE_LIMIT                   (100000UL)

bool BSP_IWDG_StartNominal(uint32_t timeout_ms)
{
    uint32_t reload;
    uint32_t wait;

    if (timeout_ms == 0UL)
    {
        return false;
    }
    reload = ((BSP_IWDG_NOMINAL_LSI_HZ * timeout_ms) /
              (BSP_IWDG_PRESCALER * 1000UL));
    if ((reload == 0UL) || (reload > (BSP_IWDG_RELOAD_MAX + 1UL)))
    {
        return false;
    }
    --reload;
    IWDG->KR = 0x5555U;
    IWDG->PR = BSP_IWDG_PRESCALER_CODE;
    IWDG->RLR = reload;
    wait = 0UL;
    while ((IWDG->SR != 0U) && (wait < BSP_IWDG_UPDATE_LIMIT))
    {
        ++wait;
    }
    if (IWDG->SR != 0U)
    {
        return false;
    }
    IWDG->KR = 0xAAAAU;
    IWDG->KR = 0xCCCCU;
    return true;
}

void BSP_IWDG_Feed(void)
{
    IWDG->KR = 0xAAAAU;
}
