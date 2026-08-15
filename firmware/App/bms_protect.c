#include "bms_protect.h"

#include <stddef.h>

#include "bms_protect.h"
#include "bsp_exti.h"
#include "bq76940_measurement.h"
#include "bq76940_regs.h"
#include "stm32f10x_exti.h"

/* ------------------------------------------------------------------ */
/* Module state.                                                       */
/* ------------------------------------------------------------------ */
static BMS_FaultSummary_t s_fault;
static bool s_xready_recovery_pending;
static BQ76940_t *s_afe_device;

BQ76940_FetRequest_t g_bms_fet_request;

void BMS_Protect_Init(void)
{
    BMS_Fault_Init(&s_fault);
    s_xready_recovery_pending = false;
    s_afe_device = NULL;
    g_bms_fet_request.chg = BQ76940_FET_DESIRE_DISABLE;
    g_bms_fet_request.dsg = BQ76940_FET_DESIRE_DISABLE;
}

void BMS_Protect_SetDevice(BQ76940_t *device)
{
    s_afe_device = device;
}

BMS_FaultSummary_t BMS_Protect_GetFaultSummary(void)
{
    return s_fault;
}

/* ------------------------------------------------------------------ */
/* CC queue (H-02: newest sample always wins).                         */
/* ------------------------------------------------------------------ */
bool BMS_Protect_PushCcSample(int16_t cc_raw)
{
    BMS_CcSample_t sample;
    BMS_CcSample_t discard;
    BaseType_t ok;

    sample.raw = cc_raw;
    sample.tick = xTaskGetTickCount();

    if (xQueueSend(xCcSampleQueue, &sample, 0U) == pdPASS)
    {
        return true;
    }

    /* Queue full: drop exactly one oldest sample and retry with the
     * newest (spec/H-02: keep newest, drop oldest). */
    ok = xQueueReceive(xCcSampleQueue, &discard, 0U);
    if (ok != pdPASS)
    {
        return false;
    }
    (void)discard;
    return (xQueueSend(xCcSampleQueue, &sample, 0U) == pdPASS);
}

/* ------------------------------------------------------------------ */
/* XREADY recovery (H-03).                                             */
/* ------------------------------------------------------------------ */
bool BMS_Protect_RecoverXready(BQ76940_t *device)
{
    BQ76940_Calibration_t calibration;

    if (device == NULL)
    {
        return false;
    }

    /* 1. Re-read calibration (transport verified). */
    if (BQ76940_ReadCalibration(device, &calibration) !=
        BQ76940_STATUS_OK)
    {
        return false;
    }
    if (!calibration.valid)
    {
        return false;
    }

    /*
     * 2. Re-apply reference protection configuration (Phase 5 control
     * primitives). Phase 9 supplies the authoritative config table; here
     * we apply the reference defaults so the recovery chain is real and
     * testable. Writes are transactional at the driver level.
     */
    {
        uint8_t ov_trip;
        uint8_t uv_trip;
        uint8_t p1;
        uint8_t p2;
        uint8_t p3;

        if (BQ76940_Control_EncodeOvTrip(4250U, &calibration, &ov_trip) !=
            BQ76940_STATUS_OK)
        {
            return false;
        }
        if (BQ76940_Control_EncodeUvTrip(2800U, &calibration, &uv_trip) !=
            BQ76940_STATUS_OK)
        {
            return false;
        }
        /* Reference: SCD 111 mV / 100 us (code 4/1), OCD 56 mV / 80 ms
         * (code 7/3), UV delay 4 s (1), OV delay 2 s (1). */
        p1 = BQ76940_Control_ComposeProtect1(true, 1U, 4U);
        p2 = BQ76940_Control_ComposeProtect2(3U, 7U);
        p3 = BQ76940_Control_ComposeProtect3(1U, 1U);

        if (BQ76940_WriteByte(device, BQ76940_REG_OV_TRIP, ov_trip) !=
            BQ76940_STATUS_OK)
        {
            return false;
        }
        if (BQ76940_WriteByte(device, BQ76940_REG_UV_TRIP, uv_trip) !=
            BQ76940_STATUS_OK)
        {
            return false;
        }
        if (BQ76940_WriteByte(device, BQ76940_REG_PROTECT1, p1) !=
            BQ76940_STATUS_OK)
        {
            return false;
        }
        if (BQ76940_WriteByte(device, BQ76940_REG_PROTECT2, p2) !=
            BQ76940_STATUS_OK)
        {
            return false;
        }
        if (BQ76940_WriteByte(device, BQ76940_REG_PROTECT3, p3) !=
            BQ76940_STATUS_OK)
        {
            return false;
        }
    }

    /* 3. Success: clear the latched fault and the SYS_STAT XREADY bit
     * (H-03: XREADY cleared LAST, after full recovery). */
    if (BQ76940_WriteByte(device, BQ76940_REG_SYS_STAT,
                          BMS_PROTECT_STAT_DEVICE_XREADY) !=
        BQ76940_STATUS_OK)
    {
        /* The clear itself failed: keep the fault pending. */
        return false;
    }
    s_fault.active &= ~(BMS_Fault_Mask(BMS_FAULT_ID_AFE_XREADY));
    s_fault.latched &= ~(BMS_Fault_Mask(BMS_FAULT_ID_AFE_XREADY));
    s_xready_recovery_pending = false;
    return true;
}

/* ------------------------------------------------------------------ */
/* SYS_STAT drain (H-05) + per-bit handling (H-01/H-02/H-03).          */
/* ------------------------------------------------------------------ */
static void BMS_Protect_HandleCcReady(BQ76940_t *device,
                                      uint8_t *clear_mask)
{
    int16_t cc_raw;

    /* H-02: only clear CC_READY when the newest sample entered the queue. */
    if (BQ76940_ReadCcRaw(device, &cc_raw) == BQ76940_STATUS_OK)
    {
        if (BMS_Protect_PushCcSample(cc_raw))
        {
            *clear_mask |= BMS_PROTECT_STAT_CC_READY;
        }
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

void BMS_Protect_Drain(BQ76940_t *device)
{
    uint8_t stat;
    uint8_t clear_mask;
    uint8_t iteration;
    BQ76940_Status_t status;

    for (iteration = 0U; iteration < BMS_PROTECT_DRAIN_MAX_ITER; ++iteration)
    {
        stat = 0U;
        status = BQ76940_ReadByte(device, BQ76940_REG_SYS_STAT, &stat);
        if (status != BQ76940_STATUS_OK)
        {
            /* I2C/CRC failure: keep pending (H-05); AFE comm fault. */
            s_fault.active |= BMS_Fault_Mask(BMS_FAULT_ID_AFE_COMM);
            return;
        }
        s_fault.active &= ~(BMS_Fault_Mask(BMS_FAULT_ID_AFE_COMM));

        if (stat == 0U)
        {
            /* SYS_STAT drained; done. */
            return;
        }

        clear_mask = 0U;
        if ((stat & BMS_PROTECT_STAT_CC_READY) != 0U)
        {
            BMS_Protect_HandleCcReady(device, &clear_mask);
        }
        BMS_Protect_Decide(stat, &s_fault, &g_bms_fet_request, &clear_mask);
        if ((stat & BMS_PROTECT_STAT_DEVICE_XREADY) != 0U)
        {
            s_xready_recovery_pending = true;
        }

        /* XREADY recovery (H-03): once latched, run the recovery chain;
         * on success the fault is cleared, on failure it stays pending. */
        if (s_xready_recovery_pending && (s_afe_device != NULL))
        {
            if (BMS_Protect_RecoverXready(s_afe_device))
            {
                /* Recovery succeeded; the fault is now cleared. */
            }
        }

        if (clear_mask != 0U)
        {
            status = BQ76940_WriteByte(device, BQ76940_REG_SYS_STAT,
                                       clear_mask);
            if (status != BQ76940_STATUS_OK)
            {
                /* Clear failed: keep pending; try again next iteration. */
                return;
            }
        }
        /* Loop to re-read: new events may have arrived (H-05 drain). */
    }
    /* Budget exhausted with bits still pending: remain pending. */
}

void Task_Protect(void *argument)
{
    (void)argument;

    for (;;)
    {
        (void)xSemaphoreTake(xAfeAlertSem, portMAX_DELAY);

        if (xSemaphoreTake(xI2CMutex,
                           pdMS_TO_TICKS(BMS_PROTECT_I2C_TIMEOUT_MS)) != pdTRUE)
        {
            /* Mutex timeout: keep pending (H-05), retry on next wake. */
            continue;
        }

        if (s_afe_device != NULL)
        {
            BMS_Protect_Drain(s_afe_device);
        }

        (void)xSemaphoreGive(xI2CMutex);
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
        (void)xSemaphoreGiveFromISR(xAfeAlertSem,
                                    &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}
