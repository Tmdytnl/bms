#ifndef BMS_PROTECT_H
#define BMS_PROTECT_H

#include <stdbool.h>
#include <stdint.h>

#include "app_rtos.h"
#include "bms_fault.h"
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
 * This module owns the ALERT ISR entry (EXTI1_IRQHandler) and the
 * ProtectTask body. It reuses the Phase 5 FET arbitration primitives and
 * the Phase 6 IPC objects; it does not implement state machine, SOC,
 * balancing or CAN (later phases).
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

/*
 * Return the current fault summary (Phase 1 model). Read-only for other
 * modules; updated by the protect path.
 */
BMS_FaultSummary_t BMS_Protect_GetFaultSummary(void);

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
void BMS_Protect_Drain(BQ76940_t *device);

/*
 * Feed a fresh CC sample into xCcSampleQueue with the H-02 policy:
 * newest sample always wins; on full queue exactly one oldest sample is
 * dropped and the newest is enqueued; returns true only if the newest
 * sample is now in the queue.
 */
bool BMS_Protect_PushCcSample(int16_t cc_raw);

/*
 * Run the XREADY recovery sequence (H-03): re-read calibration and
 * re-apply the reference protection configuration. On success clears the
 * XREADY fault and returns true; on failure keeps the fault.
 * (The full protection-config re-apply is implemented on top of Phase 5
 * control primitives; Phase 9 wires the complete config table.)
 */
bool BMS_Protect_RecoverXready(BQ76940_t *device);

#endif /* BMS_PROTECT_H */
