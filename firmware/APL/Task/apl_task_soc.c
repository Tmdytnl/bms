#include "apl_tasks.h"

#include "apl_rtos.h"
#include "bms_health.h"
#include "bms_persistence.h"
#include "bms_policy.h"
#include "bms_soc.h"

void APL_TaskSoc(void *argument)
{
    BMS_CcSample_t samples[BMS_SOC_MAX_CC_SAMPLES_PER_RUN];
    BMS_SocSnapshot_t snapshot;
    const BMS_Policy_t *policy;
    TickType_t period;
    TickType_t last;
    EventBits_t events;
    uint32_t now_ms;
    uint8_t count;

    (void)argument;
    policy = BMS_Policy_Get();
    period = pdMS_TO_TICKS(policy->soc.period_ms);
    last = xTaskGetTickCount();
    for (;;)
    {
        vTaskDelayUntil(&last, period);
        count = 0U;
        while ((count < BMS_SOC_MAX_CC_SAMPLES_PER_RUN) &&
               (xCcSampleQueue != NULL) &&
               (xQueueReceive(xCcSampleQueue, &samples[count], 0U) == pdPASS))
        {
            ++count;
        }
        events = xEventGroupClearBits(xSysEvents, EVT_CC_QUEUE_OVERFLOW);
        now_ms = APL_TimeMs();
        BMS_Soc_RunOnce(now_ms, samples, count,
                        (events & EVT_CC_QUEUE_OVERFLOW) != 0U);
        snapshot = BMS_Soc_GetSnapshot();
        (void)BMS_Persistence_TargetServiceSoc(
            snapshot.soc_permille,
            snapshot.remaining_capacity_mah,
            snapshot.queue_gap_count,
            snapshot.generation_change_count,
            snapshot.valid,
            now_ms);
        BMS_Health_Heartbeat(BMS_HEALTH_TASK_SOC);
    }
}
