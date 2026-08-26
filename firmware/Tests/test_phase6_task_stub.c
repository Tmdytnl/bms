#include "app_rtos.h"

/* 本 construction image 只验证 scheduler 前七任务创建；正式 ProtectTask 在独立
 * regression image 执行，因此这里保留 non-running entry。 */
void Task_Protect(void *argument)
{
    (void)argument;
    for (;;)
    {
    }
}

/* production SampleTask 在独立 image 执行；construction image 不引入 measurement driver。 */
void Task_Sample(void *argument)
{
    (void)argument;
    for (;;)
    {
    }
}
