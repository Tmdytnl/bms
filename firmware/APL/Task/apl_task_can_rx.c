#include "apl_tasks.h"

#include "apl_can.h"
#include "apl_rtos.h"
#include "bms_can.h"
#include "bms_health.h"

void APL_TaskCanRx(void *argument)
{
    BMS_CanFrame_t frame;
    uint32_t now_ms;

    (void)argument;
    (void)APL_Can_EnableRx();
    for (;;)
    {
        if ((xCanRxQueue != NULL) &&
            (xQueueReceive(xCanRxQueue, &frame,
                           pdMS_TO_TICKS(100U)) == pdPASS))
        {
            now_ms = APL_TimeMs();
            if (BMS_Can_RxProcess(&frame, frame.received_ms, now_ms))
            {
                APL_Rtos_RequestProtectService();
            }
        }
        BMS_Health_Heartbeat(BMS_HEALTH_TASK_CAN_RX);
    }
}
