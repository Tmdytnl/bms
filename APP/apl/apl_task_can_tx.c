#include "apl_tasks.h"

#include "apl_can.h"
#include "apl_debug.h"
#include "apl_rtos.h"
#include "fml_health.h"

/* 调度诊断帧发送、硬件重试和有界 UART 输出。 */
void APL_TaskCanTx(void *argument)
{
    /* 当前任务的周期，单位为 OS tick。 */
    const OS_Tick_t period = OS_MsToTicks(10U);
    /* 上一次周期唤醒的 tick。 */
    OS_Tick_t last;
    /* 本次处理的单调毫秒时刻。 */
    uint32_t now_ms;
    /* 上一次诊断发布的毫秒时刻。 */
    uint32_t last_publish_ms;

    (void)argument;
    last = OS_TickCount();
    last_publish_ms = APL_TimeMs() - 100UL;
    for (;;)
    {
        /* 10 ms hardware service 与 100 ms protocol publication 是两个冻结节拍。 */
        OS_DelayUntil(&last, period);
        now_ms = APL_TimeMs();
        if ((uint32_t)(now_ms - last_publish_ms) >= 100UL)
        {
            APL_Can_QueuePeriodic(now_ms);
            last_publish_ms = now_ms;
        }
        APL_Can_TxHardwareService(now_ms);
        /* UART service 每轮最多 8 B 且 busy 即返回，不拉长 CAN/heartbeat 周期。 */
        APL_Debug_Service(now_ms);
        FML_Health_Heartbeat(BMS_HEALTH_TASK_CAN_TX);
    }
}
