#include "apl_tasks.h"

#include "apl_rtos.h"
#include "bms_balance.h"
#include "bms_health.h"
#include "bms_policy.h"

void APL_TaskBalance(void *argument)
{
    TickType_t period;
    TickType_t last;

    (void)argument;
    period = pdMS_TO_TICKS(BMS_Policy_Get()->balance.period_ms);
    last = xTaskGetTickCount();
    for (;;)
    {
        vTaskDelayUntil(&last, period);
        BMS_Balance_RunOnce(APL_TimeMs());
        BMS_Health_Heartbeat(BMS_HEALTH_TASK_BALANCE);
    }
}
