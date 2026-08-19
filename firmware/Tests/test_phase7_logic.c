#include "test_phase7.h"

#include <stdbool.h>
#include <stdint.h>

#include "bms_fault.h"
#include "bms_protect.h"
#include "bq76940_control.h"

#define TEST_CHECK(expr_)          \
    do                             \
    {                              \
        if (!(expr_))              \
        {                          \
            ++failures;            \
        }                          \
    } while (0)

/*
 * Phase 7 pure-decision tests. BMS_Protect_Decide maps a raw SYS_STAT
 * snapshot to fault/request/clear decisions without any I2C or RTOS
 * dependency, so it is verified here without the transport/FreeRTOS
 * combination that the Keil simulator cannot execute together.
 */

uint32_t Test_Phase7_ProtectLogic(void)
{
    uint32_t failures;
    BMS_FaultSummary_t faults;
    BQ76940_FetRequest_t request;
    uint8_t clear_mask;
    uint8_t ctrl2;

    failures = 0UL;

    /* OV bit: active HW_OV fault, CHG inhibited, OV bit cleared. */
    BMS_Fault_Init(&faults);
    request.chg = BQ76940_FET_DESIRE_ENABLE;
    request.dsg = BQ76940_FET_DESIRE_ENABLE;
    clear_mask = 0U;
    BMS_Protect_Decide(BMS_PROTECT_STAT_OV, &faults, &request, &clear_mask);
    TEST_CHECK(BMS_Fault_Contains(faults.active, BMS_FAULT_ID_HW_OV));
    TEST_CHECK(!BMS_Fault_Contains(faults.active, BMS_FAULT_ID_HW_UV));
    TEST_CHECK(request.chg == BQ76940_FET_DESIRE_DISABLE);
    TEST_CHECK(request.dsg == BQ76940_FET_DESIRE_ENABLE);
    TEST_CHECK(clear_mask == BMS_PROTECT_STAT_OV);

    /* SCD: active+latched HW_SCD, both FETs inhibited. */
    BMS_Fault_Init(&faults);
    request.chg = BQ76940_FET_DESIRE_ENABLE;
    request.dsg = BQ76940_FET_DESIRE_ENABLE;
    clear_mask = 0U;
    BMS_Protect_Decide(BMS_PROTECT_STAT_SCD, &faults, &request, &clear_mask);
    TEST_CHECK(BMS_Fault_Contains(faults.active, BMS_FAULT_ID_HW_SCD));
    TEST_CHECK(BMS_Fault_Contains(faults.latched, BMS_FAULT_ID_HW_SCD));
    TEST_CHECK(request.chg == BQ76940_FET_DESIRE_DISABLE);
    TEST_CHECK(request.dsg == BQ76940_FET_DESIRE_DISABLE);
    TEST_CHECK(clear_mask == BMS_PROTECT_STAT_SCD);

    /* OVRD_ALERT (H-01): independent fault + both FETs inhibited. */
    BMS_Fault_Init(&faults);
    request.chg = BQ76940_FET_DESIRE_ENABLE;
    request.dsg = BQ76940_FET_DESIRE_ENABLE;
    clear_mask = 0U;
    BMS_Protect_Decide(BMS_PROTECT_STAT_OVRD_ALERT, &faults, &request,
                       &clear_mask);
    TEST_CHECK(BMS_Fault_Contains(faults.active,
                                  BMS_FAULT_ID_AFE_OVRD_ALERT));
    TEST_CHECK(BMS_Fault_Contains(faults.latched,
                                  BMS_FAULT_ID_AFE_OVRD_ALERT));
    TEST_CHECK(request.chg == BQ76940_FET_DESIRE_DISABLE);
    TEST_CHECK(request.dsg == BQ76940_FET_DESIRE_DISABLE);
    TEST_CHECK(clear_mask == BMS_PROTECT_STAT_OVRD_ALERT);

    /* XREADY (H-03): latched fault, both off, but NOT in clear mask. */
    BMS_Fault_Init(&faults);
    request.chg = BQ76940_FET_DESIRE_ENABLE;
    request.dsg = BQ76940_FET_DESIRE_ENABLE;
    clear_mask = 0U;
    BMS_Protect_Decide(BMS_PROTECT_STAT_DEVICE_XREADY, &faults, &request,
                       &clear_mask);
    TEST_CHECK(BMS_Fault_Contains(faults.active, BMS_FAULT_ID_AFE_XREADY));
    TEST_CHECK(BMS_Fault_Contains(faults.latched, BMS_FAULT_ID_AFE_XREADY));
    TEST_CHECK(request.chg == BQ76940_FET_DESIRE_DISABLE);
    TEST_CHECK(request.dsg == BQ76940_FET_DESIRE_DISABLE);
    TEST_CHECK(clear_mask == 0U);   /* XREADY never cleared by Decide */

    /* Combined CC_READY + OV (spec §20): CC_READY adds no fault, OV does. */
    BMS_Fault_Init(&faults);
    request.chg = BQ76940_FET_DESIRE_ENABLE;
    request.dsg = BQ76940_FET_DESIRE_ENABLE;
    clear_mask = 0U;
    BMS_Protect_Decide(BMS_PROTECT_STAT_CC_READY | BMS_PROTECT_STAT_OV,
                       &faults, &request, &clear_mask);
    TEST_CHECK(BMS_Fault_Contains(faults.active, BMS_FAULT_ID_HW_OV));
    TEST_CHECK(request.chg == BQ76940_FET_DESIRE_DISABLE);
    /* Decide handles fault bits; CC_READY bit is added by the caller
     * (HandleCcReady) after the queue push succeeds. */
    TEST_CHECK((clear_mask & BMS_PROTECT_STAT_CC_READY) == 0U);
    TEST_CHECK((clear_mask & BMS_PROTECT_STAT_OV) != 0U);

    /* Phase 5/7 integration boundary: protection decisions feed the pure FET
     * compositor without replaying factory/command/reserved SYS_CTRL2 bits. */
    ctrl2 = BQ76940_Control_SysCtrl2WithFets(0xFFU, &request);
    TEST_CHECK(ctrl2 == 0x42U);  /* OV: CC_EN + DSG only */

    BMS_Fault_Init(&faults);
    request.chg = BQ76940_FET_DESIRE_ENABLE;
    request.dsg = BQ76940_FET_DESIRE_ENABLE;
    BMS_Protect_Decide(BMS_PROTECT_STAT_UV | BMS_PROTECT_STAT_OCD,
                       &faults, &request, &clear_mask);
    ctrl2 = BQ76940_Control_SysCtrl2WithFets(0xFFU, &request);
    TEST_CHECK(ctrl2 == 0x41U);  /* UV/OCD: CC_EN + CHG only */

    BMS_Fault_Init(&faults);
    request.chg = BQ76940_FET_DESIRE_ENABLE;
    request.dsg = BQ76940_FET_DESIRE_ENABLE;
    BMS_Protect_Decide(BMS_PROTECT_STAT_SCD |
                       BMS_PROTECT_STAT_DEVICE_XREADY,
                       &faults, &request, &clear_mask);
    ctrl2 = BQ76940_Control_SysCtrl2WithFets(0xFFU, &request);
    TEST_CHECK(ctrl2 == 0x40U);  /* SCD/XREADY: both FETs off */

    /* Clean SYS_STAT: no faults, nothing cleared. */
    BMS_Fault_Init(&faults);
    request.chg = BQ76940_FET_DESIRE_ENABLE;
    request.dsg = BQ76940_FET_DESIRE_ENABLE;
    clear_mask = 0xFFU;
    BMS_Protect_Decide(0U, &faults, &request, &clear_mask);
    TEST_CHECK(faults.active == 0U);
    TEST_CHECK(clear_mask == 0U);

    /* HasFaultBits classification. */
    TEST_CHECK(BMS_Protect_HasFaultBits(BMS_PROTECT_STAT_OV));
    TEST_CHECK(BMS_Protect_HasFaultBits(BMS_PROTECT_STAT_DEVICE_XREADY));
    TEST_CHECK(!BMS_Protect_HasFaultBits(BMS_PROTECT_STAT_CC_READY));
    TEST_CHECK(!BMS_Protect_HasFaultBits(0U));

    return failures;
}

uint32_t Test_Phase7_CcQueue(void)
{
    uint32_t failures;
    uint8_t index;
    uint8_t stat_values[4];
    int16_t cc_values[2];
    BMS_CcSample_t sample;
    BMS_ProtectDiagnostics_t diagnostics;

    failures = 0UL;

    /* Single overflow: exactly one oldest sample is replaced by newest. */
    TestP7_StubReset();
    for (index = 0U; index < APP_RTOS_CC_SAMPLE_QUEUE_DEPTH; ++index)
    {
        TEST_CHECK(BMS_Protect_PushCcSample((int16_t)(100 + index)));
    }
    TEST_CHECK(BMS_Protect_PushCcSample(108));
    diagnostics = BMS_Protect_GetDiagnostics();
    TEST_CHECK(diagnostics.cc_queue_overflow_count == 1UL);
    TEST_CHECK(diagnostics.cc_sample_missed_count == 1UL);
    TEST_CHECK(diagnostics.cc_enqueue_failure_count == 0UL);
    TEST_CHECK(diagnostics.cc_queue_overflow_latched);
    TEST_CHECK((TestP7_EventBits() & EVT_CC_QUEUE_OVERFLOW) != 0U);
    TEST_CHECK(TestP7_QueueOpsProtected());
    TEST_CHECK(TestP7_QueueCount() == APP_RTOS_CC_SAMPLE_QUEUE_DEPTH);
    for (index = 0U; index < APP_RTOS_CC_SAMPLE_QUEUE_DEPTH; ++index)
    {
        TEST_CHECK(TestP7_QueuePop(&sample));
        TEST_CHECK(sample.raw == (int16_t)(101 + index));
    }

    /* Continuous producer overflow remains exactly-one-drop and newest wins. */
    TestP7_StubReset();
    for (index = 0U; index < APP_RTOS_CC_SAMPLE_QUEUE_DEPTH; ++index)
    {
        TEST_CHECK(BMS_Protect_PushCcSample((int16_t)index));
    }
    TEST_CHECK(BMS_Protect_PushCcSample(8));
    TEST_CHECK(BMS_Protect_PushCcSample(9));
    TEST_CHECK(BMS_Protect_PushCcSample(10));
    diagnostics = BMS_Protect_GetDiagnostics();
    TEST_CHECK(diagnostics.cc_queue_overflow_count == 3UL);
    TEST_CHECK(diagnostics.cc_sample_missed_count == 3UL);
    TEST_CHECK(diagnostics.cc_enqueue_failure_count == 0UL);
    for (index = 0U; index < APP_RTOS_CC_SAMPLE_QUEUE_DEPTH; ++index)
    {
        TEST_CHECK(TestP7_QueuePop(&sample));
        TEST_CHECK(sample.raw == (int16_t)(3 + index));
    }

    /* If the post-discard replacement itself fails, do not W1C. The next
     * bounded drain read retries the still-pending hardware sample. Diagnostics
     * distinguish the one lost oldest sample from the failed enqueue attempt. */
    TestP7_StubReset();
    for (index = 0U; index < APP_RTOS_CC_SAMPLE_QUEUE_DEPTH; ++index)
    {
        TEST_CHECK(BMS_Protect_PushCcSample((int16_t)index));
    }
    stat_values[0] = BMS_PROTECT_STAT_CC_READY;
    stat_values[1] = BMS_PROTECT_STAT_CC_READY;
    stat_values[2] = 0U;
    cc_values[0] = 900;
    cc_values[1] = 900;
    TestP7_SetStatScript(stat_values, NULL, 3U);
    TestP7_SetCcScript(cc_values, NULL, 2U);
    TestP7_SetReplacementFailures(1U);
    TEST_CHECK(BMS_Protect_Drain(TestP7_Device()) ==
               BMS_PROTECT_DRAIN_COMPLETE);
    diagnostics = BMS_Protect_GetDiagnostics();
    TEST_CHECK(diagnostics.cc_queue_overflow_count == 1UL);
    TEST_CHECK(diagnostics.cc_sample_missed_count == 1UL);
    TEST_CHECK(diagnostics.cc_enqueue_failure_count == 1UL);
    TEST_CHECK(TestP7_CcReadCount() == 2U);
    TEST_CHECK(TestP7_WriteCount() == 1U);
    TEST_CHECK(TestP7_WriteValue(0U) == BMS_PROTECT_STAT_CC_READY);
    TEST_CHECK(TestP7_QueueCount() == APP_RTOS_CC_SAMPLE_QUEUE_DEPTH);
    for (index = 1U; index < APP_RTOS_CC_SAMPLE_QUEUE_DEPTH; ++index)
    {
        TEST_CHECK(TestP7_QueuePop(&sample));
        TEST_CHECK(sample.raw == (int16_t)index);
    }
    TEST_CHECK(TestP7_QueuePop(&sample));
    TEST_CHECK(sample.raw == 900);

    /* Combined CC_READY + OV is sampled once and cleared in one W1C. */
    TestP7_StubReset();
    stat_values[0] = BMS_PROTECT_STAT_CC_READY | BMS_PROTECT_STAT_OV;
    stat_values[1] = 0U;
    cc_values[0] = 321;
    TestP7_SetStatScript(stat_values, NULL, 2U);
    TestP7_SetCcScript(cc_values, NULL, 1U);
    TEST_CHECK(BMS_Protect_Drain(TestP7_Device()) ==
               BMS_PROTECT_DRAIN_COMPLETE);
    TEST_CHECK(TestP7_WriteCount() == 1U);
    TEST_CHECK(TestP7_WriteValue(0U) ==
               (BMS_PROTECT_STAT_CC_READY | BMS_PROTECT_STAT_OV));
    TEST_CHECK(TestP7_CcReadCount() == 1U);
    TEST_CHECK(TestP7_QueueCount() == 1U);
    TEST_CHECK(TestP7_QueuePop(&sample));
    TEST_CHECK(sample.raw == 321);

    /* A failed W1C retries only the clear, never duplicates the sample. */
    TestP7_StubReset();
    stat_values[0] = BMS_PROTECT_STAT_CC_READY;
    stat_values[1] = BMS_PROTECT_STAT_CC_READY;
    stat_values[2] = 0U;
    cc_values[0] = 400;
    TestP7_SetStatScript(stat_values, NULL, 3U);
    TestP7_SetCcScript(cc_values, NULL, 1U);
    TestP7_SetWriteFailure(BQ76940_STATUS_I2C_NACK, 1U);
    TEST_CHECK(BMS_Protect_Drain(TestP7_Device()) ==
               BMS_PROTECT_DRAIN_RETRY_REQUIRED);
    TEST_CHECK(BMS_Protect_Drain(TestP7_Device()) ==
               BMS_PROTECT_DRAIN_COMPLETE);
    TEST_CHECK(TestP7_CcReadCount() == 1U);
    TEST_CHECK(TestP7_WriteCount() == 2U);
    TEST_CHECK(TestP7_WriteValue(0U) == BMS_PROTECT_STAT_CC_READY);
    TEST_CHECK(TestP7_WriteValue(1U) == BMS_PROTECT_STAT_CC_READY);
    TEST_CHECK(TestP7_QueueCount() == 1U);

    /* Payload+CRC accepted but STOP cleanup failed: the old W1C is committed.
     * If a new conversion asserts CC_READY before the next read (no observed
     * low gap), it must be sampled rather than mistaken for the old event. */
    TestP7_StubReset();
    stat_values[0] = BMS_PROTECT_STAT_CC_READY;
    stat_values[1] = BMS_PROTECT_STAT_CC_READY;
    stat_values[2] = 0U;
    cc_values[0] = 500;
    cc_values[1] = 501;
    TestP7_SetStatScript(stat_values, NULL, 3U);
    TestP7_SetCcScript(cc_values, NULL, 2U);
    TestP7_SetWriteFailure(BQ76940_STATUS_WRITE_ACCEPTED_STOP_ERROR, 1U);
    TEST_CHECK(BMS_Protect_Drain(TestP7_Device()) ==
               BMS_PROTECT_DRAIN_RETRY_REQUIRED);
    TEST_CHECK(BMS_Protect_Drain(TestP7_Device()) ==
               BMS_PROTECT_DRAIN_COMPLETE);
    TEST_CHECK(TestP7_CcReadCount() == 2U);
    TEST_CHECK(TestP7_WriteCount() == 2U);
    TEST_CHECK(TestP7_QueueCount() == 2U);
    TEST_CHECK(TestP7_QueuePop(&sample));
    TEST_CHECK(sample.raw == 500);
    TEST_CHECK(TestP7_QueuePop(&sample));
    TEST_CHECK(sample.raw == 501);

    return failures;
}

uint32_t Test_Phase7_AlertRetry(void)
{
    uint32_t failures;
    uint8_t stat_values[6];
    BQ76940_Status_t stat_statuses[4];
    BQ76940_Status_t cc_statuses[4];
    int16_t cc_values[4];
    BMS_FaultSummary_t faults;

    failures = 0UL;

    /* ISR path executes only clear/give/yield plumbing and no BQ access. */
    TestP7_StubReset();
    TEST_CHECK(TestP7_ExerciseAlertIsr());
    TEST_CHECK(TestP7_StatReadCount() == 0U);
    TEST_CHECK(TestP7_WriteCount() == 0U);

    /* H-05 Case A executes the production Task_Protect loop: one semaphore
     * token is consumed, the first mutex take fails, the task delays 10 ms,
     * retries without a new edge, drains OV, then reaches its next wait. */
    TestP7_StubReset();
    stat_values[0] = BMS_PROTECT_STAT_OV;
    stat_values[1] = 0U;
    TestP7_SetStatScript(stat_values, NULL, 2U);
    TestP7_SetMutexFailures(1U);
    TEST_CHECK(TestP7_RunProtectTaskRetryScenario());
    TEST_CHECK(TestP7_TaskDelayCount() == 1U);
    TEST_CHECK(TestP7_LastTaskDelay() ==
               pdMS_TO_TICKS(BMS_PROTECT_RETRY_DELAY_MS));
    TEST_CHECK(TestP7_ExtiInitCount() == 1U);
    TEST_CHECK(TestP7_StatReadCount() == 2U);
    TEST_CHECK(TestP7_WriteValue(0U) == BMS_PROTECT_STAT_OV);
    TEST_CHECK(TestP7_MutexAvailable());

    /* Startup-high regression: the task enables EXTI only after scheduler
     * entry, observes PB1 already high, and drains without any ISR token. */
    TestP7_StubReset();
    stat_values[0] = BMS_PROTECT_STAT_OV;
    stat_values[1] = 0U;
    TestP7_SetStatScript(stat_values, NULL, 2U);
    TEST_CHECK(TestP7_RunProtectTaskAlreadyHighScenario());
    TEST_CHECK(TestP7_ExtiInitCount() == 1U);
    TEST_CHECK(TestP7_TaskDelayCount() == 1U);
    TEST_CHECK(TestP7_StatReadCount() == 2U);
    TEST_CHECK(TestP7_WriteValue(0U) == BMS_PROTECT_STAT_OV);
    TEST_CHECK(TestP7_MutexAvailable());

    /* H-05 Case B: budget exhaustion and a still-active pin both retain
     * pending state without requiring another EXTI edge. */
    TestP7_StubReset();
    stat_values[0] = BMS_PROTECT_STAT_OV;
    stat_values[1] = BMS_PROTECT_STAT_OV;
    stat_values[2] = BMS_PROTECT_STAT_OV;
    stat_values[3] = BMS_PROTECT_STAT_OV;
    stat_values[4] = 0U;
    TestP7_SetStatScript(stat_values, NULL, 5U);
    TEST_CHECK(BMS_Protect_ServicePending(TestP7_Device()) ==
               BMS_PROTECT_SERVICE_RETRY_REQUIRED);
    TEST_CHECK(TestP7_StatReadCount() == BMS_PROTECT_DRAIN_MAX_ITER);
    TEST_CHECK(BMS_Protect_ServicePending(TestP7_Device()) ==
               BMS_PROTECT_SERVICE_IDLE);
    TestP7_SetAlertActive(true);
    TEST_CHECK(BMS_Protect_ServicePending(TestP7_Device()) ==
               BMS_PROTECT_SERVICE_RETRY_REQUIRED);
    TestP7_SetAlertActive(false);
    TEST_CHECK(BMS_Protect_ServicePending(TestP7_Device()) ==
               BMS_PROTECT_SERVICE_IDLE);
    TEST_CHECK(TestP7_MutexAvailable());

    /* H-05 Case C: a new UV event appearing while OV is being cleared is
     * observed on the next drain read and handled independently. */
    TestP7_StubReset();
    stat_values[0] = BMS_PROTECT_STAT_OV;
    stat_values[1] = BMS_PROTECT_STAT_UV;
    stat_values[2] = 0U;
    TestP7_SetStatScript(stat_values, NULL, 3U);
    TEST_CHECK(BMS_Protect_ServicePending(TestP7_Device()) ==
               BMS_PROTECT_SERVICE_IDLE);
    TEST_CHECK(TestP7_WriteCount() == 2U);
    TEST_CHECK(TestP7_WriteValue(0U) == BMS_PROTECT_STAT_OV);
    TEST_CHECK(TestP7_WriteValue(1U) == BMS_PROTECT_STAT_UV);
    faults = BMS_Protect_GetFaultSummary();
    TEST_CHECK(BMS_Fault_Contains(faults.active, BMS_FAULT_ID_HW_OV));
    TEST_CHECK(BMS_Fault_Contains(faults.active, BMS_FAULT_ID_HW_UV));

    /* SYS_STAT read timeout is observable and retains pending state. */
    TestP7_StubReset();
    stat_values[0] = 0U;
    stat_statuses[0] = BQ76940_STATUS_I2C_TIMEOUT;
    TestP7_SetStatScript(stat_values, stat_statuses, 1U);
    TEST_CHECK(BMS_Protect_ServicePending(TestP7_Device()) ==
               BMS_PROTECT_SERVICE_RETRY_REQUIRED);
    faults = BMS_Protect_GetFaultSummary();
    TEST_CHECK(BMS_Fault_Contains(faults.active, BMS_FAULT_ID_AFE_COMM));
    TEST_CHECK((TestP7_EventBits() & EVT_FAULT_PRESENT) != 0U);
    TEST_CHECK(TestP7_WriteCount() == 0U);
    TEST_CHECK(TestP7_MutexAvailable());

    /* CC CRC failures never W1C the unread sample and are classified as CRC,
     * not a generic communication fault. */
    TestP7_StubReset();
    stat_values[0] = BMS_PROTECT_STAT_CC_READY;
    stat_values[1] = BMS_PROTECT_STAT_CC_READY;
    stat_values[2] = BMS_PROTECT_STAT_CC_READY;
    stat_values[3] = BMS_PROTECT_STAT_CC_READY;
    cc_values[0] = 0;
    cc_values[1] = 0;
    cc_values[2] = 0;
    cc_values[3] = 0;
    cc_statuses[0] = BQ76940_STATUS_CRC_MISMATCH;
    cc_statuses[1] = BQ76940_STATUS_CRC_MISMATCH;
    cc_statuses[2] = BQ76940_STATUS_CRC_MISMATCH;
    cc_statuses[3] = BQ76940_STATUS_CRC_MISMATCH;
    TestP7_SetStatScript(stat_values, NULL, 4U);
    TestP7_SetCcScript(cc_values, cc_statuses, 4U);
    TEST_CHECK(BMS_Protect_Drain(TestP7_Device()) ==
               BMS_PROTECT_DRAIN_RETRY_REQUIRED);
    faults = BMS_Protect_GetFaultSummary();
    TEST_CHECK(BMS_Fault_Contains(faults.active, BMS_FAULT_ID_AFE_CRC));
    TEST_CHECK(!BMS_Fault_Contains(faults.active, BMS_FAULT_ID_AFE_COMM));
    TEST_CHECK(TestP7_WriteCount() == 0U);
    TEST_CHECK(TestP7_QueueCount() == 0U);

    return failures;
}

uint32_t Test_Phase7_Xready(void)
{
    uint32_t failures;
    uint8_t stat_values[2];
    BMS_FaultSummary_t faults;

    failures = 0UL;
    stat_values[0] = BMS_PROTECT_STAT_DEVICE_XREADY;
    stat_values[1] = 0U;

    /* No authoritative recovery hook: retain active+latched and never W1C. */
    TestP7_StubReset();
    TestP7_SetStatScript(stat_values, NULL, 1U);
    TEST_CHECK(BMS_Protect_Drain(TestP7_Device()) ==
               BMS_PROTECT_DRAIN_RETRY_REQUIRED);
    faults = BMS_Protect_GetFaultSummary();
    TEST_CHECK(BMS_Fault_Contains(faults.active, BMS_FAULT_ID_AFE_XREADY));
    TEST_CHECK(BMS_Fault_Contains(faults.latched, BMS_FAULT_ID_AFE_XREADY));
    TEST_CHECK(TestP7_WriteCount() == 0U);
    TEST_CHECK(TestP7_RecoveryCallCount() == 0U);

    /* An incomplete hook also keeps the fault pending and uncleared. */
    TestP7_StubReset();
    BMS_Protect_SetXreadyRecoveryHook(TestP7_RecoveryHook);
    TestP7_SetRecoveryResult(false);
    TestP7_SetStatScript(stat_values, NULL, 1U);
    TEST_CHECK(BMS_Protect_Drain(TestP7_Device()) ==
               BMS_PROTECT_DRAIN_RETRY_REQUIRED);
    TEST_CHECK(TestP7_RecoveryCallCount() == 1U);
    TEST_CHECK(TestP7_WriteCount() == 0U);

    /* Only the complete hook may W1C XREADY. Active clears, history remains. */
    TestP7_StubReset();
    BMS_Protect_SetXreadyRecoveryHook(TestP7_RecoveryHook);
    TestP7_SetRecoveryResult(true);
    TestP7_SetStatScript(stat_values, NULL, 2U);
    TEST_CHECK(BMS_Protect_Drain(TestP7_Device()) ==
               BMS_PROTECT_DRAIN_COMPLETE);
    TEST_CHECK(TestP7_RecoveryCallCount() == 1U);
    TEST_CHECK(TestP7_WriteCount() == 1U);
    TEST_CHECK(TestP7_WriteValue(0U) == BMS_PROTECT_STAT_DEVICE_XREADY);
    faults = BMS_Protect_GetFaultSummary();
    TEST_CHECK(!BMS_Fault_Contains(faults.active, BMS_FAULT_ID_AFE_XREADY));
    TEST_CHECK(BMS_Fault_Contains(faults.latched, BMS_FAULT_ID_AFE_XREADY));

    /* If XREADY W1C was accepted but STOP cleanup failed, recovery and W1C
     * are not replayed. A clean follow-up read completes while history stays. */
    TestP7_StubReset();
    BMS_Protect_SetXreadyRecoveryHook(TestP7_RecoveryHook);
    TestP7_SetRecoveryResult(true);
    TestP7_SetStatScript(stat_values, NULL, 2U);
    TestP7_SetWriteFailure(BQ76940_STATUS_WRITE_ACCEPTED_STOP_ERROR, 1U);
    TEST_CHECK(BMS_Protect_Drain(TestP7_Device()) ==
               BMS_PROTECT_DRAIN_RETRY_REQUIRED);
    TEST_CHECK(BMS_Protect_Drain(TestP7_Device()) ==
               BMS_PROTECT_DRAIN_COMPLETE);
    TEST_CHECK(TestP7_RecoveryCallCount() == 1U);
    TEST_CHECK(TestP7_WriteCount() == 1U);
    faults = BMS_Protect_GetFaultSummary();
    TEST_CHECK(!BMS_Fault_Contains(faults.active, BMS_FAULT_ID_AFE_XREADY));
    TEST_CHECK(BMS_Fault_Contains(faults.latched, BMS_FAULT_ID_AFE_XREADY));

    return failures;
}
