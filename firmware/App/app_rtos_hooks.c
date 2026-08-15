#include "app_rtos.h"

#include <stddef.h>

/*
 * FreeRTOS application hooks (Phase 6).
 *
 * H-09 baseline: configASSERT + configCHECK_FOR_STACK_OVERFLOW=2 with
 * implemented hooks. Assert/overflow/malloc-failure are fatal diagnostics:
 * disable interrupts and stop. The full health/supervision reporting path
 * (IWDG, CAN diagnostic) lands in Phase 9/11; these stubs keep the system
 * from continuing in an undefined state.
 */

static void App_Rtos_FatalStop(void)
{
    for (;;)
    {
    }
}

void vApplicationAssertFailedHandler(void)
{
    App_Rtos_FatalStop();
}

void vApplicationMallocFailedHook(void)
{
    App_Rtos_FatalStop();
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    App_Rtos_FatalStop();
}

void vApplicationIdleHook(void)
{
    /* Idle hook enabled (configUSE_IDLE_HOOK=1); no work in Phase 6. */
}
