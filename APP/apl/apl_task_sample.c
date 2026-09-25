#include "apl_tasks.h"

#include "apl_rtos.h"
#include "fml_config.h"
#include "fml_health.h"
#include "fml_sample.h"

/* 250 ms periodic wake 只提供执行节拍；采样 transaction、identity 与发布均归 FML。 */
void APL_TaskSample(void *argument)
{
    /* 当前任务的周期，单位为 OS tick。 */
    const OS_Tick_t period = OS_MsToTicks(BMS_SAMPLE_PERIOD_MS);
    /* 上一次周期唤醒的 tick。 */
    OS_Tick_t last_wake;

    (void)argument;
    last_wake = OS_TickCount();
    for (;;)
    {
        OS_DelayUntil(&last_wake, period);
        (void)FML_Sample_RunOnce(APL_TimeMs());
        FML_Health_Heartbeat(BMS_HEALTH_TASK_SAMPLE);
    }
}
