#include "apl_tasks.h"

#include "apl_can.h"
#include "apl_debug.h"
#include "apl_rtos.h"
#include "bms_health.h"

void APL_TaskCanTx(void *argument)
{
    const TickType_t period = pdMS_TO_TICKS(10U);
    TickType_t last;
    uint32_t now_ms;
    uint32_t last_publish_ms;

    (void)argument;
    last = xTaskGetTickCount();
    last_publish_ms = APL_TimeMs() - 100UL;
    for (;;)
    {
        /* 10 ms hardware service 与 100 ms protocol publication 是两个冻结节拍。 */
        vTaskDelayUntil(&last, period);
        now_ms = APL_TimeMs();
        if ((uint32_t)(now_ms - last_publish_ms) >= 100UL)
        {
            APL_Can_QueuePeriodic(now_ms);
            last_publish_ms = now_ms;
        }
        APL_Can_TxHardwareService(now_ms);
        /* UART service 每轮最多 8 B 且 busy 即返回，不拉长 CAN/heartbeat 周期。 */
        APL_Debug_Service(now_ms);
        BMS_Health_Heartbeat(BMS_HEALTH_TASK_CAN_TX);
    }
}
