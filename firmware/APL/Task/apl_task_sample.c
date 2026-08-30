#include "apl_tasks.h"

#include "apl_rtos.h"
#include "bms_config.h"
#include "bms_health.h"
#include "bms_sample.h"

void APL_TaskSample(void *argument)
{
    const TickType_t period = pdMS_TO_TICKS(BMS_SAMPLE_PERIOD_MS);
    TickType_t last_wake;

    (void)argument;
    last_wake = xTaskGetTickCount();
    for (;;)
    {
        vTaskDelayUntil(&last_wake, period);
        (void)BMS_Sample_RunOnce(APL_TimeMs());
        BMS_Health_Heartbeat(BMS_HEALTH_TASK_SAMPLE);
    }
}
