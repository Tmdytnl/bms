#include "FreeRTOS.h"
#include "task.h"

#include <stddef.h>

/*
 * H-09：configASSERT 与 stack-overflow level 2 都有正式 hook。assert、overflow、
 * malloc failure 属于 fatal diagnostic：关闭中断并停机，禁止在未定义状态继续。
 * 正常运行期 task health 与 IWDG supervision 仍由 StateTask owner 负责。
 */

static void OS_FatalStop(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}

/* 在断言失败时进入不可恢复的安全停机路径。 */
void vApplicationAssertFailedHandler(void)
{
    OS_FatalStop();
}

/* 在 FreeRTOS 堆分配失败时进入安全停机路径。 */
void vApplicationMallocFailedHook(void)
{
    OS_FatalStop();
}

/* 在任务栈溢出时进入安全停机路径。 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    OS_FatalStop();
}

/* 提供 FreeRTOS idle 钩子，不转移任务安全职责。 */
void vApplicationIdleHook(void)
{
    /* 已启用 Idle hook，但刻意不在 idle context 隐式执行任何业务。 */
}
