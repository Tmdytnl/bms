#include "bms_protect.h"

#include <stddef.h>

#include "bsp_exti.h"
#include "bq76940_measurement.h"
#include "bq76940_regs.h"
#include "stm32f10x_exti.h"

/* ------------------------------------------------------------------ */
/* Module state.                                                       */
/* ------------------------------------------------------------------ */
static BMS_FaultSummary_t s_fault;
static BMS_ProtectDiagnostics_t s_diagnostics;
static bool s_xready_recovery_pending;
static bool s_cc_clear_pending;
static BQ76940_t *s_afe_device;
static BMS_ProtectXreadyRecoveryHook_t s_xready_recovery_hook;

BQ76940_FetRequest_t g_bms_fet_request;

void BMS_Protect_Init(void)
{
    BMS_Fault_Init(&s_fault);
    s_diagnostics.cc_queue_overflow_count = 0UL;
    s_diagnostics.cc_sample_missed_count = 0UL;
    s_diagnostics.cc_enqueue_failure_count = 0UL;
    s_diagnostics.cc_queue_overflow_latched = false;
    s_xready_recovery_pending = false;
    s_cc_clear_pending = false;
    s_afe_device = NULL;
    s_xready_recovery_hook = NULL;
    g_bms_fet_request.chg = BQ76940_FET_DESIRE_DISABLE;
    g_bms_fet_request.dsg = BQ76940_FET_DESIRE_DISABLE;
}

void BMS_Protect_SetDevice(BQ76940_t *device)
{
    s_afe_device = device;
}

void BMS_Protect_SetXreadyRecoveryHook(
    BMS_ProtectXreadyRecoveryHook_t recovery_hook)
{
    s_xready_recovery_hook = recovery_hook;
}

BMS_FaultSummary_t BMS_Protect_GetFaultSummary(void)
{
    return s_fault;
}

BMS_ProtectDiagnostics_t BMS_Protect_GetDiagnostics(void)
{
    BMS_ProtectDiagnostics_t snapshot;

    vTaskSuspendAll();
    snapshot = s_diagnostics;
    (void)xTaskResumeAll();
    return snapshot;
}

static void BMS_Protect_RecordCcOverflow(bool oldest_was_dropped,
                                         bool replacement_failed)
{
    if (s_diagnostics.cc_queue_overflow_count < UINT32_MAX)
    {
        ++s_diagnostics.cc_queue_overflow_count;
    }
    if (oldest_was_dropped &&
        (s_diagnostics.cc_sample_missed_count < UINT32_MAX))
    {
        ++s_diagnostics.cc_sample_missed_count;
    }
    if (replacement_failed &&
        (s_diagnostics.cc_enqueue_failure_count < UINT32_MAX))
    {
        ++s_diagnostics.cc_enqueue_failure_count;
    }
    s_diagnostics.cc_queue_overflow_latched = true;
}

static void BMS_Protect_RecordAfeFailure(BQ76940_Status_t status)
{
    if ((status == BQ76940_STATUS_CRC_MISMATCH) ||
        (status == BQ76940_STATUS_CRC_REJECTED))
    {
        s_fault.active |= BMS_Fault_Mask(BMS_FAULT_ID_AFE_CRC);
    }
    else if (status != BQ76940_STATUS_OK)
    {
        s_fault.active |= BMS_Fault_Mask(BMS_FAULT_ID_AFE_COMM);
    }
    if ((status != BQ76940_STATUS_OK) && (xSysEvents != NULL))
    {
        (void)xEventGroupSetBits(xSysEvents, EVT_FAULT_PRESENT);
    }
}

/* ------------------------------------------------------------------ */
/* CC queue (H-02: newest sample always wins).                         */
/* ------------------------------------------------------------------ */
bool BMS_Protect_PushCcSample(int16_t cc_raw)
{
    BMS_CcSample_t sample;
    BMS_CcSample_t discard;
    BaseType_t inserted;
    bool overflowed;
    bool oldest_was_dropped;
    bool newest_was_missed;

    sample.raw = cc_raw;
    sample.tick = xTaskGetTickCount();

    if (xCcSampleQueue == NULL)
    {
        return false;
    }

    overflowed = false;
    oldest_was_dropped = false;
    newest_was_missed = false;
    inserted = pdFAIL;

    /* The full-check, single-oldest discard, and newest enqueue are one
     * scheduler-protected nonblocking operation. This prevents a concurrent
     * task consumer from making the wrapper discard two samples. */
    vTaskSuspendAll();
    if (xQueueSend(xCcSampleQueue, &sample, 0U) == pdPASS)
    {
        inserted = pdPASS;
    }
    else
    {
        overflowed = true;
        if (xQueueReceive(xCcSampleQueue, &discard, 0U) == pdPASS)
        {
            (void)discard;
            oldest_was_dropped = true;
            inserted = xQueueSend(xCcSampleQueue, &sample, 0U);
        }
        if (inserted != pdPASS)
        {
            newest_was_missed = true;
        }
    }
    if (overflowed)
    {
        /* Publish the multi-field diagnostic snapshot before resuming another
         * task, so a task-level reader cannot observe half an increment. */
        BMS_Protect_RecordCcOverflow(oldest_was_dropped,
                                     newest_was_missed);
    }
    (void)xTaskResumeAll();

    if (overflowed)
    {
        if (xSysEvents != NULL)
        {
            (void)xEventGroupSetBits(xSysEvents, EVT_CC_QUEUE_OVERFLOW);
        }
    }

    return (inserted == pdPASS);
}

/* ------------------------------------------------------------------ */
/* XREADY recovery (H-03).                                             */
/* ------------------------------------------------------------------ */
bool BMS_Protect_RecoverXready(BQ76940_t *device)
{
    BQ76940_Status_t status;

    if ((device == NULL) || (s_xready_recovery_hook == NULL))
    {
        return false;
    }
    if (!s_xready_recovery_hook(device))
    {
        return false;
    }

    /* XREADY is W1C only after the authoritative hook confirms the complete
     * recovery contract. The history latch intentionally remains set. */
    status = BQ76940_WriteByte(device, BQ76940_REG_SYS_STAT,
                               BMS_PROTECT_STAT_DEVICE_XREADY);
    if ((status != BQ76940_STATUS_OK) &&
        (status != BQ76940_STATUS_WRITE_ACCEPTED_STOP_ERROR))
    {
        /* The clear was rejected: keep the fault pending. */
        BMS_Protect_RecordAfeFailure(status);
        return false;
    }
    s_fault.active &= ~(BMS_Fault_Mask(BMS_FAULT_ID_AFE_XREADY));
    s_xready_recovery_pending = false;
    if (status == BQ76940_STATUS_WRITE_ACCEPTED_STOP_ERROR)
    {
        /* W1C was accepted, but the bus still requires recovery/retry. */
        BMS_Protect_RecordAfeFailure(status);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* SYS_STAT drain (H-05) + per-bit handling (H-01/H-02/H-03).          */
/* ------------------------------------------------------------------ */
static void BMS_Protect_HandleCcReady(BQ76940_t *device,
                                      uint8_t *clear_mask)
{
    int16_t cc_raw;
    BQ76940_Status_t status;

    /* A previous sample was already committed to the queue but its W1C
     * write failed. Retry only the clear so one hardware sample cannot be
     * enqueued repeatedly and later integrated more than once. */
    if (s_cc_clear_pending)
    {
        *clear_mask |= BMS_PROTECT_STAT_CC_READY;
        return;
    }

    /* H-02: only clear CC_READY when the newest sample entered the queue. */
    status = BQ76940_ReadCcRaw(device, &cc_raw);
    if (status == BQ76940_STATUS_OK)
    {
        if (BMS_Protect_PushCcSample(cc_raw))
        {
            s_cc_clear_pending = true;
            *clear_mask |= BMS_PROTECT_STAT_CC_READY;
        }
    }
    else
    {
        BMS_Protect_RecordAfeFailure(status);
    }
}

void BMS_Protect_Decide(uint8_t stat,
                        BMS_FaultSummary_t *faults,
                        BQ76940_FetRequest_t *request,
                        uint8_t *clear_mask)
{
    if ((faults == NULL) || (request == NULL) || (clear_mask == NULL))
    {
        return;
    }

    *clear_mask = 0U;

    /* OV: active fault + inhibit CHG. */
    if ((stat & BMS_PROTECT_STAT_OV) != 0U)
    {
        faults->active |= BMS_Fault_Mask(BMS_FAULT_ID_HW_OV);
        request->chg = BQ76940_FET_DESIRE_DISABLE;
        *clear_mask |= BMS_PROTECT_STAT_OV;
    }

    /* UV: active fault + inhibit DSG. */
    if ((stat & BMS_PROTECT_STAT_UV) != 0U)
    {
        faults->active |= BMS_Fault_Mask(BMS_FAULT_ID_HW_UV);
        request->dsg = BQ76940_FET_DESIRE_DISABLE;
        *clear_mask |= BMS_PROTECT_STAT_UV;
    }

    /* OCD: active fault + inhibit DSG. */
    if ((stat & BMS_PROTECT_STAT_OCD) != 0U)
    {
        faults->active |= BMS_Fault_Mask(BMS_FAULT_ID_HW_OCD);
        request->dsg = BQ76940_FET_DESIRE_DISABLE;
        *clear_mask |= BMS_PROTECT_STAT_OCD;
    }

    /* SCD: active + latched fault + inhibit both. */
    if ((stat & BMS_PROTECT_STAT_SCD) != 0U)
    {
        faults->active |= BMS_Fault_Mask(BMS_FAULT_ID_HW_SCD);
        faults->latched |= BMS_Fault_Mask(BMS_FAULT_ID_HW_SCD);
        request->chg = BQ76940_FET_DESIRE_DISABLE;
        request->dsg = BQ76940_FET_DESIRE_DISABLE;
        *clear_mask |= BMS_PROTECT_STAT_SCD;
    }

    /* OVRD_ALERT (H-01): independent handling, inhibit both. */
    if ((stat & BMS_PROTECT_STAT_OVRD_ALERT) != 0U)
    {
        faults->active |= BMS_Fault_Mask(BMS_FAULT_ID_AFE_OVRD_ALERT);
        faults->latched |= BMS_Fault_Mask(BMS_FAULT_ID_AFE_OVRD_ALERT);
        request->chg = BQ76940_FET_DESIRE_DISABLE;
        request->dsg = BQ76940_FET_DESIRE_DISABLE;
        *clear_mask |= BMS_PROTECT_STAT_OVRD_ALERT;
    }

    /* XREADY (H-03): latched fault + both off; recovery via
     * BMS_Protect_RecoverXready; never cleared here. */
    if ((stat & BMS_PROTECT_STAT_DEVICE_XREADY) != 0U)
    {
        faults->active |= BMS_Fault_Mask(BMS_FAULT_ID_AFE_XREADY);
        faults->latched |= BMS_Fault_Mask(BMS_FAULT_ID_AFE_XREADY);
        request->chg = BQ76940_FET_DESIRE_DISABLE;
        request->dsg = BQ76940_FET_DESIRE_DISABLE;
        /* XREADY is NOT added to the clear mask here (H-03). */
    }
}

bool BMS_Protect_HasFaultBits(uint8_t stat)
{
    return ((stat & (BMS_PROTECT_STAT_OV | BMS_PROTECT_STAT_UV |
                     BMS_PROTECT_STAT_SCD | BMS_PROTECT_STAT_OCD |
                     BMS_PROTECT_STAT_DEVICE_XREADY |
                     BMS_PROTECT_STAT_OVRD_ALERT)) != 0U);
}

BMS_ProtectDrainResult_t BMS_Protect_Drain(BQ76940_t *device)
{
    uint8_t stat;
    uint8_t clear_mask;
    uint8_t iteration;
    BQ76940_Status_t status;

    if (device == NULL)
    {
        return BMS_PROTECT_DRAIN_RETRY_REQUIRED;
    }

    for (iteration = 0U; iteration < BMS_PROTECT_DRAIN_MAX_ITER; ++iteration)
    {
        stat = 0U;
        status = BQ76940_ReadByte(device, BQ76940_REG_SYS_STAT, &stat);
        if (status != BQ76940_STATUS_OK)
        {
            /* I2C/CRC failure: keep pending (H-05); AFE comm fault. */
            BMS_Protect_RecordAfeFailure(status);
            return BMS_PROTECT_DRAIN_RETRY_REQUIRED;
        }
        s_fault.active &= ~(BMS_Fault_Mask(BMS_FAULT_ID_AFE_COMM) |
                            BMS_Fault_Mask(BMS_FAULT_ID_AFE_CRC));

        /* A definitely rejected W1C can later be rendered moot by a reset or
         * another authorized clear. A successful low read retires that stale
         * retry marker. Accepted-write/STOP failures are handled explicitly at
         * the write site and never leave this marker set. */
        if ((stat & BMS_PROTECT_STAT_CC_READY) == 0U)
        {
            s_cc_clear_pending = false;
        }

        if (stat == 0U)
        {
            if (s_xready_recovery_pending)
            {
                if (!BMS_Protect_RecoverXready(device))
                {
                    return BMS_PROTECT_DRAIN_RETRY_REQUIRED;
                }
                continue;
            }
            return BMS_PROTECT_DRAIN_COMPLETE;
        }

        clear_mask = 0U;
        BMS_Protect_Decide(stat, &s_fault, &g_bms_fet_request, &clear_mask);
        if (BMS_Protect_HasFaultBits(stat) && (xSysEvents != NULL))
        {
            (void)xEventGroupSetBits(xSysEvents, EVT_FAULT_PRESENT);
        }
        if ((stat & BMS_PROTECT_STAT_CC_READY) != 0U)
        {
            BMS_Protect_HandleCcReady(device, &clear_mask);
        }
        if ((stat & BMS_PROTECT_STAT_DEVICE_XREADY) != 0U)
        {
            s_xready_recovery_pending = true;
        }

        if (clear_mask != 0U)
        {
            status = BQ76940_WriteByte(device, BQ76940_REG_SYS_STAT,
                                       clear_mask);
            if ((status != BQ76940_STATUS_OK) &&
                (status != BQ76940_STATUS_WRITE_ACCEPTED_STOP_ERROR))
            {
                /* Clear was rejected: keep pending and retry it. */
                BMS_Protect_RecordAfeFailure(status);
                return BMS_PROTECT_DRAIN_RETRY_REQUIRED;
            }
            if ((clear_mask & BMS_PROTECT_STAT_CC_READY) != 0U)
            {
                s_cc_clear_pending = false;
            }
            if (status == BQ76940_STATUS_WRITE_ACCEPTED_STOP_ERROR)
            {
                /* Payload+CRC were ACKed, so do not replay this W1C against a
                 * possibly newer event. Release the bus and retry service. */
                BMS_Protect_RecordAfeFailure(status);
                return BMS_PROTECT_DRAIN_RETRY_REQUIRED;
            }
        }

        /* Clear XREADY last, and only after the externally supplied complete
         * recovery contract succeeds. A failed or absent hook releases the
         * mutex promptly and retains pending state for a delayed retry. */
        if (s_xready_recovery_pending)
        {
            if (!BMS_Protect_RecoverXready(device))
            {
                return BMS_PROTECT_DRAIN_RETRY_REQUIRED;
            }
        }
        /* Loop to re-read: new events may have arrived (H-05 drain). */
    }
    return BMS_PROTECT_DRAIN_RETRY_REQUIRED;
}

BMS_ProtectServiceResult_t BMS_Protect_ServicePending(BQ76940_t *device)
{
    BMS_ProtectDrainResult_t drain_result;

    if ((device == NULL) || (xI2CMutex == NULL))
    {
        return BMS_PROTECT_SERVICE_RETRY_REQUIRED;
    }
    if (xSemaphoreTake(xI2CMutex,
                       pdMS_TO_TICKS(BMS_PROTECT_I2C_TIMEOUT_MS)) != pdTRUE)
    {
        return BMS_PROTECT_SERVICE_RETRY_REQUIRED;
    }

    drain_result = BMS_Protect_Drain(device);
    (void)xSemaphoreGive(xI2CMutex);

    if ((drain_result != BMS_PROTECT_DRAIN_COMPLETE) ||
        BSP_ALERT_PinActive())
    {
        return BMS_PROTECT_SERVICE_RETRY_REQUIRED;
    }
    return BMS_PROTECT_SERVICE_IDLE;
}

void Task_Protect(void *argument)
{
    bool retry_pending;

    (void)argument;

    /* FreeRTOS initializes its Cortex-M ISR-priority validator inside
     * xPortStartScheduler. Enabling EXTI before that point would let a real
     * edge enter xSemaphoreGiveFromISR with uninitialized port state. This
     * highest-priority task therefore owns EXTI activation. */
    while (!BSP_ALERT_EXTI_Init())
    {
        vTaskDelay(pdMS_TO_TICKS(BMS_PROTECT_RETRY_DELAY_MS));
    }

    /* Rising-edge EXTI cannot report a level that was already high while the
     * line was disabled. Seed task-level work directly from PB1 after enable. */
    retry_pending = BSP_ALERT_PinActive();

    for (;;)
    {
        if (!retry_pending)
        {
            if (xSemaphoreTake(xAfeAlertSem, portMAX_DELAY) != pdTRUE)
            {
                continue;
            }
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(BMS_PROTECT_RETRY_DELAY_MS));
        }

        retry_pending =
            (BMS_Protect_ServicePending(s_afe_device) ==
             BMS_PROTECT_SERVICE_RETRY_REQUIRED);
    }
}

/* ------------------------------------------------------------------ */
/* EXTI1 ISR (spec §20.1, H-05).                                      */
/* ------------------------------------------------------------------ */
void EXTI1_IRQHandler(void)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    if (EXTI_GetITStatus(EXTI_Line1) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line1);
        if (xAfeAlertSem != NULL)
        {
            (void)xSemaphoreGiveFromISR(xAfeAlertSem,
                                        &higher_priority_task_woken);
        }
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}
