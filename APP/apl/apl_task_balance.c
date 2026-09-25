#include "apl_tasks.h"

#include "apl_rtos.h"
#include "fml_balance.h"
#include "fml_health.h"
#include "fml_policy.h"

/* APL 只提供 1 s execution context；CELLBAL 选择、复核与 sole-writer 事务归 FML。 */
void APL_TaskBalance(void *argument)
{
    /* 当前任务的周期，单位为 OS tick。 */
    OS_Tick_t period;
    /* 上一次周期唤醒的 tick。 */
    OS_Tick_t last;

    (void)argument;
    period = OS_MsToTicks(FML_Policy_Get()->balance.period_ms);
    last = OS_TickCount();
    for (;;)
    {
        OS_DelayUntil(&last, period);
        FML_Balance_RunOnce(APL_TimeMs());
        FML_Health_Heartbeat(BMS_HEALTH_TASK_BALANCE);
    }
}
