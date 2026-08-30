#include "apl_tasks.h"

#include "apl_rtos.h"
#include "apl_system.h"
#include "bms_health.h"
#include "bms_protect.h"
#include "bsp_exti.h"

void APL_TaskProtect(void *argument)
{
    BMS_CcSample_t sample;
    bool inserted;
    bool overflowed;
    bool oldest_was_dropped;
    bool retry_pending;
    uint32_t now_ms;

    (void)argument;
    while (!BSP_ALERT_EXTI_Init())
    {
        vTaskDelay(pdMS_TO_TICKS(BMS_PROTECT_RETRY_DELAY_MS));
    }
    retry_pending = BSP_ALERT_PinActive();

    for (;;)
    {
        if (!retry_pending)
        {
            (void)xSemaphoreTake(xAfeAlertSem,
                pdMS_TO_TICKS(BMS_PROTECT_HEALTH_WAIT_MS));
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(BMS_PROTECT_RETRY_DELAY_MS));
        }

        now_ms = APL_TimeMs();
        retry_pending =
            (BMS_Protect_ServicePending(APL_SystemAfeDevice(), now_ms) ==
             BMS_PROTECT_SERVICE_RETRY_REQUIRED);

        if (BMS_Protect_GetPendingCcSample(&sample))
        {
            inserted = APL_Rtos_TransportCcSample(
                &sample, &overflowed, &oldest_was_dropped);
            if (BMS_Protect_CompleteCcTransport(
                    sample.transport_id, inserted, overflowed,
                    oldest_was_dropped) && inserted)
            {
                /* Queue commit precedes the sole-owner CC_READY W1C attempt. */
                retry_pending =
                    (BMS_Protect_ServicePending(
                        APL_SystemAfeDevice(), now_ms) ==
                     BMS_PROTECT_SERVICE_RETRY_REQUIRED);
            }
            else
            {
                retry_pending = true;
            }
        }
        BMS_Protect_ServiceMaintenance(now_ms);
        retry_pending = retry_pending || BSP_ALERT_PinActive();
        BMS_Health_Heartbeat(BMS_HEALTH_TASK_PROTECT);
        APL_Rtos_NotifyStateUrgent();
    }
}
