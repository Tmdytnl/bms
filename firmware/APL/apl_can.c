#include "apl_can.h"

#include <limits.h>
#include <string.h>

#include "apl_rtos.h"
#include "bms_can.h"
#include "bsp_can.h"

#define APL_CAN_TARGET_RETRY_MS                  (1000UL)

static const BMS_Policy_t *s_policy;
static uint32_t s_last_init_attempt_ms;
static bool s_init_attempted;
static bool s_rx_enabled;
static volatile uint32_t s_rx_fifo_overrun_pending;
static volatile uint32_t s_rx_queue_drop_pending;

static void APL_Can_FlushIsrDiagnostics(void)
{
    uint32_t overrun_count;
    uint32_t drop_count;

    taskENTER_CRITICAL();
    overrun_count = s_rx_fifo_overrun_pending;
    drop_count = s_rx_queue_drop_pending;
    s_rx_fifo_overrun_pending = 0UL;
    s_rx_queue_drop_pending = 0UL;
    taskEXIT_CRITICAL();
    BMS_Can_RecordDiagnosticCount(
        BMS_CAN_DIAG_TARGET_RX_FIFO_OVERRUN, overrun_count);
    BMS_Can_RecordDiagnosticCount(
        BMS_CAN_DIAG_TARGET_RX_QUEUE_DROP, drop_count);
}

void APL_Can_RecordRxFifoOverrunFromISR(void)
{
    if (s_rx_fifo_overrun_pending < UINT32_MAX)
    {
        ++s_rx_fifo_overrun_pending;
    }
}

void APL_Can_RecordRxQueueDropFromISR(void)
{
    if (s_rx_queue_drop_pending < UINT32_MAX)
    {
        ++s_rx_queue_drop_pending;
    }
}

bool APL_Can_BindTarget(const BMS_Policy_t *policy)
{
    s_policy = BMS_Policy_Validate(policy) ? policy : NULL;
    s_init_attempted = true;
    s_last_init_attempt_ms = 0UL;
    s_rx_enabled = false;
    s_rx_fifo_overrun_pending = 0UL;
    s_rx_queue_drop_pending = 0UL;
    if ((s_policy == NULL) || !s_policy->can.standard_11_bit_ids ||
        !BSP_CAN_Init500K(s_policy->can.service_rx_id))
    {
        BMS_Can_RecordDiagnostic(BMS_CAN_DIAG_TARGET_INIT_FAILURE);
        return false;
    }
    return true;
}

bool APL_Can_EnableRx(void)
{
    if (!BSP_CAN_EnableRxInterrupt())
    {
        BMS_Can_RecordDiagnostic(BMS_CAN_DIAG_TARGET_INIT_FAILURE);
        return false;
    }
    s_rx_enabled = true;
    return true;
}

void APL_Can_QueuePeriodic(uint32_t now_ms)
{
    BMS_CanFrame_t frames[BMS_CAN_TX_FRAME_COUNT];
    uint8_t count;
    uint8_t index;

    count = BMS_Can_BuildPeriodicFrames(now_ms, frames);
    for (index = 0U; index < count; ++index)
    {
        if ((xCanTxQueue != NULL) &&
            (xQueueSend(xCanTxQueue, &frames[index], 0U) == pdPASS))
        {
            BMS_Can_RecordDiagnostic(BMS_CAN_DIAG_TX_ENQUEUED);
        }
        else
        {
            BMS_Can_RecordDiagnostic(BMS_CAN_DIAG_TX_QUEUE_DROP);
        }
    }
}

void APL_Can_TxHardwareService(uint32_t now_ms)
{
    BMS_CanFrame_t queued;
    BSP_CanFrame_t target;
    BSP_CanTxResult_t result;

    APL_Can_FlushIsrDiagnostics();
    if (!BSP_CAN_IsInitialized())
    {
        if (s_init_attempted &&
            ((uint32_t)(now_ms - s_last_init_attempt_ms) <
             APL_CAN_TARGET_RETRY_MS))
        {
            return;
        }
        s_init_attempted = true;
        s_last_init_attempt_ms = now_ms;
        s_rx_enabled = false;
        if ((s_policy == NULL) ||
            !BSP_CAN_Init500K(s_policy->can.service_rx_id))
        {
            BMS_Can_RecordDiagnostic(BMS_CAN_DIAG_TARGET_INIT_FAILURE);
            return;
        }
    }
    if (!s_rx_enabled && !APL_Can_EnableRx())
    {
        return;
    }
    if (BSP_CAN_IsBusOff())
    {
        if (BSP_CAN_Recover())
        {
            BMS_Can_RecordDiagnostic(BMS_CAN_DIAG_TARGET_BUS_OFF_RECOVERY);
        }
        else
        {
            s_rx_enabled = false;
            return;
        }
    }
    while ((xCanTxQueue != NULL) &&
           (xQueueReceive(xCanTxQueue, &queued, 0U) == pdPASS))
    {
        target.id = queued.ext_id;
        target.extended = s_policy != NULL &&
            !s_policy->can.standard_11_bit_ids;
        target.dlc = queued.dlc;
        (void)memcpy(target.data, queued.data, sizeof(target.data));
        result = BSP_CAN_TryTransmit(&target);
        if (result == BSP_CAN_TX_ACCEPTED)
        {
            BMS_Can_RecordDiagnostic(BMS_CAN_DIAG_TARGET_TX);
        }
        else if (result == BSP_CAN_TX_NO_MAILBOX)
        {
            if (xQueueSendToFront(xCanTxQueue, &queued, 0U) != pdPASS)
            {
                BMS_Can_RecordDiagnostic(BMS_CAN_DIAG_TARGET_TX_DROP);
            }
            break;
        }
        else
        {
            BMS_Can_RecordDiagnostic(BMS_CAN_DIAG_TARGET_TX_DROP);
        }
    }
}
