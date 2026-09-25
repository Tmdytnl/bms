#include "apl_tasks.h"

#include "os_objects_internal.h"
#include "apl_rtos.h"
#include "fml_health.h"
#include "fml_protect.h"
#include "bsp_exti.h"

/* 处理 ALERT 通知、CC 两阶段队列交接和 Protect 有界重试。 */
void APL_TaskProtect(void *argument)
{
    /* 由启动层注入的 AFE 设备句柄。 */
    BQ76940_t *afe_device;
    /* 本轮处理的 CC 电流或 AFE 测量样本。 */
    BMS_CcSample_t sample;
    /* 样本是否成功进入队列。 */
    bool inserted;
    /* 队列是否在本轮交接时溢出。 */
    bool overflowed;
    /* 是否丢弃了队列中最旧的样本。 */
    bool oldest_was_dropped;
    /* 是否仍需重试当前操作。 */
    bool retry_pending;
    /* 本次处理的单调毫秒时刻。 */
    uint32_t now_ms;

    /* dependency 由 APL composition root 在建任务时注入，不通过全局 accessor 回取。 */
    afe_device = (BQ76940_t *)argument;
    while (!BSP_ALERT_EXTI_Init())
    {
        OS_Delay(OS_MsToTicks(BMS_PROTECT_RETRY_DELAY_MS));
    }
    retry_pending = BSP_ALERT_PinActive();

    for (;;)
    {
        if (!retry_pending)
        {
            (void)OS_SemaphoreTake(g_os_afe_alert_semaphore,
                OS_MsToTicks(BMS_PROTECT_HEALTH_WAIT_MS));
        }
        else
        {
            OS_Delay(OS_MsToTicks(BMS_PROTECT_RETRY_DELAY_MS));
        }

        now_ms = APL_TimeMs();
        retry_pending =
            (FML_Protect_ServicePending(afe_device, now_ms) ==
             BMS_PROTECT_SERVICE_RETRY_REQUIRED);

        if (FML_Protect_GetPendingCcSample(&sample))
        {
            inserted = APL_Rtos_TransportCcSample(
                &sample, &overflowed, &oldest_was_dropped);
            if (FML_Protect_CompleteCcTransport(
                    sample.transport_id, inserted, overflowed,
                    oldest_was_dropped) && inserted)
            {
                /*
                 * 两阶段提交：领域 sample 先确定进入 APL queue，再把 transport
                 * 结果回送 Protect。只有这一步已提交，下一次 service 才可尝试
                 * CC_READY W1C；若先清事件，掉电/队列失败会永久丢失该次电流证据。
                 */
                retry_pending =
                    (FML_Protect_ServicePending(
                        afe_device, now_ms) ==
                     BMS_PROTECT_SERVICE_RETRY_REQUIRED);
            }
            else
            {
                retry_pending = true;
            }
        }
        FML_Protect_ServiceMaintenance(now_ms);
        retry_pending = retry_pending || BSP_ALERT_PinActive();
        FML_Health_Heartbeat(BMS_HEALTH_TASK_PROTECT);
        APL_Rtos_NotifyStateUrgent();
    }
}
