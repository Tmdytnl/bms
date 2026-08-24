#include "app_rtos.h"

#include <stddef.h>

/*
 * H-09 baseline: configASSERT + configCHECK_FOR_STACK_OVERFLOW=2 with
 * implemented hooks. Assert/overflow/malloc-failure are fatal diagnostics:
 * disable interrupts and stop so execution cannot continue in an undefined
 * state. Runtime task health and IWDG supervision remain in StateTask.
 */

static void App_Rtos_FatalStop(void)
{
    taskDISABLE_INTERRUPTS();
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
    /* Idle hook enabled (configUSE_IDLE_HOOK=1); intentionally no work. */
}
