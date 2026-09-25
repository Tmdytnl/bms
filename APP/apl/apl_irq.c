#include "os_objects_internal.h"
#include "apl_rtos.h"
#include "apl_can.h"

#include <string.h>

#include "fml_can.h"
#include "bsp_can.h"
#include "bsp_exti.h"

/* 清除 ALERT 中断挂起并把唤醒交给 ProtectTask，不在 ISR 执行业务。 */
void EXTI1_IRQHandler(void)
{
    /* ISR 是否唤醒了更高优先级任务。 */
    OS_Result_t higher_priority_task_woken;

    /* ISR 只把 STM32 pending 转成 APL semaphore；BQ/W1C/故障生命周期留给 Protect。 */
    higher_priority_task_woken = OS_FAIL;
    if (BSP_ALERT_EXTI_IsPending())
    {
        BSP_ALERT_EXTI_ClearPending();
        if (g_os_afe_alert_semaphore != NULL)
        {
            (void)OS_SemaphoreGiveFromISR(g_os_afe_alert_semaphore,
                                        &higher_priority_task_woken);
        }
        OS_YieldFromISR(higher_priority_task_woken);
    }
}

#if !defined(TEST_PHASE7_IMAGE)
/* 从 CAN RX FIFO0 搬运报文到 APL 队列，保留 ISR 的有界执行。 */
void USB_LP_CAN1_RX0_IRQHandler(void)
{
    /* ISR 是否唤醒了更高优先级任务。 */
    OS_Result_t higher_priority_task_woken;
    /* 当前硬件或写入操作的目标。 */
    BSP_CanFrame_t target;
    /* 当前接收或发送的一帧报文。 */
    BMS_CanFrame_t frame;

    /* ISR 只复制 wire frame 与接收时刻；service command 校验和安全请求留在 CANRxTask。 */
    higher_priority_task_woken = OS_FAIL;
    if (BSP_CAN_IsRxFifoOverrun())
    {
        BSP_CAN_ClearRxFifoOverrun();
        APL_Can_RecordRxFifoOverrunFromISR();
    }
    while (BSP_CAN_ReceivePending())
    {
        if (BSP_CAN_Receive(&target))
        {
            frame.standard_id = target.id;
            frame.dlc = target.dlc;
            (void)memcpy(frame.data, target.data, sizeof(frame.data));
            frame.received_ms = APL_TimeMsFromISR();
            if ((g_os_can_rx_queue == NULL) ||
                (OS_QueueSendFromISR(g_os_can_rx_queue, &frame,
                                   &higher_priority_task_woken) != OS_PASS))
            {
                APL_Can_RecordRxQueueDropFromISR();
            }
        }
    }
    OS_YieldFromISR(higher_priority_task_woken);
}
#endif
