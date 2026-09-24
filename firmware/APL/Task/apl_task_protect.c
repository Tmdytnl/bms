#include "apl_tasks.h"

#include "apl_rtos_internal.h"
#include "bms_health.h"
#include "bms_protect.h"
#include "bsp_exti.h"

/* 处理 ALERT 通知、CC 两阶段队列交接和 Protect 有界重试。 */
void APL_TaskProtect(void *argument)
{
    BQ76940_t *afe_device;
    BMS_CcSample_t sample;
    bool inserted;
    bool overflowed;
    bool oldest_was_dropped;
    bool retry_pending;
    uint32_t now_ms;

    /* dependency 由 APL composition root 在建任务时注入，不通过全局 accessor 回取。 */
    afe_device = (BQ76940_t *)argument;
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
            (BMS_Protect_ServicePending(afe_device, now_ms) ==
             BMS_PROTECT_SERVICE_RETRY_REQUIRED);

        if (BMS_Protect_GetPendingCcSample(&sample))
        {
            inserted = APL_Rtos_TransportCcSample(
                &sample, &overflowed, &oldest_was_dropped);
            if (BMS_Protect_CompleteCcTransport(
                    sample.transport_id, inserted, overflowed,
                    oldest_was_dropped) && inserted)
            {
                /*
                 * 两阶段提交：领域 sample 先确定进入 APL queue，再把 transport
                 * 结果回送 Protect。只有这一步已提交，下一次 service 才可尝试
                 * CC_READY W1C；若先清事件，掉电/队列失败会永久丢失该次电流证据。
                 */
                retry_pending =
                    (BMS_Protect_ServicePending(
                        afe_device, now_ms) ==
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
