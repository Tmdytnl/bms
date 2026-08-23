#include "bms_protect.h"

#include <stddef.h>

#include "bsp_exti.h"
#include "bms_data.h"
#include "bms_health.h"
#include "bq76940_measurement.h"
#include "bq76940_regs.h"
#include "stm32f10x_exti.h"

/* ------------------------------------------------------------------ */
/* Module state.                                                       */
/* ------------------------------------------------------------------ */
static BMS_FaultSummary_t s_fault;
static BMS_ProtectDiagnostics_t s_diagnostics;
static BMS_ProtectLatestCc_t s_latest_cc;
static BMS_ProtectXreadyState_t s_xready_state;
static bool s_xready_recovery_pending;
static bool s_cc_clear_pending;
static uint8_t s_w1c_finalization_ambiguous_mask;
static BQ76940_t *s_afe_device;
static BMS_ProtectXreadyRecoveryHook_t s_xready_recovery_hook;
static uint32_t s_publication_revision;
static uint32_t s_source_generation[BMS_PROTECT_SOURCE_COUNT];
static uint8_t s_last_sys_stat;
static BMS_ProtectXreadyClearAuthorization_t s_xready_clear_authorization;
static BMS_ProtectXreadyClearAck_t s_xready_clear_ack;
static const BMS_Policy_t *s_policy;
static BMS_ProtectHwRecoveryRequest_t s_hw_recovery_request;
static BMS_ProtectHwRecoveryAck_t s_hw_recovery_ack;
static BMS_ServiceResetRequest_t s_service_reset_request;
static BMS_ServiceResetAck_t s_service_reset_ack;
static uint8_t s_afe_consecutive_failures;
static uint8_t s_afe_consecutive_successes;
static uint32_t s_afe_last_success_ms;
static uint32_t s_afe_comm_active_started_ms;
static bool s_afe_comm_active_timing;
static uint8_t s_ocd_event_count;
static uint32_t s_ocd_window_started_ms;
static bool s_ocd_window_active;

BQ76940_FetRequest_t g_bms_fet_request;

static void BMS_Protect_AdvanceRevision(void)
{
    s_publication_revision =
        (uint32_t)(s_publication_revision + 1UL);
}

static void BMS_Protect_RecordNewSourceEvents(uint8_t stat)
{
    uint8_t new_events;
    uint32_t now_ms;
    bool changed;

    new_events = (uint8_t)(stat & (uint8_t)(~s_last_sys_stat));
    changed = false;
    if ((new_events & BMS_PROTECT_STAT_OV) != 0U)
    {
        s_source_generation[BMS_PROTECT_SOURCE_HW_OV] =
            (uint32_t)(s_source_generation[BMS_PROTECT_SOURCE_HW_OV] + 1UL);
        changed = true;
        if (s_hw_recovery_ack.fault_id == BMS_FAULT_ID_HW_OV)
        {
            s_hw_recovery_ack.accepted = false;
        }
    }
    if ((new_events & BMS_PROTECT_STAT_UV) != 0U)
    {
        s_source_generation[BMS_PROTECT_SOURCE_HW_UV] =
            (uint32_t)(s_source_generation[BMS_PROTECT_SOURCE_HW_UV] + 1UL);
        changed = true;
        if (s_hw_recovery_ack.fault_id == BMS_FAULT_ID_HW_UV)
        {
            s_hw_recovery_ack.accepted = false;
        }
    }
    if ((new_events & BMS_PROTECT_STAT_OCD) != 0U)
    {
        s_source_generation[BMS_PROTECT_SOURCE_HW_OCD] =
            (uint32_t)(s_source_generation[BMS_PROTECT_SOURCE_HW_OCD] + 1UL);
        changed = true;
        if (s_hw_recovery_ack.fault_id == BMS_FAULT_ID_HW_OCD)
        {
            s_hw_recovery_ack.accepted = false;
        }
        if (s_policy != NULL)
        {
            now_ms = (uint32_t)(xTaskGetTickCount() *
                                portTICK_PERIOD_MS);
            if (!s_ocd_window_active ||
                ((uint32_t)(now_ms - s_ocd_window_started_ms) >
                 s_policy->ocd_escalation.event_window_ms))
            {
                s_ocd_window_started_ms = now_ms;
                s_ocd_event_count = 1U;
                s_ocd_window_active = true;
            }
            else if (s_ocd_event_count < UINT8_MAX)
            {
                ++s_ocd_event_count;
            }
            if (s_ocd_event_count >=
                s_policy->ocd_escalation.event_count_to_latch)
            {
                s_fault.latched |=
                    BMS_Fault_Mask(BMS_FAULT_ID_HW_OCD);
            }
        }
    }
    if ((new_events & BMS_PROTECT_STAT_SCD) != 0U)
    {
        s_source_generation[BMS_PROTECT_SOURCE_HW_SCD] =
            (uint32_t)(s_source_generation[BMS_PROTECT_SOURCE_HW_SCD] + 1UL);
        changed = true;
    }
    s_last_sys_stat = stat;
    if (changed)
    {
        BMS_Protect_AdvanceRevision();
    }
}

void BMS_Protect_Init(void)
{
    uint8_t source_index;

    BMS_Fault_Init(&s_fault);
    s_diagnostics.cc_queue_overflow_count = 0UL;
    s_diagnostics.cc_sample_missed_count = 0UL;
    s_diagnostics.cc_enqueue_failure_count = 0UL;
    s_diagnostics.w1c_finalization_ambiguous_count = 0UL;
    s_diagnostics.cc_event_identity_ambiguous_count = 0UL;
    s_diagnostics.w1c_finalization_ambiguous_mask = 0U;
    s_diagnostics.cc_queue_overflow_latched = false;
    s_diagnostics.w1c_finalization_ambiguous_latched = false;
    s_latest_cc.raw = (int16_t)0;
    s_latest_cc.tick = (TickType_t)0U;
    s_latest_cc.sequence = 0UL;
    s_latest_cc.xready_generation = 0UL;
    s_latest_cc.valid = false;
    s_xready_state.xready_generation = 0UL;
    s_xready_state.active = false;
    s_xready_recovery_pending = false;
    s_cc_clear_pending = false;
    s_w1c_finalization_ambiguous_mask = 0U;
    s_afe_device = NULL;
    s_xready_recovery_hook = NULL;
    s_publication_revision = 0UL;
    for (source_index = 0U;
         source_index < (uint8_t)BMS_PROTECT_SOURCE_COUNT;
         ++source_index)
    {
        s_source_generation[source_index] = 0UL;
    }
    s_last_sys_stat = 0U;
    s_xready_clear_authorization.xready_generation = 0UL;
    s_xready_clear_authorization.recovery_revision = 0UL;
    s_xready_clear_authorization.valid = false;
    s_xready_clear_ack.xready_generation = 0UL;
    s_xready_clear_ack.recovery_revision = 0UL;
    s_xready_clear_ack.protect_revision = 0UL;
    s_xready_clear_ack.accepted = false;
    s_xready_clear_ack.finalization_ambiguous = false;
    s_policy = NULL;
    s_hw_recovery_request.valid = false;
    s_hw_recovery_ack.fault_id = BMS_FAULT_ID_HW_OV;
    s_hw_recovery_ack.request_id = 0UL;
    s_hw_recovery_ack.source_generation = 0UL;
    s_hw_recovery_ack.qualification_revision = 0UL;
    s_hw_recovery_ack.protect_revision = 0UL;
    s_hw_recovery_ack.accepted = false;
    s_service_reset_request.valid = false;
    s_service_reset_ack.source = BMS_SERVICE_RESET_HW_SCD;
    s_service_reset_ack.request_id = 0UL;
    s_service_reset_ack.protect_revision = 0UL;
    s_service_reset_ack.accepted = false;
    s_afe_consecutive_failures = 0U;
    s_afe_consecutive_successes = 0U;
    s_afe_last_success_ms = 0UL;
    s_afe_comm_active_started_ms = 0UL;
    s_afe_comm_active_timing = false;
    s_ocd_event_count = 0U;
    s_ocd_window_started_ms = 0UL;
    s_ocd_window_active = false;
    g_bms_fet_request.chg = BQ76940_FET_DESIRE_DISABLE;
    g_bms_fet_request.dsg = BQ76940_FET_DESIRE_DISABLE;
}

void BMS_Protect_SetDevice(BQ76940_t *device)
{
    s_afe_device = device;
}

void BMS_Protect_SetPolicy(const BMS_Policy_t *policy)
{
    s_policy = BMS_Policy_Validate(policy) ? policy : NULL;
    s_afe_last_success_ms =
        (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void BMS_Protect_SetXreadyRecoveryHook(
    BMS_ProtectXreadyRecoveryHook_t recovery_hook)
{
    s_xready_recovery_hook = recovery_hook;
}

BMS_FaultSummary_t BMS_Protect_GetFaultSummary(void)
{
    BMS_FaultSummary_t snapshot;

    vTaskSuspendAll();
    snapshot = s_fault;
    (void)xTaskResumeAll();
    return snapshot;
}

static void BMS_Protect_AddBothInhibit(
    BMS_ProtectSafetySnapshot_t *snapshot,
    BMS_FaultId_t fault_id)
{
    BMS_InhibitReasonBitmap_t reason;

    reason = BMS_INHIBIT_REASON_FAULT(fault_id);
    snapshot->inhibit_chg_reasons |= reason;
    snapshot->inhibit_dsg_reasons |= reason;
}

BMS_ProtectSafetySnapshot_t BMS_Protect_GetSafetySnapshot(void)
{
    BMS_ProtectSafetySnapshot_t snapshot;
    BMS_FaultBitmap_t action_sources;
    BMS_FaultBitmap_t mapped_sources;
    uint8_t source_index;

    vTaskSuspendAll();
    snapshot.faults = s_fault;
    snapshot.publication_revision = s_publication_revision;
    for (source_index = 0U;
         source_index < (uint8_t)BMS_PROTECT_SOURCE_COUNT;
         ++source_index)
    {
        snapshot.source_generation[source_index] =
            s_source_generation[source_index];
    }
    snapshot.xready_generation = s_xready_state.xready_generation;
    snapshot.xready_active = s_xready_state.active;
    (void)xTaskResumeAll();

    snapshot.inhibit_chg_reasons = 0UL;
    snapshot.inhibit_dsg_reasons = 0UL;
    action_sources = snapshot.faults.active |
        (snapshot.faults.latched &
         (BMS_Fault_Mask(BMS_FAULT_ID_HW_SCD) |
          BMS_Fault_Mask(BMS_FAULT_ID_AFE_XREADY) |
          BMS_Fault_Mask(BMS_FAULT_ID_AFE_OVRD_ALERT) |
          BMS_Fault_Mask(BMS_FAULT_ID_AFE_COMM) |
          BMS_Fault_Mask(BMS_FAULT_ID_HW_OCD)));
    mapped_sources = 0UL;

    if (BMS_Fault_Contains(action_sources, BMS_FAULT_ID_HW_OV))
    {
        snapshot.inhibit_chg_reasons |=
            BMS_INHIBIT_REASON_FAULT(BMS_FAULT_ID_HW_OV);
        mapped_sources |= BMS_Fault_Mask(BMS_FAULT_ID_HW_OV);
    }
    if (BMS_Fault_Contains(action_sources, BMS_FAULT_ID_HW_UV))
    {
        snapshot.inhibit_dsg_reasons |=
            BMS_INHIBIT_REASON_FAULT(BMS_FAULT_ID_HW_UV);
        mapped_sources |= BMS_Fault_Mask(BMS_FAULT_ID_HW_UV);
    }
    if (BMS_Fault_Contains(action_sources, BMS_FAULT_ID_HW_OCD))
    {
        snapshot.inhibit_dsg_reasons |=
            BMS_INHIBIT_REASON_FAULT(BMS_FAULT_ID_HW_OCD);
        mapped_sources |= BMS_Fault_Mask(BMS_FAULT_ID_HW_OCD);
    }
    if (BMS_Fault_Contains(action_sources, BMS_FAULT_ID_HW_SCD))
    {
        BMS_Protect_AddBothInhibit(&snapshot, BMS_FAULT_ID_HW_SCD);
        mapped_sources |= BMS_Fault_Mask(BMS_FAULT_ID_HW_SCD);
    }
    if (BMS_Fault_Contains(action_sources, BMS_FAULT_ID_AFE_XREADY))
    {
        BMS_Protect_AddBothInhibit(&snapshot, BMS_FAULT_ID_AFE_XREADY);
        mapped_sources |= BMS_Fault_Mask(BMS_FAULT_ID_AFE_XREADY);
    }
    if (BMS_Fault_Contains(action_sources, BMS_FAULT_ID_AFE_OVRD_ALERT))
    {
        BMS_Protect_AddBothInhibit(&snapshot,
                                   BMS_FAULT_ID_AFE_OVRD_ALERT);
        mapped_sources |= BMS_Fault_Mask(BMS_FAULT_ID_AFE_OVRD_ALERT);
    }
    if (BMS_Fault_Contains(action_sources, BMS_FAULT_ID_AFE_COMM))
    {
        BMS_Protect_AddBothInhibit(&snapshot, BMS_FAULT_ID_AFE_COMM);
        mapped_sources |= BMS_Fault_Mask(BMS_FAULT_ID_AFE_COMM);
    }
    if (BMS_Fault_Contains(action_sources, BMS_FAULT_ID_AFE_CRC))
    {
        BMS_Protect_AddBothInhibit(&snapshot, BMS_FAULT_ID_AFE_CRC);
        mapped_sources |= BMS_Fault_Mask(BMS_FAULT_ID_AFE_CRC);
    }
    mapped_sources |= BMS_Fault_Mask(BMS_FAULT_ID_AFE_STALE);
    if ((action_sources & ~mapped_sources) != 0UL)
    {
        snapshot.inhibit_chg_reasons |= BMS_INHIBIT_REASON_UNKNOWN_SOURCE;
        snapshot.inhibit_dsg_reasons |= BMS_INHIBIT_REASON_UNKNOWN_SOURCE;
    }
    return snapshot;
}

BMS_ProtectDiagnostics_t BMS_Protect_GetDiagnostics(void)
{
    BMS_ProtectDiagnostics_t snapshot;

    vTaskSuspendAll();
    snapshot = s_diagnostics;
    snapshot.w1c_finalization_ambiguous_mask =
        s_w1c_finalization_ambiguous_mask;
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
    BMS_FaultBitmap_t active_mask;
    BMS_FaultBitmap_t previous_active;

    active_mask = (BMS_FaultBitmap_t)0U;
    if ((status == BQ76940_STATUS_CRC_MISMATCH) ||
        (status == BQ76940_STATUS_CRC_REJECTED))
    {
        active_mask = BMS_Fault_Mask(BMS_FAULT_ID_AFE_CRC);
    }
    else if ((status != BQ76940_STATUS_OK) && (s_policy == NULL))
    {
        active_mask = BMS_Fault_Mask(BMS_FAULT_ID_AFE_COMM);
    }
    else if (status != BQ76940_STATUS_OK)
    {
        if (s_afe_consecutive_failures < UINT8_MAX)
        {
            ++s_afe_consecutive_failures;
        }
        if (s_afe_consecutive_failures >=
            s_policy->afe_comm.consecutive_failures_to_active)
        {
            active_mask = BMS_Fault_Mask(BMS_FAULT_ID_AFE_COMM);
        }
    }
    if (status != BQ76940_STATUS_OK)
    {
        s_afe_consecutive_successes = 0U;
    }
    if (active_mask != (BMS_FaultBitmap_t)0U)
    {
        vTaskSuspendAll();
        previous_active = s_fault.active;
        s_fault.active |= active_mask;
        if (previous_active != s_fault.active)
        {
            BMS_Protect_AdvanceRevision();
            if (BMS_Fault_Contains(active_mask,
                                   BMS_FAULT_ID_AFE_COMM))
            {
                s_afe_comm_active_started_ms =
                    (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
                s_afe_comm_active_timing = true;
            }
        }
        (void)xTaskResumeAll();
    }
    if ((status != BQ76940_STATUS_OK) && (xSysEvents != NULL))
    {
        (void)xEventGroupSetBits(xSysEvents, EVT_FAULT_PRESENT);
    }
}

static void BMS_Protect_RecordW1cFinalizationAmbiguity(uint8_t clear_mask)
{
    if (clear_mask == 0U)
    {
        return;
    }

    vTaskSuspendAll();
    if (s_diagnostics.w1c_finalization_ambiguous_count < UINT32_MAX)
    {
        ++s_diagnostics.w1c_finalization_ambiguous_count;
    }
    if (((clear_mask & BMS_PROTECT_STAT_CC_READY) != 0U) &&
        (s_diagnostics.cc_event_identity_ambiguous_count < UINT32_MAX))
    {
        ++s_diagnostics.cc_event_identity_ambiguous_count;
    }
    s_w1c_finalization_ambiguous_mask |= clear_mask;
    s_diagnostics.w1c_finalization_ambiguous_latched = true;
    (void)xTaskResumeAll();
}

static void BMS_Protect_ResolveObservedLowW1c(uint8_t stat)
{
    uint8_t resolved_mask;
    bool safety_changed;

    vTaskSuspendAll();
    safety_changed = false;
    resolved_mask = (uint8_t)(s_w1c_finalization_ambiguous_mask &
                              (uint8_t)(~stat));
    s_w1c_finalization_ambiguous_mask &= stat;
    if ((resolved_mask & BMS_PROTECT_STAT_CC_READY) != 0U)
    {
        /* The queued sample remains accepted. Observed-low is the only
         * software-safe point at which its quarantined W1C can retire. */
        s_cc_clear_pending = false;
    }
    if (((resolved_mask & BMS_PROTECT_STAT_DEVICE_XREADY) != 0U) &&
        s_xready_recovery_pending)
    {
        /* The full recovery hook already succeeded before the ambiguous W1C.
         * Observing XREADY low confirms that no W1C replay is needed. */
        s_fault.active &=
            ~(BMS_Fault_Mask(BMS_FAULT_ID_AFE_XREADY));
        s_xready_state.active = false;
        s_xready_recovery_pending = false;
        safety_changed = true;
    }
    if (safety_changed)
    {
        BMS_Protect_AdvanceRevision();
    }
    (void)xTaskResumeAll();
}

static void BMS_Protect_RecordAfeReadSuccess(void)
{
    BMS_FaultBitmap_t previous_active;

    vTaskSuspendAll();
    s_afe_last_success_ms =
        (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    s_afe_consecutive_failures = 0U;
    if (s_afe_consecutive_successes < UINT8_MAX)
    {
        ++s_afe_consecutive_successes;
    }
    previous_active = s_fault.active;
    if ((s_policy == NULL) ||
        (s_afe_consecutive_successes >=
         s_policy->afe_comm.consecutive_successes_to_recover))
    {
        s_fault.active &= ~(BMS_Fault_Mask(BMS_FAULT_ID_AFE_CRC));
        if (s_w1c_finalization_ambiguous_mask == 0U)
        {
            s_fault.active &= ~(BMS_Fault_Mask(BMS_FAULT_ID_AFE_COMM));
        }
    }
    if (previous_active != s_fault.active)
    {
        BMS_Protect_AdvanceRevision();
    }
    (void)xTaskResumeAll();
}

static void BMS_Protect_UpdateAfeCommPolicy(uint32_t now_ms)
{
    BMS_FaultBitmap_t comm_mask;
    BMS_FaultSummary_t previous;

    if (s_policy == NULL)
    {
        return;
    }
    comm_mask = BMS_Fault_Mask(BMS_FAULT_ID_AFE_COMM);
    vTaskSuspendAll();
    previous = s_fault;
    if ((uint32_t)(now_ms - s_afe_last_success_ms) >=
        s_policy->afe_comm.no_success_timeout_ms)
    {
        s_fault.active |= comm_mask;
        if (!s_afe_comm_active_timing)
        {
            s_afe_comm_active_started_ms = now_ms;
            s_afe_comm_active_timing = true;
        }
    }
    if (BMS_Fault_Contains(s_fault.active, BMS_FAULT_ID_AFE_COMM))
    {
        if (!s_afe_comm_active_timing)
        {
            s_afe_comm_active_started_ms = now_ms;
            s_afe_comm_active_timing = true;
        }
        if ((uint32_t)(now_ms - s_afe_comm_active_started_ms) >=
            s_policy->afe_comm.continuous_latch_ms)
        {
            s_fault.latched |= comm_mask;
        }
    }
    else
    {
        s_afe_comm_active_timing = false;
    }
    if ((previous.active != s_fault.active) ||
        (previous.latched != s_fault.latched))
    {
        BMS_Protect_AdvanceRevision();
    }
    (void)xTaskResumeAll();
}

#if defined(TEST_PHASE7_IMAGE) || defined(TEST_PHASE9_IMAGE)
void BMS_Protect_TestUpdateAfeCommPolicy(uint32_t now_ms)
{
    BMS_Protect_UpdateAfeCommPolicy(now_ms);
}
#endif

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
    if ((inserted == pdPASS) && !s_xready_state.active)
    {
        s_latest_cc.raw = sample.raw;
        s_latest_cc.tick = sample.tick;
        s_latest_cc.sequence =
            BMS_PROTECT_CC_SEQUENCE_NEXT(s_latest_cc.sequence);
        s_latest_cc.xready_generation =
            s_xready_state.xready_generation;
        s_latest_cc.valid = true;
    }
    else if ((inserted == pdPASS) && s_xready_state.active)
    {
        /* Preserve the queue/SOC contract, but never expose a CC value read
         * while the AFE reset epoch is active. */
        s_latest_cc.valid = false;
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

bool BMS_Protect_GetLatestCc(BMS_ProtectLatestCc_t *snapshot)
{
    bool available;

    if (snapshot == NULL)
    {
        return false;
    }

    vTaskSuspendAll();
    *snapshot = s_latest_cc;
    available = snapshot->valid && !s_xready_state.active &&
        (snapshot->xready_generation ==
         s_xready_state.xready_generation);
    if (!available)
    {
        snapshot->valid = false;
    }
    (void)xTaskResumeAll();
    return available;
}

bool BMS_Protect_GetXreadyState(BMS_ProtectXreadyState_t *snapshot)
{
    if (snapshot == NULL)
    {
        return false;
    }

    vTaskSuspendAll();
    *snapshot = s_xready_state;
    (void)xTaskResumeAll();
    return true;
}

bool BMS_Protect_XreadyBindingIsCurrent(
    const BMS_ProtectXreadyState_t *state,
    uint32_t bound_generation)
{
    return (state != NULL) && !state->active &&
           (state->xready_generation == bound_generation);
}

/* ------------------------------------------------------------------ */
/* XREADY recovery (H-03).                                             */
/* ------------------------------------------------------------------ */
bool BMS_Protect_RecoverXready(BQ76940_t *device)
{
    BQ76940_Status_t status;
    bool coordinator_authorized;

    if (device == NULL)
    {
        return false;
    }
    /* A prior full recovery reached an ambiguous W1C finalization. Re-running
     * either the hook or W1C while XREADY remains high could replay a
     * non-idempotent recovery or clear a newer event. Wait for observed-low. */
    if ((s_w1c_finalization_ambiguous_mask &
         BMS_PROTECT_STAT_DEVICE_XREADY) != 0U)
    {
        return false;
    }
    vTaskSuspendAll();
    coordinator_authorized = s_xready_clear_authorization.valid &&
        s_xready_state.active &&
        (s_xready_clear_authorization.xready_generation ==
         s_xready_state.xready_generation);
    (void)xTaskResumeAll();
    if (!coordinator_authorized &&
        ((s_xready_recovery_hook == NULL) ||
         !s_xready_recovery_hook(device)))
    {
        return false;
    }

    /* XREADY is W1C only after the authoritative hook confirms the complete
     * recovery contract. The history latch intentionally remains set. */
    s_xready_recovery_pending = true;
    status = BQ76940_WriteByte(device, BQ76940_REG_SYS_STAT,
                               BMS_PROTECT_STAT_DEVICE_XREADY);
    if (status == BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS)
    {
        vTaskSuspendAll();
        s_xready_clear_ack.xready_generation =
            s_xready_state.xready_generation;
        s_xready_clear_ack.recovery_revision =
            s_xready_clear_authorization.recovery_revision;
        s_xready_clear_ack.protect_revision = s_publication_revision;
        s_xready_clear_ack.accepted = false;
        s_xready_clear_ack.finalization_ambiguous = true;
        s_xready_clear_authorization.valid = false;
        (void)xTaskResumeAll();
        BMS_Protect_RecordW1cFinalizationAmbiguity(
            BMS_PROTECT_STAT_DEVICE_XREADY);
        BMS_Protect_RecordAfeFailure(status);
        return false;
    }
    if (status != BQ76940_STATUS_OK)
    {
        /* The clear was rejected: keep the fault pending. */
        BMS_Protect_RecordAfeFailure(status);
        return false;
    }
    vTaskSuspendAll();
    s_fault.active &= ~(BMS_Fault_Mask(BMS_FAULT_ID_AFE_XREADY));
    s_xready_state.active = false;
    s_xready_recovery_pending = false;
    BMS_Protect_AdvanceRevision();
    s_xready_clear_ack.xready_generation =
        s_xready_state.xready_generation;
    s_xready_clear_ack.recovery_revision =
        s_xready_clear_authorization.recovery_revision;
    s_xready_clear_ack.protect_revision = s_publication_revision;
    s_xready_clear_ack.accepted = true;
    s_xready_clear_ack.finalization_ambiguous = false;
    s_xready_clear_authorization.valid = false;
    (void)xTaskResumeAll();
    return true;
}

bool BMS_Protect_AuthorizeXreadyClear(uint32_t xready_generation,
                                     uint32_t recovery_revision)
{
    bool accepted;

    vTaskSuspendAll();
    accepted = s_xready_state.active &&
        (s_xready_state.xready_generation == xready_generation) &&
        !s_xready_clear_authorization.valid &&
        !s_xready_clear_ack.finalization_ambiguous;
    if (accepted)
    {
        s_xready_clear_authorization.xready_generation = xready_generation;
        s_xready_clear_authorization.recovery_revision = recovery_revision;
        s_xready_clear_authorization.valid = true;
        s_xready_clear_ack.accepted = false;
        s_xready_clear_ack.finalization_ambiguous = false;
    }
    (void)xTaskResumeAll();
    return accepted;
}

bool BMS_Protect_GetXreadyClearAck(BMS_ProtectXreadyClearAck_t *ack)
{
    if (ack == NULL)
    {
        return false;
    }
    vTaskSuspendAll();
    *ack = s_xready_clear_ack;
    (void)xTaskResumeAll();
    return true;
}

bool BMS_Protect_ReleaseXreadyActionLatch(uint32_t xready_generation,
                                         uint32_t recovery_revision)
{
    bool released;

    vTaskSuspendAll();
    released = !s_xready_state.active &&
        s_xready_clear_ack.accepted &&
        (s_xready_state.xready_generation == xready_generation) &&
        (s_xready_clear_ack.xready_generation == xready_generation) &&
        (s_xready_clear_ack.recovery_revision == recovery_revision);
    if (released)
    {
        s_fault.latched &=
            ~(BMS_Fault_Mask(BMS_FAULT_ID_AFE_XREADY));
        BMS_Protect_AdvanceRevision();
    }
    (void)xTaskResumeAll();
    return released;
}

static bool BMS_Protect_TimeNotExpired(uint32_t now_ms,
                                       uint32_t expiry_ms)
{
    return ((int32_t)(expiry_ms - now_ms) >= 0);
}

static bool BMS_Protect_SourceOfFault(
    BMS_FaultId_t fault_id,
    BMS_ProtectSourceId_t *source,
    uint8_t *status_mask)
{
    if ((source == NULL) || (status_mask == NULL))
    {
        return false;
    }
    if (fault_id == BMS_FAULT_ID_HW_OV)
    {
        *source = BMS_PROTECT_SOURCE_HW_OV;
        *status_mask = BMS_PROTECT_STAT_OV;
        return true;
    }
    if (fault_id == BMS_FAULT_ID_HW_UV)
    {
        *source = BMS_PROTECT_SOURCE_HW_UV;
        *status_mask = BMS_PROTECT_STAT_UV;
        return true;
    }
    if (fault_id == BMS_FAULT_ID_HW_OCD)
    {
        *source = BMS_PROTECT_SOURCE_HW_OCD;
        *status_mask = BMS_PROTECT_STAT_OCD;
        return true;
    }
    return false;
}

static bool BMS_Protect_HwRequestIsCurrent(
    const BMS_ProtectHwRecoveryRequest_t *request,
    uint32_t now_ms)
{
    BMS_ProtectSourceId_t source;
    BMS_DataIdentity_t identity;
    uint8_t status_mask;

    if ((request == NULL) || !request->valid ||
        !BMS_Protect_TimeNotExpired(now_ms, request->expiry_ms) ||
        !BMS_Protect_SourceOfFault(request->fault_id,
                                   &source, &status_mask) ||
        !BMS_Fault_Contains(s_fault.active, request->fault_id) ||
        (s_source_generation[source] !=
         request->expected_source_generation) ||
        s_xready_state.active ||
        !BMS_Data_GetIdentity(&identity))
    {
        return false;
    }
    (void)status_mask;
    return (identity.sample_sequence ==
            request->evaluated_sample_sequence) &&
        (identity.afe_generation ==
         request->evaluated_afe_generation);
}

bool BMS_Protect_SubmitHwRecoveryRequest(
    const BMS_ProtectHwRecoveryRequest_t *request)
{
    bool accepted;

    if ((request == NULL) || !request->valid)
    {
        return false;
    }
    vTaskSuspendAll();
    accepted = BMS_Protect_HwRequestIsCurrent(
        request, (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));
    if (accepted)
    {
        s_hw_recovery_request = *request;
        s_hw_recovery_ack.accepted = false;
    }
    (void)xTaskResumeAll();
    if (accepted)
    {
        App_Rtos_RequestProtectService();
    }
    return accepted;
}

bool BMS_Protect_GetHwRecoveryAck(BMS_ProtectHwRecoveryAck_t *ack)
{
    if (ack == NULL)
    {
        return false;
    }
    vTaskSuspendAll();
    *ack = s_hw_recovery_ack;
    (void)xTaskResumeAll();
    return true;
}

bool BMS_Protect_SubmitServiceResetRequest(
    const BMS_ServiceResetRequest_t *request)
{
    BMS_DataIdentity_t identity;
    bool accepted;

    if ((request == NULL) || !request->valid ||
        ((uint32_t)request->source >=
         (uint32_t)BMS_SERVICE_RESET_SOURCE_COUNT) ||
        !BMS_Data_GetIdentity(&identity))
    {
        return false;
    }
    accepted = !s_xready_state.active &&
        (identity.sample_sequence == request->evaluated_sample_sequence) &&
        (identity.afe_generation == request->evaluated_afe_generation);
    if (accepted)
    {
        vTaskSuspendAll();
        s_service_reset_request = *request;
        s_service_reset_ack.accepted = false;
        (void)xTaskResumeAll();
        App_Rtos_RequestProtectService();
    }
    return accepted;
}

bool BMS_Protect_GetServiceResetAck(BMS_ServiceResetAck_t *ack)
{
    if (ack == NULL)
    {
        return false;
    }
    vTaskSuspendAll();
    *ack = s_service_reset_ack;
    (void)xTaskResumeAll();
    return true;
}

static void BMS_Protect_ServiceHwRecovery(uint32_t now_ms)
{
    BMS_ProtectHwRecoveryRequest_t request;
    BMS_ProtectSourceId_t source;
    BQ76940_Status_t status;
    uint8_t target_status_mask;
    uint8_t stat;

    vTaskSuspendAll();
    request = s_hw_recovery_request;
    (void)xTaskResumeAll();
    if (!request.valid)
    {
        return;
    }
    if (!BMS_Protect_HwRequestIsCurrent(&request, now_ms) ||
        !BMS_Protect_SourceOfFault(request.fault_id,
                                   &source, &target_status_mask) ||
        (xI2CMutex == NULL) ||
        (xSemaphoreTake(xI2CMutex,
                        pdMS_TO_TICKS(BMS_PROTECT_I2C_TIMEOUT_MS)) != pdTRUE))
    {
        vTaskSuspendAll();
        s_hw_recovery_request.valid = false;
        s_hw_recovery_ack.accepted = false;
        (void)xTaskResumeAll();
        return;
    }
    if (!BMS_Protect_HwRequestIsCurrent(&request, now_ms))
    {
        (void)xSemaphoreGive(xI2CMutex);
        vTaskSuspendAll();
        s_hw_recovery_request.valid = false;
        s_hw_recovery_ack.accepted = false;
        (void)xTaskResumeAll();
        return;
    }
    stat = 0U;
    status = BQ76940_ReadByte(s_afe_device, BQ76940_REG_SYS_STAT, &stat);
    if (status != BQ76940_STATUS_OK)
    {
        (void)xSemaphoreGive(xI2CMutex);
        BMS_Protect_RecordAfeFailure(status);
        vTaskSuspendAll();
        s_hw_recovery_request.valid = false;
        s_hw_recovery_ack.accepted = false;
        (void)xTaskResumeAll();
        return;
    }
    if (((stat & target_status_mask) != 0U) ||
        ((stat & (BMS_PROTECT_STAT_DEVICE_XREADY |
                  BMS_PROTECT_STAT_OVRD_ALERT |
                  BMS_PROTECT_STAT_SCD)) != 0U) ||
        !BMS_Protect_HwRequestIsCurrent(&request, now_ms))
    {
        (void)xSemaphoreGive(xI2CMutex);
        vTaskSuspendAll();
        s_hw_recovery_request.valid = false;
        s_hw_recovery_ack.accepted = false;
        (void)xTaskResumeAll();
        return;
    }
    (void)xSemaphoreGive(xI2CMutex);

    vTaskSuspendAll();
    if (s_hw_recovery_request.valid &&
        (s_hw_recovery_request.request_id == request.request_id) &&
        (s_source_generation[source] ==
         request.expected_source_generation))
    {
        s_fault.active &= ~BMS_Fault_Mask(request.fault_id);
        BMS_Protect_AdvanceRevision();
        s_hw_recovery_ack.fault_id = request.fault_id;
        s_hw_recovery_ack.request_id = request.request_id;
        s_hw_recovery_ack.source_generation =
            request.expected_source_generation;
        s_hw_recovery_ack.qualification_revision =
            request.qualification_revision;
        s_hw_recovery_ack.protect_revision = s_publication_revision;
        s_hw_recovery_ack.accepted = true;
    }
    s_hw_recovery_request.valid = false;
    (void)xTaskResumeAll();
}

static bool BMS_Protect_ServiceResetMapping(
    BMS_ServiceResetSource_t source,
    BMS_FaultId_t *fault_id,
    uint8_t *status_mask)
{
    if ((fault_id == NULL) || (status_mask == NULL))
    {
        return false;
    }
    if (source == BMS_SERVICE_RESET_HW_SCD)
    {
        *fault_id = BMS_FAULT_ID_HW_SCD;
        *status_mask = BMS_PROTECT_STAT_SCD;
        return true;
    }
    if (source == BMS_SERVICE_RESET_AFE_OVRD_ALERT)
    {
        *fault_id = BMS_FAULT_ID_AFE_OVRD_ALERT;
        *status_mask = BMS_PROTECT_STAT_OVRD_ALERT;
        return true;
    }
    if (source == BMS_SERVICE_RESET_AFE_COMM)
    {
        *fault_id = BMS_FAULT_ID_AFE_COMM;
        *status_mask = 0U;
        return true;
    }
    return false;
}

static void BMS_Protect_ServiceReset(uint32_t now_ms)
{
    BMS_ServiceResetRequest_t request;
    BMS_DataIdentity_t identity;
    BMS_FaultId_t fault_id;
    BQ76940_Status_t status;
    uint8_t status_mask;
    uint8_t stat;
    bool current;

    vTaskSuspendAll();
    request = s_service_reset_request;
    (void)xTaskResumeAll();
    if (!request.valid ||
        !BMS_Protect_ServiceResetMapping(request.source,
                                         &fault_id, &status_mask) ||
        !BMS_Protect_TimeNotExpired(now_ms, request.expiry_ms) ||
        s_xready_state.active || !BMS_Data_GetIdentity(&identity) ||
        (identity.sample_sequence != request.evaluated_sample_sequence) ||
        (identity.afe_generation != request.evaluated_afe_generation) ||
        (xI2CMutex == NULL) ||
        (xSemaphoreTake(xI2CMutex,
                        pdMS_TO_TICKS(BMS_PROTECT_I2C_TIMEOUT_MS)) != pdTRUE))
    {
        vTaskSuspendAll();
        s_service_reset_request.valid = false;
        s_service_reset_ack.accepted = false;
        (void)xTaskResumeAll();
        return;
    }
    stat = 0U;
    status = BQ76940_ReadByte(s_afe_device, BQ76940_REG_SYS_STAT, &stat);
    (void)xSemaphoreGive(xI2CMutex);
    if (status != BQ76940_STATUS_OK)
    {
        BMS_Protect_RecordAfeFailure(status);
        current = false;
    }
    else
    {
        current = ((stat & status_mask) == 0U) &&
            !s_xready_state.active && BMS_Data_GetIdentity(&identity) &&
            (identity.sample_sequence == request.evaluated_sample_sequence) &&
            (identity.afe_generation == request.evaluated_afe_generation);
        if (request.source == BMS_SERVICE_RESET_AFE_COMM)
        {
            current = current &&
                !BMS_Fault_Contains(s_fault.active,
                                    BMS_FAULT_ID_AFE_COMM);
        }
    }
    vTaskSuspendAll();
    if (current && s_service_reset_request.valid &&
        (s_service_reset_request.request_id == request.request_id))
    {
        s_fault.active &= ~BMS_Fault_Mask(fault_id);
        s_fault.latched &= ~BMS_Fault_Mask(fault_id);
        BMS_Protect_AdvanceRevision();
        s_service_reset_ack.source = request.source;
        s_service_reset_ack.request_id = request.request_id;
        s_service_reset_ack.protect_revision = s_publication_revision;
        s_service_reset_ack.accepted = true;
    }
    else
    {
        s_service_reset_ack.accepted = false;
    }
    s_service_reset_request.valid = false;
    (void)xTaskResumeAll();
}

/* ------------------------------------------------------------------ */
/* SYS_STAT drain (H-05) + per-bit handling (H-01/H-02/H-03).          */
/* ------------------------------------------------------------------ */
static void BMS_Protect_HandleCcReady(BQ76940_t *device,
                                      uint8_t *clear_mask)
{
    int16_t cc_raw;
    BQ76940_Status_t status;

    /* ACKed bytes plus failed STOP leave old/new event identity unknowable.
     * Do not W1C replay and do not enqueue again while that bit remains high. */
    if ((s_w1c_finalization_ambiguous_mask &
         BMS_PROTECT_STAT_CC_READY) != 0U)
    {
        return;
    }

    /* A previous sample was committed to the queue but its W1C was definitely
     * rejected. Retry only the clear so the sample cannot be enqueued twice. */
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

    /* OV event: capture unresolved active + inhibit CHG. Only the Phase 9
     * recovery owner may clear it after the full recovery contract. */
    if ((stat & BMS_PROTECT_STAT_OV) != 0U)
    {
        faults->active |= BMS_Fault_Mask(BMS_FAULT_ID_HW_OV);
        request->chg = BQ76940_FET_DESIRE_DISABLE;
        *clear_mask |= BMS_PROTECT_STAT_OV;
    }

    /* UV event: capture unresolved active + inhibit DSG; not auto-cleared. */
    if ((stat & BMS_PROTECT_STAT_UV) != 0U)
    {
        faults->active |= BMS_Fault_Mask(BMS_FAULT_ID_HW_UV);
        request->dsg = BQ76940_FET_DESIRE_DISABLE;
        *clear_mask |= BMS_PROTECT_STAT_UV;
    }

    /* OCD event: capture unresolved active + inhibit DSG; not auto-cleared. */
    if ((stat & BMS_PROTECT_STAT_OCD) != 0U)
    {
        faults->active |= BMS_Fault_Mask(BMS_FAULT_ID_HW_OCD);
        request->dsg = BQ76940_FET_DESIRE_DISABLE;
        *clear_mask |= BMS_PROTECT_STAT_OCD;
    }

    /* SCD event: active + permanent-policy latch + inhibit both. */
    if ((stat & BMS_PROTECT_STAT_SCD) != 0U)
    {
        faults->active |= BMS_Fault_Mask(BMS_FAULT_ID_HW_SCD);
        faults->latched |= BMS_Fault_Mask(BMS_FAULT_ID_HW_SCD);
        request->chg = BQ76940_FET_DESIRE_DISABLE;
        request->dsg = BQ76940_FET_DESIRE_DISABLE;
        *clear_mask |= BMS_PROTECT_STAT_SCD;
    }

    /* OVRD_ALERT (H-01): active+latched independent capture, inhibit both.
     * Phase 7 has no physical-source recovery policy and never auto-clears. */
    if ((stat & BMS_PROTECT_STAT_OVRD_ALERT) != 0U)
    {
        faults->active |= BMS_Fault_Mask(BMS_FAULT_ID_AFE_OVRD_ALERT);
        faults->latched |= BMS_Fault_Mask(BMS_FAULT_ID_AFE_OVRD_ALERT);
        request->chg = BQ76940_FET_DESIRE_DISABLE;
        request->dsg = BQ76940_FET_DESIRE_DISABLE;
        *clear_mask |= BMS_PROTECT_STAT_OVRD_ALERT;
    }

    /* XREADY (H-03): active+latched + both off. Full reinitialization may
     * clear active through BMS_Protect_RecoverXready; latched remains owned by
     * the Phase 9 explicit-reset policy and is never cleared here. */
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
    BMS_FaultSummary_t previous_fault;
    bool xready_was_active;

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
        BMS_Protect_ResolveObservedLowW1c(stat);
        BMS_Protect_RecordAfeReadSuccess();
        vTaskSuspendAll();
        BMS_Protect_RecordNewSourceEvents(stat);
        (void)xTaskResumeAll();

        /* A definitely rejected W1C can later be rendered moot by reset or an
         * authorized external clear. A successful low read retires its retry
         * marker. Ambiguous finalization uses the separate quarantine above. */
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
        /* The getter also suspends the scheduler, so active+latched publish as
         * one task-context generation rather than two independently torn
         * words. ProtectTask remains the only Phase 7 writer. */
        vTaskSuspendAll();
        previous_fault = s_fault;
        xready_was_active = s_xready_state.active;
        BMS_Protect_Decide(stat, &s_fault, &g_bms_fet_request, &clear_mask);
        if ((stat & BMS_PROTECT_STAT_DEVICE_XREADY) != 0U)
        {
            if (!s_xready_state.active)
            {
                s_xready_state.xready_generation =
                    BMS_PROTECT_XREADY_GENERATION_NEXT(
                        s_xready_state.xready_generation);
                /* The prior AFE epoch's current must not survive recovery as
                 * a consumable latest-CC mailbox value. */
                s_latest_cc.valid = false;
                s_xready_clear_authorization.valid = false;
                s_xready_clear_ack.accepted = false;
                s_xready_clear_ack.finalization_ambiguous = false;
            }
            s_xready_state.active = true;
            s_xready_recovery_pending = true;
        }
        if ((previous_fault.active != s_fault.active) ||
            (previous_fault.latched != s_fault.latched) ||
            (xready_was_active != s_xready_state.active))
        {
            BMS_Protect_AdvanceRevision();
        }
        (void)xTaskResumeAll();
        if (BMS_Protect_HasFaultBits(stat) && (xSysEvents != NULL))
        {
            (void)xEventGroupSetBits(xSysEvents, EVT_FAULT_PRESENT);
        }
        if ((stat & BMS_PROTECT_STAT_CC_READY) != 0U)
        {
            BMS_Protect_HandleCcReady(device, &clear_mask);
        }
        /* Never replay a W1C whose previous finalization is unresolved. Other
         * newly captured bits in the same snapshot may still be cleared. */
        clear_mask = (uint8_t)(clear_mask &
                               (uint8_t)(~s_w1c_finalization_ambiguous_mask));

        if (clear_mask != 0U)
        {
            status = BQ76940_WriteByte(device, BQ76940_REG_SYS_STAT,
                                       clear_mask);
            if (status == BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS)
            {
                /* Contain, diagnose and quarantine. With no documented BQ
                 * commit point, software cannot safely pick replay versus a
                 * new event identity while the requested bit stays high. */
                BMS_Protect_RecordW1cFinalizationAmbiguity(clear_mask);
                BMS_Protect_RecordAfeFailure(status);
                return BMS_PROTECT_DRAIN_RETRY_REQUIRED;
            }
            if (status != BQ76940_STATUS_OK)
            {
                /* Clear was rejected: keep pending and retry it. */
                BMS_Protect_RecordAfeFailure(status);
                return BMS_PROTECT_DRAIN_RETRY_REQUIRED;
            }
            if ((clear_mask & BMS_PROTECT_STAT_CC_READY) != 0U)
            {
                s_cc_clear_pending = false;
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
    uint32_t now_ms;
    TickType_t wait_ticks;

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
            wait_ticks = pdMS_TO_TICKS(BMS_PROTECT_HEALTH_WAIT_MS);
            (void)xSemaphoreTake(xAfeAlertSem, wait_ticks);
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(BMS_PROTECT_RETRY_DELAY_MS));
        }

        retry_pending =
            (BMS_Protect_ServicePending(s_afe_device) ==
             BMS_PROTECT_SERVICE_RETRY_REQUIRED);
        now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        BMS_Protect_UpdateAfeCommPolicy(now_ms);
        BMS_Protect_ServiceHwRecovery(now_ms);
        BMS_Protect_ServiceReset(now_ms);
        BMS_Health_Heartbeat(BMS_HEALTH_TASK_PROTECT);
        App_Rtos_NotifyStateUrgent();
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
