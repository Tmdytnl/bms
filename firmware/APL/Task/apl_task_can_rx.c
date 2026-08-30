#include "apl_tasks.h"

#include "apl_can.h"
#include "apl_rtos_internal.h"
#include "bms_can.h"
#include "bms_health.h"

void APL_TaskCanRx(void *argument)
{
    BMS_CanFrame_t frame;
    uint32_t now_ms;

    (void)argument;
    /* IRQ handoff 环境到此已就绪，才允许 BSP unmask RX interrupt。 */
    (void)APL_Can_EnableRx();
    for (;;)
    {
        if ((xCanRxQueue != NULL) &&
            (xQueueReceive(xCanRxQueue, &frame,
                           pdMS_TO_TICKS(100U)) == pdPASS))
        {
            now_ms = APL_TimeMs();
            /* FML 只返回“有合法 source-specific request 待服务”，APL 负责唤醒。 */
            if (BMS_Can_RxProcess(&frame, frame.received_ms, now_ms))
            {
                APL_Rtos_RequestProtectService();
            }
        }
        BMS_Health_Heartbeat(BMS_HEALTH_TASK_CAN_RX);
    }
}
