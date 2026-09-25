#include "apl_tasks.h"

#include "apl_can.h"
#include "os_objects_internal.h"
#include "apl_rtos.h"
#include "fml_can.h"
#include "fml_health.h"

/* 消费 CAN 接收队列并把服务请求交给领域 owner。 */
void APL_TaskCanRx(void *argument)
{
    /* 当前接收或发送的一帧报文。 */
    BMS_CanFrame_t frame;
    /* 本次处理的单调毫秒时刻。 */
    uint32_t now_ms;

    (void)argument;
    /* IRQ handoff 环境到此已就绪，才允许 BSP unmask RX interrupt。 */
    (void)APL_Can_EnableRx();
    for (;;)
    {
        if ((g_os_can_rx_queue != NULL) &&
            (OS_QueueReceive(g_os_can_rx_queue, &frame,
                           OS_MsToTicks(100U)) == OS_PASS))
        {
            now_ms = APL_TimeMs();
            /* FML 只返回“有合法 source-specific request 待服务”，APL 负责唤醒。 */
            if (FML_Can_RxProcess(&frame, frame.received_ms, now_ms))
            {
                APL_Rtos_RequestProtectService();
            }
        }
        FML_Health_Heartbeat(BMS_HEALTH_TASK_CAN_RX);
    }
}
