#include "apl_rtos.h"
#include "apl_can.h"

#include <string.h>

#include "bms_can.h"
#include "bsp_can.h"
#include "bsp_exti.h"

void EXTI1_IRQHandler(void)
{
    BaseType_t higher_priority_task_woken;

    higher_priority_task_woken = pdFALSE;
    if (BSP_ALERT_EXTI_IsPending())
    {
        BSP_ALERT_EXTI_ClearPending();
        if (xAfeAlertSem != NULL)
        {
            (void)xSemaphoreGiveFromISR(xAfeAlertSem,
                                        &higher_priority_task_woken);
        }
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

#if !defined(TEST_PHASE7_IMAGE)
void USB_LP_CAN1_RX0_IRQHandler(void)
{
    BaseType_t higher_priority_task_woken;
    BSP_CanFrame_t target;
    BMS_CanFrame_t frame;

    higher_priority_task_woken = pdFALSE;
    if (BSP_CAN_IsRxFifoOverrun())
    {
        BSP_CAN_ClearRxFifoOverrun();
        APL_Can_RecordRxFifoOverrunFromISR();
    }
    while (BSP_CAN_ReceivePending())
    {
        if (BSP_CAN_Receive(&target))
        {
            frame.ext_id = target.id;
            frame.dlc = target.dlc;
            (void)memcpy(frame.data, target.data, sizeof(frame.data));
            frame.received_ms = APL_TimeMsFromISR();
            if ((xCanRxQueue == NULL) ||
                (xQueueSendFromISR(xCanRxQueue, &frame,
                                   &higher_priority_task_woken) != pdPASS))
            {
                APL_Can_RecordRxQueueDropFromISR();
            }
        }
    }
    portYIELD_FROM_ISR(higher_priority_task_woken);
}
#endif
