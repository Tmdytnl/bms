#ifndef BMS_PROTECT_H
#define BMS_PROTECT_H

#include <stdbool.h>
#include <stdint.h>

#include "app_rtos.h"
#include "bms_fault.h"
#include "bms_policy.h"
#include "bms_safety.h"
#include "bq76940.h"
#include "bq76940_control.h"

/*
 * BMS V1 ProtectTask / ALERT path (Phase 7).
 *
 * Official TI basis (BQ769x0 Datasheet SLUSBK2I Rev.I §8.3.1.3):
 *   - SYS_STAT is write-1-to-clear; each bit indicates an event.
 *   - CC_READY (bit7): fresh coulomb reading; latches if not cleared.
 *   - DEVICE_XREADY (bit5): device fault or SHIP->NORMAL entry; clears
 *     CHG/DSG automatically; host must clear it and rewrite FETs.
 *   - OVRD_ALERT (bit4): external ALERT override; disables both FETs.
 *   - UV/OV/SCD/OCD bits 3..0.
 *
 * Errata application:
 *   - H-01: OVRD_ALERT handled independently (fault + FET inhibit +
 *     W1C), never swallowed by another branch.
 *   - H-02: CC_READY cleared only after the CC read + queue push
 *     succeed; on queue-full the newest sample replaces the oldest and
 *     the bit is cleared only after the newest sample entered.
 *   - H-03: XREADY latches a fault, disables both FETs, and clears only
 *     after a full recovery sequence (re-init, calibration, protection
 *     config re-apply) succeeds.
 *   - H-05: ALERT is drained with retry; a stuck-high line keeps the
 *     pending state instead of waiting for a new edge.
 *
 * Phase 7 fault-lifecycle boundary:
 *   - SYS_STAT is an event-capture source. W1C/observed-low is not evidence
 *     that the underlying voltage/current/physical condition recovered.
 *   - HW_OV, HW_UV and HW_OCD set active only. They are recovery-eligible,
 *     but remain active until the Phase 9 recovery owner proves fresh valid
 *     measurements, threshold+hysteresis+delay, policy and hardware status.
 *   - HW_SCD, AFE_XREADY and AFE_OVRD_ALERT set active+latched. Phase 7 never
 *     auto-clears their latches. SCD/OVRD require an explicit later policy;
 *     XREADY active may clear only after full recovery and confirmed W1C.
 *   - ProtectTask is the Phase 7 hardware-event capture/publish owner. Other
 *     modules receive snapshots and must not infer recovery from SYS_STAT=0.
 *
 * This module owns the ALERT ISR entry (EXTI1_IRQHandler) and the
 * ProtectTask body. It reuses the Phase 5 FET arbitration primitives and
 * the shared IPC objects; State, SOC, balancing and CAN remain separate
 * owner modules.
 */

/* SYS_STAT bit masks (SLUSBK2I §8.3.1.3; Phase 3 regs.h has the address,
 * these bit definitions are Phase 7 additions kept local to this module). */
#define BMS_PROTECT_STAT_CC_READY       ((uint8_t)0x80U)
#define BMS_PROTECT_STAT_DEVICE_XREADY  ((uint8_t)0x20U)
#define BMS_PROTECT_STAT_OVRD_ALERT     ((uint8_t)0x10U)
#define BMS_PROTECT_STAT_UV             ((uint8_t)0x08U)
#define BMS_PROTECT_STAT_OV             ((uint8_t)0x04U)
#define BMS_PROTECT_STAT_SCD            ((uint8_t)0x02U)
#define BMS_PROTECT_STAT_OCD            ((uint8_t)0x01U)

/* I2C mutex take timeout for the ALERT path (spec §27 uses 20 ms). */
#define BMS_PROTECT_I2C_TIMEOUT_MS      (20U)

/* Drain-retry budget (H-05): bounded retries per ALERT wake. */
#define BMS_PROTECT_DRAIN_MAX_ITER      (4U)

/* Delay between task-level retry attempts. This prevents a stuck ALERT or
 * unavailable I2C mutex from turning the highest-priority task into a busy
 * loop while keeping retry independent of another EXTI edge. */
#define BMS_PROTECT_RETRY_DELAY_MS      (10U)
#define BMS_PROTECT_HEALTH_WAIT_MS       (100U)

typedef enum
{
    BMS_PROTECT_DRAIN_COMPLETE = 0,
    BMS_PROTECT_DRAIN_RETRY_REQUIRED
} BMS_ProtectDrainResult_t;

typedef enum
{
    BMS_PROTECT_SERVICE_IDLE = 0,
    BMS_PROTECT_SERVICE_RETRY_REQUIRED
} BMS_ProtectServiceResult_t;

typedef struct
{
    uint32_t cc_queue_overflow_count;
    /* Number of already-queued samples irrecoverably dropped to make room. */
    uint32_t cc_sample_missed_count;
    /* Newest-sample enqueue attempts that failed during overflow recovery.
     * CC_READY remains set in this case, so the hardware sample is retried. */
    uint32_t cc_enqueue_failure_count;
    /* SYS_STAT W1C transactions whose payload/CRC were ACKed but final STOP
     * failed. This is transaction history, not a claim of register commit. */
    uint32_t w1c_finalization_ambiguous_count;
    /* Subset of the above transactions that included CC_READY. A nonzero
     * value means CC event identity may have coalesced and Phase 10 must not
     * claim exact-zero-loss integration across the event. */
    uint32_t cc_event_identity_ambiguous_count;
    /* Currently quarantined SYS_STAT bits. A quarantined bit is neither W1C
     * replayed nor treated as a new event until an observed-low read retires
     * it. A continuously high old/new event cannot be disambiguated in
     * software. */
    uint8_t w1c_finalization_ambiguous_mask;
    bool cc_queue_overflow_latched;
    bool w1c_finalization_ambiguous_latched;
} BMS_ProtectDiagnostics_t;

/*
 * Scheduler-coherent mirror of the newest inactive/current-epoch CC sample
 * which was accepted by xCcSampleQueue. SampleTask reads this mailbox; it
 * must never receive/peek the SOC-owned queue or perform a second CC register
 * read.
 * sequence is a mailbox generation tag (natural unsigned wrap is
 * intentional). xready_generation binds the sample to the AFE epoch in
 * which it was read; an XREADY transition invalidates the mailbox even when
 * the SOC-owned queue still accepts a CC sample under its existing contract.
 */
typedef struct
{
    int16_t raw;
    TickType_t tick;
    uint32_t sequence;
    uint32_t xready_generation;
    bool valid;
} BMS_ProtectLatestCc_t;

/*
 * Scheduler-coherent XREADY epoch. The generation advances only on the
 * first inactive-to-active observation and wraps naturally. Clearing active
 * after recovery never rewinds the generation. Equality rejects a binding
 * across the observed transition, including the immediate UINT32_MAX-to-zero
 * wrap. Avoiding alias after a complete 2^32 XREADY-event cycle depends on
 * the system watchdog/rebinding assumption and is not claimed here.
 */
typedef struct
{
    uint32_t xready_generation;
    bool active;
} BMS_ProtectXreadyState_t;

typedef enum
{
    BMS_PROTECT_SOURCE_HW_OV = 0,
    BMS_PROTECT_SOURCE_HW_UV,
    BMS_PROTECT_SOURCE_HW_OCD,
    BMS_PROTECT_SOURCE_HW_SCD,
    BMS_PROTECT_SOURCE_COUNT
} BMS_ProtectSourceId_t;

typedef struct
{
    BMS_FaultSummary_t faults;
    BMS_InhibitReasonBitmap_t inhibit_chg_reasons;
    BMS_InhibitReasonBitmap_t inhibit_dsg_reasons;
    uint32_t publication_revision;
    uint32_t source_generation[BMS_PROTECT_SOURCE_COUNT];
    uint32_t xready_generation;
    bool xready_active;
} BMS_ProtectSafetySnapshot_t;

typedef struct
{
    uint32_t xready_generation;
    uint32_t recovery_revision;
    bool valid;
} BMS_ProtectXreadyClearAuthorization_t;

typedef struct
{
    uint32_t xready_generation;
    uint32_t recovery_revision;
    uint32_t protect_revision;
    bool accepted;
    bool finalization_ambiguous;
} BMS_ProtectXreadyClearAck_t;

typedef struct
{
    BMS_FaultId_t fault_id;
    uint32_t request_id;
    uint32_t expected_source_generation;
    uint32_t evaluated_sample_sequence;
    uint32_t evaluated_afe_generation;
    uint32_t qualification_revision;
    uint32_t expiry_ms;
    bool valid;
} BMS_ProtectHwRecoveryRequest_t;

typedef struct
{
    BMS_FaultId_t fault_id;
    uint32_t request_id;
    uint32_t source_generation;
    uint32_t qualification_revision;
    uint32_t protect_revision;
    bool accepted;
} BMS_ProtectHwRecoveryAck_t;

typedef enum
{
    BMS_SERVICE_RESET_HW_SCD = 0,
    BMS_SERVICE_RESET_AFE_OVRD_ALERT,
    BMS_SERVICE_RESET_AFE_COMM,
    BMS_SERVICE_RESET_SOURCE_COUNT
} BMS_ServiceResetSource_t;

typedef struct
{
    BMS_ServiceResetSource_t source;
    uint32_t request_id;
    uint32_t evaluated_sample_sequence;
    uint32_t evaluated_afe_generation;
    uint32_t qualification_revision;
    uint32_t expiry_ms;
    bool valid;
} BMS_ServiceResetRequest_t;

typedef struct
{
    BMS_ServiceResetSource_t source;
    uint32_t request_id;
    uint32_t protect_revision;
    bool accepted;
} BMS_ServiceResetAck_t;

/* Modular generation advance shared with the production-C wrap regression. */
#define BMS_PROTECT_CC_SEQUENCE_NEXT(sequence_) \
    ((uint32_t)((uint32_t)(sequence_) + 1UL))
#define BMS_PROTECT_XREADY_GENERATION_NEXT(generation_) \
    ((uint32_t)((uint32_t)(generation_) + 1UL))

/* Phase 9 supplies the authoritative XREADY recovery implementation. The
 * hook may return true only after device re-initialization, required settling,
 * calibration reload, authoritative protection/configuration re-apply with
 * readback, and status-group verification have all succeeded. The hook runs
 * while the I2C mutex is held, so each call must be bounded and nonblocking;
 * a multi-step/settling state machine returns false between short steps and
 * never delays while holding the mutex. Phase 7 keeps XREADY pending when no
 * such hook is installed. */
typedef bool (*BMS_ProtectXreadyRecoveryHook_t)(BQ76940_t *device);

/* FET request helpers (H-04: modules only submit requests). */
extern BQ76940_FetRequest_t g_bms_fet_request;

/*
 * Initialize the protect module state (fault summary, FET request to
 * all-off, no XREADY recovery in progress). Called once before the
 * scheduler starts.
 */
void BMS_Protect_Init(void);

/*
 * Bind the shared BQ transport device used by the ALERT drain path.
 * Phase 8 will own the canonical handle; this setter allows Phase 7 to
 * inject the device for testing and early integration.
 */
void BMS_Protect_SetDevice(BQ76940_t *device);
void BMS_Protect_SetPolicy(const BMS_Policy_t *policy);

void BMS_Protect_SetXreadyRecoveryHook(
    BMS_ProtectXreadyRecoveryHook_t recovery_hook);

/*
 * ProtectTask entry (priority 5, registered by App_Rtos_CreateTasks).
 * Waits on xAfeAlertSem, then drains SYS_STAT with bounded retries.
 */
void Task_Protect(void *argument);

/*
 * EXTI1 ALERT ISR. Only gives xAfeAlertSem and yields; never touches the
 * BQ or I2C (spec §20.1, H-05). Defined here so the interrupt entry is
 * co-located with the protect path.
 */
void EXTI1_IRQHandler(void);

/* Return one same-generation active+latched snapshot. This task-context API
 * is scheduler-protected against the ProtectTask publisher; it is not an ISR
 * API. The returned value is read-only and SYS_STAT=0 must not be interpreted
 * as a recovery authorization. */
BMS_FaultSummary_t BMS_Protect_GetFaultSummary(void);

/* Authoritative Protect-owned action snapshot consumed directly by FET. */
BMS_ProtectSafetySnapshot_t BMS_Protect_GetSafetySnapshot(void);

/* Latched, task-context H-02 diagnostics. The returned multi-field snapshot is
 * scheduler-protected; counters saturate at UINT32_MAX rather than wrapping. */
BMS_ProtectDiagnostics_t BMS_Protect_GetDiagnostics(void);

/*
 * Pure decision function (no I2C, no RTOS): map a SYS_STAT snapshot to
 * the fault/request/clear decisions. Testable without hardware.
 *   stat        : raw SYS_STAT byte
 *   faults      : in/out fault summary (updated)
 *   request     : in/out FET request (updated)
 *   clear_mask  : out, write-1-clear bits that were successfully handled
 */
void BMS_Protect_Decide(uint8_t stat,
                        BMS_FaultSummary_t *faults,
                        BQ76940_FetRequest_t *request,
                        uint8_t *clear_mask);

/*
 * Pure decision: given a raw SYS_STAT byte, does it contain any
 * fault-class bit (OV/UV/SCD/OCD/XREADY/OVRD) as opposed to only
 * CC_READY? Used to decide urgent reporting.
 */
bool BMS_Protect_HasFaultBits(uint8_t stat);

/*
 * Drain SYS_STAT once with bounded retries (H-05). Public so tests and
 * the task can drive the same path. Reads SYS_STAT, handles every set
 * bit independently, and write-1-clears the successfully handled bits.
 */
BMS_ProtectDrainResult_t BMS_Protect_Drain(BQ76940_t *device);

/* Perform one bounded task-level service attempt. A retry result means the
 * caller must retain pending state, delay briefly, and call again without
 * waiting for another semaphore edge. */
BMS_ProtectServiceResult_t BMS_Protect_ServicePending(BQ76940_t *device);

/*
 * Feed a fresh CC sample into xCcSampleQueue with the H-02 policy:
 * newest sample always wins; on full queue exactly one oldest sample is
 * dropped and the newest is enqueued; returns true only if the newest
 * sample is now in the queue.
 */
bool BMS_Protect_PushCcSample(int16_t cc_raw);

/* Copy the latest current-epoch CC sample under scheduler exclusion. Returns
 * false for NULL, while XREADY is active, before an inactive-epoch mailbox
 * publication, or if the mailbox epoch differs from the current AFE epoch.
 * On a non-NULL unavailable result, snapshot->valid is false. */
bool BMS_Protect_GetLatestCc(BMS_ProtectLatestCc_t *snapshot);

/* Copy the current XREADY generation/active pair under scheduler exclusion.
 * Returns false only for a NULL output. Task context only, never ISR context. */
bool BMS_Protect_GetXreadyState(BMS_ProtectXreadyState_t *snapshot);

/* Pure binding decision used by SampleTask and generation-wrap tests. */
bool BMS_Protect_XreadyBindingIsCurrent(
    const BMS_ProtectXreadyState_t *state,
    uint32_t bound_generation);

/*
 * Complete the XREADY recovery contract (H-03) through the authoritative
 * hook and only then W1C XREADY. Active clears only after a successful final
 * STOP, or after a prior ambiguous finalization is resolved by observing the
 * bit low. The historical latch remains for the Phase 9 explicit-reset policy.
 */
bool BMS_Protect_RecoverXready(BQ76940_t *device);

/* Recovery Coordinator requests; Protect remains the sole runtime W1C owner. */
bool BMS_Protect_AuthorizeXreadyClear(uint32_t xready_generation,
                                     uint32_t recovery_revision);
bool BMS_Protect_GetXreadyClearAck(BMS_ProtectXreadyClearAck_t *ack);

/* SIM_POLICY_V1 source-specific XREADY action-latch release. */
bool BMS_Protect_ReleaseXreadyActionLatch(uint32_t xready_generation,
                                         uint32_t recovery_revision);

/* State qualification -> Protect fresh-status two-party recovery. */
bool BMS_Protect_SubmitHwRecoveryRequest(
    const BMS_ProtectHwRecoveryRequest_t *request);
bool BMS_Protect_GetHwRecoveryAck(BMS_ProtectHwRecoveryAck_t *ack);

/* Source-specific service reset request; never a bitmap clear command. */
bool BMS_Protect_SubmitServiceResetRequest(
    const BMS_ServiceResetRequest_t *request);
bool BMS_Protect_GetServiceResetAck(BMS_ServiceResetAck_t *ack);

#if defined(TEST_PHASE7_IMAGE) || defined(TEST_PHASE9_IMAGE)
void BMS_Protect_TestUpdateAfeCommPolicy(uint32_t now_ms);
#endif

#endif /* BMS_PROTECT_H */
