#include "apl_rtos.h"

#include <stddef.h>

/*
 * H-09：configASSERT 与 stack-overflow level 2 都有正式 hook。assert、overflow、
 * malloc failure 属于 fatal diagnostic：关闭中断并停机，禁止在未定义状态继续。
 * 正常运行期 task health 与 IWDG supervision 仍由 StateTask owner 负责。
 */

static void APL_Rtos_FatalStop(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}

void vApplicationAssertFailedHandler(void)
{
    APL_Rtos_FatalStop();
}

void vApplicationMallocFailedHook(void)
{
    APL_Rtos_FatalStop();
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    APL_Rtos_FatalStop();
}

void vApplicationIdleHook(void)
{
    /* 已启用 Idle hook，但刻意不在 idle context 隐式执行任何业务。 */
}
