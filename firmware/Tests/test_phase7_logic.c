#include "test_phase7.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

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
 * Protect 纯决策测试：BMS_Protect_Decide 不依赖 I2C/RTOS，把同一 SYS_STAT
 * snapshot 映射为 fault/request/clear，因而可独立验证每个 bit 的 ownership。
 */

uint32_t Test_Phase7_ProtectLogic(void)
{
    uint32_t failures;
    BMS_FaultSummary_t faults;
    BQ76940_FetRequest_t request;
    uint8_t clear_mask;
    uint8_t ctrl2;

    failures = 0UL;

    /* OV：HW_OV active、CHG inhibit，并把已捕获 OV 加入 W1C mask。 */
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

    /* SCD：HW_SCD active+latched，双向 inhibit。 */
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

    /* OVRD_ALERT（H-01）：独立 fault 并双向 inhibit。 */
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

    /* XREADY（H-03）：latched+双向 inhibit，但普通 decision 绝不 clear。 */
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
    TEST_CHECK(clear_mask == 0U);   /* Decide 永不清 XREADY */

    /* CC_READY+OV：CC_READY 不产生 fault，OV 独立产生。 */
    BMS_Fault_Init(&faults);
    request.chg = BQ76940_FET_DESIRE_ENABLE;
    request.dsg = BQ76940_FET_DESIRE_ENABLE;
    clear_mask = 0U;
    BMS_Protect_Decide(BMS_PROTECT_STAT_CC_READY | BMS_PROTECT_STAT_OV,
                       &faults, &request, &clear_mask);
    TEST_CHECK(BMS_Fault_Contains(faults.active, BMS_FAULT_ID_HW_OV));
    TEST_CHECK(request.chg == BQ76940_FET_DESIRE_DISABLE);
    /* Decide 处理 fault bit；CC_READY 只有 queue push 成功后由 caller 加入 clear。 */
    TEST_CHECK((clear_mask & BMS_PROTECT_STAT_CC_READY) == 0U);
    TEST_CHECK((clear_mask & BMS_PROTECT_STAT_OV) != 0U);

    /* protection decision 进入纯 FET compositor，不传播 factory/command/reserved bit。 */
    ctrl2 = BQ76940_Control_SysCtrl2WithFets(0xFFU, &request);
    TEST_CHECK(ctrl2 == 0x42U);  /* OV：只保留 CC_EN+DSG */

    BMS_Fault_Init(&faults);
    request.chg = BQ76940_FET_DESIRE_ENABLE;
    request.dsg = BQ76940_FET_DESIRE_ENABLE;
    BMS_Protect_Decide(BMS_PROTECT_STAT_UV | BMS_PROTECT_STAT_OCD,
                       &faults, &request, &clear_mask);
    ctrl2 = BQ76940_Control_SysCtrl2WithFets(0xFFU, &request);
    TEST_CHECK(ctrl2 == 0x41U);  /* UV/OCD：只保留 CC_EN+CHG */

    BMS_Fault_Init(&faults);
    request.chg = BQ76940_FET_DESIRE_ENABLE;
    request.dsg = BQ76940_FET_DESIRE_ENABLE;
    BMS_Protect_Decide(BMS_PROTECT_STAT_SCD |
                       BMS_PROTECT_STAT_DEVICE_XREADY,
                       &faults, &request, &clear_mask);
    ctrl2 = BQ76940_Control_SysCtrl2WithFets(0xFFU, &request);
    TEST_CHECK(ctrl2 == 0x40U);  /* SCD/XREADY：双关 */

    /* SYS_STAT 全零：无 fault，也无 W1C。 */
    BMS_Fault_Init(&faults);
    request.chg = BQ76940_FET_DESIRE_ENABLE;
    request.dsg = BQ76940_FET_DESIRE_ENABLE;
    clear_mask = 0xFFU;
    BMS_Protect_Decide(0U, &faults, &request, &clear_mask);
    TEST_CHECK(faults.active == 0U);
    TEST_CHECK(clear_mask == 0U);

    /* HasFaultBits 只区分 fault-class 与单独 CC_READY。 */
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
    uint8_t stat_values[7];
    int16_t cc_values[2];
    BMS_CcSample_t sample;
    BMS_ProtectDiagnostics_t diagnostics;
    BMS_ProtectLatestCc_t latest_cc;
    BMS_ProtectLatestCc_t previous_cc;
    uint32_t suspend_before;
    uint32_t resume_before;

    failures = 0UL;

    /* latest-CC mailbox 初始 invalid，多字段 getter 与 producer 共用 scheduler exclusion。 */
    TestP7_StubReset();
    TEST_CHECK(!BMS_Protect_GetLatestCc(NULL));
    suspend_before = TestP7_SchedulerSuspendCount();
    resume_before = TestP7_SchedulerResumeCount();
    TEST_CHECK(!BMS_Protect_GetLatestCc(&latest_cc));
    TEST_CHECK(!latest_cc.valid);
    TEST_CHECK(latest_cc.raw == (int16_t)0);
    TEST_CHECK(latest_cc.sample_ms == (uint32_t)0U);
    TEST_CHECK(latest_cc.sequence == 0UL);
    TEST_CHECK(latest_cc.xready_generation == 0UL);
    TEST_CHECK(BMS_PROTECT_CC_SEQUENCE_NEXT(UINT32_MAX) == 0UL);
    TEST_CHECK(TestP7_SchedulerSuspendCount() == (suspend_before + 1UL));
    TEST_CHECK(TestP7_SchedulerResumeCount() == (resume_before + 1UL));
    TEST_CHECK(TestP7_SchedulerProtectionBalanced());

    /* queue 确认接纳后才发布 mailbox generation。 */
    TEST_CHECK(TestP7_PushCcSample(77));
    TEST_CHECK(BMS_Protect_GetLatestCc(&latest_cc));
    TEST_CHECK(latest_cc.valid);
    TEST_CHECK(latest_cc.raw == 77);
    TEST_CHECK(latest_cc.sample_ms == (uint32_t)0U);
    TEST_CHECK(latest_cc.sequence == 1UL);
    TEST_CHECK(latest_cc.xready_generation == 0UL);
    TEST_CHECK(TestP7_PushCcSample(78));
    TEST_CHECK(BMS_Protect_GetLatestCc(&latest_cc));
    TEST_CHECK(latest_cc.raw == 78);
    TEST_CHECK(latest_cc.sample_ms == (uint32_t)1U);
    TEST_CHECK(latest_cc.sequence == 2UL);
    TEST_CHECK(latest_cc.xready_generation == 0UL);

    /* 单次 overflow：只用 newest 替换一个 oldest。 */
    TestP7_StubReset();
    for (index = 0U; index < APL_RTOS_CC_SAMPLE_QUEUE_DEPTH; ++index)
    {
        TEST_CHECK(TestP7_PushCcSample((int16_t)(100 + index)));
    }
    TEST_CHECK(TestP7_PushCcSample(108));
    diagnostics = BMS_Protect_GetDiagnostics();
    TEST_CHECK(diagnostics.cc_queue_overflow_count == 1UL);
    TEST_CHECK(diagnostics.cc_sample_missed_count == 1UL);
    TEST_CHECK(diagnostics.cc_enqueue_failure_count == 0UL);
    TEST_CHECK(diagnostics.cc_queue_overflow_latched);
    TEST_CHECK((TestP7_EventBits() & EVT_CC_QUEUE_OVERFLOW) != 0U);
    TEST_CHECK(TestP7_QueueOpsProtected());
    TEST_CHECK(TestP7_QueueCount() == APL_RTOS_CC_SAMPLE_QUEUE_DEPTH);
    TEST_CHECK(BMS_Protect_GetLatestCc(&latest_cc));
    TEST_CHECK(latest_cc.raw == 108);
    TEST_CHECK(latest_cc.sample_ms == (uint32_t)8U);
    TEST_CHECK(latest_cc.sequence == 9UL);
    for (index = 0U; index < APL_RTOS_CC_SAMPLE_QUEUE_DEPTH; ++index)
    {
        TEST_CHECK(TestP7_QueuePop(&sample));
        TEST_CHECK(sample.raw == (int16_t)(101 + index));
        TEST_CHECK(sample.xready_generation == 0UL);
    }

    /* 连续 producer overflow 仍保持每次只丢一个且 newest wins。 */
    TestP7_StubReset();
    for (index = 0U; index < APL_RTOS_CC_SAMPLE_QUEUE_DEPTH; ++index)
    {
        TEST_CHECK(TestP7_PushCcSample((int16_t)index));
    }
    TEST_CHECK(TestP7_PushCcSample(8));
    TEST_CHECK(TestP7_PushCcSample(9));
    TEST_CHECK(TestP7_PushCcSample(10));
    diagnostics = BMS_Protect_GetDiagnostics();
    TEST_CHECK(diagnostics.cc_queue_overflow_count == 3UL);
    TEST_CHECK(diagnostics.cc_sample_missed_count == 3UL);
    TEST_CHECK(diagnostics.cc_enqueue_failure_count == 0UL);
    for (index = 0U; index < APL_RTOS_CC_SAMPLE_QUEUE_DEPTH; ++index)
    {
        TEST_CHECK(TestP7_QueuePop(&sample));
        TEST_CHECK(sample.raw == (int16_t)(3 + index));
    }

    /* 丢 oldest 后 replacement enqueue 失败，rejected newest 不得推进 mailbox。 */
    TestP7_StubReset();
    for (index = 0U; index < APL_RTOS_CC_SAMPLE_QUEUE_DEPTH; ++index)
    {
        TEST_CHECK(TestP7_PushCcSample((int16_t)index));
    }
    TEST_CHECK(BMS_Protect_GetLatestCc(&previous_cc));
    TestP7_SetReplacementFailures(1U);
    TEST_CHECK(!TestP7_PushCcSample(900));
    TEST_CHECK(BMS_Protect_GetLatestCc(&latest_cc));
    TEST_CHECK(latest_cc.valid == previous_cc.valid);
    TEST_CHECK(latest_cc.raw == previous_cc.raw);
    TEST_CHECK(latest_cc.sample_ms == previous_cc.sample_ms);
    TEST_CHECK(latest_cc.sequence == previous_cc.sequence);
    TEST_CHECK(latest_cc.xready_generation ==
               previous_cc.xready_generation);

    /* replacement 失败时不 W1C，下次 bounded drain 重试 pending hardware sample；
     * diagnostics 分开记录 lost oldest 与 failed enqueue。 */
    TestP7_StubReset();
    for (index = 0U; index < APL_RTOS_CC_SAMPLE_QUEUE_DEPTH; ++index)
    {
        TEST_CHECK(TestP7_PushCcSample((int16_t)index));
    }
    stat_values[0] = BMS_PROTECT_STAT_CC_READY;
    stat_values[1] = BMS_PROTECT_STAT_CC_READY;
    stat_values[2] = 0U;
    cc_values[0] = 900;
    cc_values[1] = 900;
    TestP7_SetStatScript(stat_values, NULL, 3U);
    TestP7_SetCcScript(cc_values, NULL, 2U);
    TestP7_SetReplacementFailures(1U);
    TEST_CHECK(TestP7_ProtectDrain() ==
               BMS_PROTECT_DRAIN_COMPLETE);
    diagnostics = BMS_Protect_GetDiagnostics();
    TEST_CHECK(diagnostics.cc_queue_overflow_count == 1UL);
    TEST_CHECK(diagnostics.cc_sample_missed_count == 1UL);
    TEST_CHECK(diagnostics.cc_enqueue_failure_count == 1UL);
    /* transport replacement 重试同一 staged sample，不得重复读取 CC。 */
    TEST_CHECK(TestP7_CcReadCount() == 1U);
    TEST_CHECK(TestP7_WriteCount() == 1U);
    TEST_CHECK(TestP7_WriteValue(0U) == BMS_PROTECT_STAT_CC_READY);
    TEST_CHECK(TestP7_QueueCount() == APL_RTOS_CC_SAMPLE_QUEUE_DEPTH);
    for (index = 1U; index < APL_RTOS_CC_SAMPLE_QUEUE_DEPTH; ++index)
    {
        TEST_CHECK(TestP7_QueuePop(&sample));
        TEST_CHECK(sample.raw == (int16_t)index);
    }
    TEST_CHECK(TestP7_QueuePop(&sample));
    TEST_CHECK(sample.raw == 900);

    /*
     * CC_READY+OV 同一 snapshot 分别提交：OV 可立即 W1C；CC 必须先完成 APL
     * queue handoff。若下一次 status 已观察为 low，不得为凑合并写而重放 W1C。
     */
    TestP7_StubReset();
    stat_values[0] = BMS_PROTECT_STAT_CC_READY | BMS_PROTECT_STAT_OV;
    stat_values[1] = 0U;
    cc_values[0] = 321;
    TestP7_SetStatScript(stat_values, NULL, 2U);
    TestP7_SetCcScript(cc_values, NULL, 1U);
    TEST_CHECK(TestP7_ProtectDrain() ==
               BMS_PROTECT_DRAIN_COMPLETE);
    TEST_CHECK(TestP7_WriteCount() == 1U);
    TEST_CHECK(TestP7_WriteValue(0U) == BMS_PROTECT_STAT_OV);
    TEST_CHECK(TestP7_CcReadCount() == 1U);
    TEST_CHECK(TestP7_QueueCount() == 1U);
    TEST_CHECK(TestP7_QueuePop(&sample));
    TEST_CHECK(sample.raw == 321);

    /* W1C 明确失败时只重试 clear，不重复 sample。 */
    TestP7_StubReset();
    stat_values[0] = BMS_PROTECT_STAT_CC_READY;
    stat_values[1] = BMS_PROTECT_STAT_CC_READY;
    stat_values[2] = BMS_PROTECT_STAT_CC_READY;
    stat_values[3] = 0U;
    cc_values[0] = 400;
    TestP7_SetStatScript(stat_values, NULL, 4U);
    TestP7_SetCcScript(cc_values, NULL, 1U);
    TestP7_SetWriteFailure(BQ76940_STATUS_I2C_NACK, 1U);
    TEST_CHECK(TestP7_ProtectDrain() ==
               BMS_PROTECT_DRAIN_RETRY_REQUIRED);
    TEST_CHECK(TestP7_ProtectDrain() ==
               BMS_PROTECT_DRAIN_COMPLETE);
    TEST_CHECK(TestP7_CcReadCount() == 1U);
    TEST_CHECK(TestP7_WriteCount() == 2U);
    TEST_CHECK(TestP7_WriteValue(0U) == BMS_PROTECT_STAT_CC_READY);
    TEST_CHECK(TestP7_WriteValue(1U) == BMS_PROTECT_STAT_CC_READY);
    TEST_CHECK(TestP7_QueueCount() == 1U);

    /* payload+CRC ACK 而 STOP 失败：commit ambiguous。持续高位无法区分 old/new
     * conversion；正式路径 quarantine CC_READY，不 replay、不再 enqueue，并暴露
     * possible coalescence 供 SOC 处理。 */
    TestP7_StubReset();
    stat_values[0] = BMS_PROTECT_STAT_CC_READY;
    stat_values[1] = BMS_PROTECT_STAT_CC_READY;
    stat_values[2] = BMS_PROTECT_STAT_CC_READY;
    stat_values[3] = BMS_PROTECT_STAT_CC_READY;
    stat_values[4] = BMS_PROTECT_STAT_CC_READY;
    stat_values[5] = BMS_PROTECT_STAT_CC_READY;
    stat_values[6] = 0U;
    cc_values[0] = 500;
    cc_values[1] = 501;
    TestP7_SetStatScript(stat_values, NULL, 7U);
    TestP7_SetCcScript(cc_values, NULL, 2U);
    TestP7_SetWriteFailure(
        BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS, 1U);
    TEST_CHECK(TestP7_ProtectDrain() ==
               BMS_PROTECT_DRAIN_RETRY_REQUIRED);
    diagnostics = BMS_Protect_GetDiagnostics();
    TEST_CHECK(diagnostics.w1c_finalization_ambiguous_count == 1UL);
    TEST_CHECK(diagnostics.cc_event_identity_ambiguous_count == 1UL);
    TEST_CHECK(diagnostics.w1c_finalization_ambiguous_mask ==
               BMS_PROTECT_STAT_CC_READY);
    TEST_CHECK(diagnostics.w1c_finalization_ambiguous_latched);
    /* 连续四次 high read 消耗下一次 bounded attempt，但不 replay 或虚构第二 sample。 */
    TEST_CHECK(TestP7_ProtectDrain() ==
               BMS_PROTECT_DRAIN_RETRY_REQUIRED);
    TEST_CHECK(TestP7_CcReadCount() == 1U);
    TEST_CHECK(TestP7_WriteCount() == 1U);
    TEST_CHECK(TestP7_QueueCount() == 1U);
    /* 只有 observed-low 能退休 quarantine；history 与 ambiguity counter 保留。 */
    TEST_CHECK(TestP7_ProtectDrain() ==
               BMS_PROTECT_DRAIN_COMPLETE);
    diagnostics = BMS_Protect_GetDiagnostics();
    TEST_CHECK(diagnostics.w1c_finalization_ambiguous_mask == 0U);
    TEST_CHECK(diagnostics.w1c_finalization_ambiguous_count == 1UL);
    TEST_CHECK(diagnostics.cc_event_identity_ambiguous_count == 1UL);
    TEST_CHECK(TestP7_QueuePop(&sample));
    TEST_CHECK(sample.raw == 500);
    TEST_CHECK(!TestP7_QueuePop(&sample));

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

    /* ISR path 只执行 clear/give/yield plumbing，无 BQ access。 */
    TestP7_StubReset();
    TEST_CHECK(TestP7_ExerciseAlertIsr());
    TEST_CHECK(TestP7_StatReadCount() == 0U);
    TEST_CHECK(TestP7_WriteCount() == 0U);

    /* H-05 Case A：正式 Task_Protect 消费 token，首次 mutex 失败后 delay 10 ms，
     * 无新 edge 也会 retry/drain OV，再回到下一次 wait。 */
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

    /* startup-high：任务进入 scheduler 后启用 EXTI，直接观察 PB1 high，无 ISR token 也 drain。 */
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

    /* H-05 Case B：budget exhausted 且 pin 仍 active 时保留 pending，不等新 edge。 */
    TestP7_StubReset();
    stat_values[0] = BMS_PROTECT_STAT_OV;
    stat_values[1] = BMS_PROTECT_STAT_OV;
    stat_values[2] = BMS_PROTECT_STAT_OV;
    stat_values[3] = BMS_PROTECT_STAT_OV;
    stat_values[4] = 0U;
    TestP7_SetStatScript(stat_values, NULL, 5U);
    TEST_CHECK(BMS_Protect_ServicePending(TestP7_Device(), 0UL) ==
               BMS_PROTECT_SERVICE_RETRY_REQUIRED);
    TEST_CHECK(TestP7_StatReadCount() == BMS_PROTECT_DRAIN_MAX_ITER);
    TEST_CHECK(BMS_Protect_ServicePending(TestP7_Device(), 0UL) ==
               BMS_PROTECT_SERVICE_IDLE);
    /* pin level 属于 APL task retry policy；FML service 只依据 SYS_STAT transaction。 */
    TestP7_SetAlertActive(true);
    TEST_CHECK(BMS_Protect_ServicePending(TestP7_Device(), 0UL) ==
               BMS_PROTECT_SERVICE_IDLE);
    TestP7_SetAlertActive(false);
    TEST_CHECK(BMS_Protect_ServicePending(TestP7_Device(), 0UL) ==
               BMS_PROTECT_SERVICE_IDLE);
    TEST_CHECK(TestP7_MutexAvailable());

    /* H-05 Case C：clear OV 期间新 UV 在下一次 drain read 被独立捕获。 */
    TestP7_StubReset();
    stat_values[0] = BMS_PROTECT_STAT_OV;
    stat_values[1] = BMS_PROTECT_STAT_UV;
    stat_values[2] = 0U;
    TestP7_SetStatScript(stat_values, NULL, 3U);
    TEST_CHECK(BMS_Protect_ServicePending(TestP7_Device(), 0UL) ==
               BMS_PROTECT_SERVICE_IDLE);
    TEST_CHECK(TestP7_WriteCount() == 2U);
    TEST_CHECK(TestP7_WriteValue(0U) == BMS_PROTECT_STAT_OV);
    TEST_CHECK(TestP7_WriteValue(1U) == BMS_PROTECT_STAT_UV);
    faults = BMS_Protect_GetFaultSummary();
    TEST_CHECK(BMS_Fault_Contains(faults.active, BMS_FAULT_ID_HW_OV));
    TEST_CHECK(BMS_Fault_Contains(faults.active, BMS_FAULT_ID_HW_UV));

    /* SYS_STAT read timeout 可诊断并保留 pending。 */
    TestP7_StubReset();
    stat_values[0] = 0U;
    stat_statuses[0] = BQ76940_STATUS_I2C_TIMEOUT;
    TestP7_SetStatScript(stat_values, stat_statuses, 1U);
    TEST_CHECK(BMS_Protect_ServicePending(TestP7_Device(), 0UL) ==
               BMS_PROTECT_SERVICE_RETRY_REQUIRED);
    faults = BMS_Protect_GetFaultSummary();
    TEST_CHECK(BMS_Fault_Contains(faults.active, BMS_FAULT_ID_AFE_COMM));
    /* FML 发布 fault state，不直接写 APL event group。 */
    TEST_CHECK((TestP7_EventBits() & EVT_FAULT_PRESENT) == 0U);
    TEST_CHECK(TestP7_WriteCount() == 0U);
    TEST_CHECK(TestP7_MutexAvailable());

    /* CC CRC failure 不 W1C unread sample，并保留 CRC 分类而非 generic comm。 */
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
    TEST_CHECK(TestP7_ProtectDrain() ==
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
    int16_t cc_values[1];
    BMS_CcSample_t cc_sample;
    BMS_FaultSummary_t faults;
    BMS_ProtectDiagnostics_t diagnostics;
    BMS_ProtectLatestCc_t latest_cc;
    BMS_ProtectXreadyState_t xready_state;
    uint32_t suspend_before;
    uint32_t resume_before;

    failures = 0UL;
    stat_values[0] = BMS_PROTECT_STAT_DEVICE_XREADY;
    stat_values[1] = 0U;

    /* XREADY epoch 从 inactive/generation 0 开始，getter 一致复制两个字段。 */
    TestP7_StubReset();
    TEST_CHECK(!BMS_Protect_GetXreadyState(NULL));
    suspend_before = TestP7_SchedulerSuspendCount();
    resume_before = TestP7_SchedulerResumeCount();
    TEST_CHECK(BMS_Protect_GetXreadyState(&xready_state));
    TEST_CHECK(xready_state.xready_generation == 0UL);
    TEST_CHECK(!xready_state.active);
    TEST_CHECK(TestP7_SchedulerSuspendCount() == (suspend_before + 1UL));
    TEST_CHECK(TestP7_SchedulerResumeCount() == (resume_before + 1UL));
    TEST_CHECK(BMS_PROTECT_XREADY_GENERATION_NEXT(UINT32_MAX) == 0UL);
    xready_state.xready_generation = 0UL;
    xready_state.active = false;
    TEST_CHECK(!BMS_Protect_XreadyBindingIsCurrent(
        &xready_state, UINT32_MAX));
    TEST_CHECK(BMS_Protect_XreadyBindingIsCurrent(&xready_state, 0UL));
    xready_state.active = true;
    TEST_CHECK(!BMS_Protect_XreadyBindingIsCurrent(&xready_state, 0UL));

    /*
     * XREADY+CC_READY 同一 snapshot 中，生命周期中断优先：边界 CC 不读取、
     * 不入 SOC queue，也不暴露到 Sample mailbox；只有恢复后的新 CC 可被接纳。
     */
    TEST_CHECK(TestP7_PushCcSample((int16_t)111));
    TEST_CHECK(BMS_Protect_GetLatestCc(&latest_cc));
    TEST_CHECK(latest_cc.xready_generation == 0UL);
    BMS_Protect_SetXreadyRecoveryHook(TestP7_RecoveryHook);
    TestP7_SetRecoveryResult(true);
    stat_values[0] = (uint8_t)(BMS_PROTECT_STAT_DEVICE_XREADY |
                               BMS_PROTECT_STAT_CC_READY);
    stat_values[1] = 0U;
    cc_values[0] = (int16_t)222;
    TestP7_SetStatScript(stat_values, NULL, 2U);
    TestP7_SetCcScript(cc_values, NULL, 1U);
    TEST_CHECK(TestP7_ProtectDrain() ==
               BMS_PROTECT_DRAIN_COMPLETE);
    TEST_CHECK(!BMS_Protect_GetLatestCc(&latest_cc));
    TEST_CHECK(!latest_cc.valid);
    TEST_CHECK(BMS_Protect_GetXreadyState(&xready_state));
    TEST_CHECK(xready_state.xready_generation == 1UL);
    TEST_CHECK(!xready_state.active);
    TEST_CHECK(TestP7_CcReadCount() == 0U);
    TEST_CHECK(TestP7_QueueCount() == 1U);
    TEST_CHECK(TestP7_QueuePop(&cc_sample));
    TEST_CHECK(cc_sample.raw == (int16_t)111);
    TEST_CHECK(!TestP7_QueuePop(&cc_sample));
    TEST_CHECK(TestP7_PushCcSample((int16_t)333));
    TEST_CHECK(BMS_Protect_GetLatestCc(&latest_cc));
    TEST_CHECK(latest_cc.raw == (int16_t)333);
    TEST_CHECK(latest_cc.sequence == 2UL);
    TEST_CHECK(latest_cc.xready_generation == 1UL);

    stat_values[0] = BMS_PROTECT_STAT_DEVICE_XREADY;
    stat_values[1] = 0U;

    /* 无权威 recovery hook：保留 active+latched，绝不 W1C。 */
    TestP7_StubReset();
    TestP7_SetStatScript(stat_values, NULL, 1U);
    TEST_CHECK(TestP7_ProtectDrain() ==
               BMS_PROTECT_DRAIN_RETRY_REQUIRED);
    faults = BMS_Protect_GetFaultSummary();
    TEST_CHECK(BMS_Fault_Contains(faults.active, BMS_FAULT_ID_AFE_XREADY));
    TEST_CHECK(BMS_Fault_Contains(faults.latched, BMS_FAULT_ID_AFE_XREADY));
    TEST_CHECK(BMS_Protect_GetXreadyState(&xready_state));
    TEST_CHECK(xready_state.xready_generation == 1UL);
    TEST_CHECK(xready_state.active);
    TEST_CHECK(TestP7_WriteCount() == 0U);
    TEST_CHECK(TestP7_RecoveryCallCount() == 0U);

    /* 重读同一 active event 不推进 generation。 */
    TestP7_SetStatScript(stat_values, NULL, 1U);
    TEST_CHECK(TestP7_ProtectDrain() ==
               BMS_PROTECT_DRAIN_RETRY_REQUIRED);
    TEST_CHECK(BMS_Protect_GetXreadyState(&xready_state));
    TEST_CHECK(xready_state.xready_generation == 1UL);
    TEST_CHECK(xready_state.active);

    /* incomplete hook 同样保留 fault pending/uncleared。 */
    TestP7_StubReset();
    BMS_Protect_SetXreadyRecoveryHook(TestP7_RecoveryHook);
    TestP7_SetRecoveryResult(false);
    TestP7_SetStatScript(stat_values, NULL, 1U);
    TEST_CHECK(TestP7_ProtectDrain() ==
               BMS_PROTECT_DRAIN_RETRY_REQUIRED);
    TEST_CHECK(TestP7_RecoveryCallCount() == 1U);
    TEST_CHECK(TestP7_WriteCount() == 0U);

    /* 只有 complete hook 可 W1C XREADY；active 清除但 history 保留。 */
    TestP7_StubReset();
    BMS_Protect_SetXreadyRecoveryHook(TestP7_RecoveryHook);
    TestP7_SetRecoveryResult(true);
    TestP7_SetStatScript(stat_values, NULL, 2U);
    TEST_CHECK(TestP7_ProtectDrain() ==
               BMS_PROTECT_DRAIN_COMPLETE);
    TEST_CHECK(TestP7_RecoveryCallCount() == 1U);
    TEST_CHECK(TestP7_WriteCount() == 1U);
    TEST_CHECK(TestP7_WriteValue(0U) == BMS_PROTECT_STAT_DEVICE_XREADY);
    faults = BMS_Protect_GetFaultSummary();
    TEST_CHECK(!BMS_Fault_Contains(faults.active, BMS_FAULT_ID_AFE_XREADY));
    TEST_CHECK(BMS_Fault_Contains(faults.latched, BMS_FAULT_ID_AFE_XREADY));
    TEST_CHECK(BMS_Protect_GetXreadyState(&xready_state));
    TEST_CHECK(xready_state.xready_generation == 1UL);
    TEST_CHECK(!xready_state.active);

    /* 新 inactive→active 再推进 generation；recovery 只清 active，不倒退 epoch。 */
    TestP7_SetStatScript(stat_values, NULL, 2U);
    TEST_CHECK(TestP7_ProtectDrain() ==
               BMS_PROTECT_DRAIN_COMPLETE);
    TEST_CHECK(BMS_Protect_GetXreadyState(&xready_state));
    TEST_CHECK(xready_state.xready_generation == 2UL);
    TEST_CHECK(!xready_state.active);

    /* 明确拒绝的 XREADY W1C 保持同 generation active；bounded retry 可退休它，
     * 但成功前不能推进 generation 或暴露 inactive window。 */
    TestP7_StubReset();
    BMS_Protect_SetXreadyRecoveryHook(TestP7_RecoveryHook);
    TestP7_SetRecoveryResult(true);
    TEST_CHECK(TestP7_PushCcSample((int16_t)10));
    TEST_CHECK(BMS_Protect_GetLatestCc(&latest_cc));
    stat_values[0] = BMS_PROTECT_STAT_DEVICE_XREADY;
    stat_values[1] = BMS_PROTECT_STAT_DEVICE_XREADY;
    TestP7_SetStatScript(stat_values, NULL, 2U);
    TestP7_SetWriteFailure(BQ76940_STATUS_I2C_NACK, 1U);
    TEST_CHECK(TestP7_ProtectDrain() ==
               BMS_PROTECT_DRAIN_RETRY_REQUIRED);
    TEST_CHECK(BMS_Protect_GetXreadyState(&xready_state));
    TEST_CHECK(xready_state.xready_generation == 1UL);
    TEST_CHECK(xready_state.active);
    TEST_CHECK(!BMS_Protect_GetLatestCc(&latest_cc));
    TEST_CHECK(!latest_cc.valid);
    TEST_CHECK(TestP7_RecoveryCallCount() == 1U);
    TEST_CHECK(TestP7_WriteCount() == 1U);
    TEST_CHECK(TestP7_ProtectDrain() ==
               BMS_PROTECT_DRAIN_COMPLETE);
    TEST_CHECK(BMS_Protect_GetXreadyState(&xready_state));
    TEST_CHECK(xready_state.xready_generation == 1UL);
    TEST_CHECK(!xready_state.active);
    TEST_CHECK(!BMS_Protect_GetLatestCc(&latest_cc));
    TEST_CHECK(!latest_cc.valid);
    TEST_CHECK(TestP7_RecoveryCallCount() == 2U);
    TEST_CHECK(TestP7_WriteCount() == 2U);

    /* XREADY W1C ambiguous 时 active 保留，recovery/W1C 都不 replay；observed-low
     * 确认退休，history 仍留给 explicit-reset policy。 */
    TestP7_StubReset();
    BMS_Protect_SetXreadyRecoveryHook(TestP7_RecoveryHook);
    TestP7_SetRecoveryResult(true);
    TEST_CHECK(TestP7_PushCcSample((int16_t)20));
    TEST_CHECK(BMS_Protect_GetLatestCc(&latest_cc));
    stat_values[1] = 0U;
    TestP7_SetStatScript(stat_values, NULL, 2U);
    TestP7_SetWriteFailure(
        BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS, 1U);
    TEST_CHECK(TestP7_ProtectDrain() ==
               BMS_PROTECT_DRAIN_RETRY_REQUIRED);
    faults = BMS_Protect_GetFaultSummary();
    TEST_CHECK(BMS_Fault_Contains(faults.active,
                                  BMS_FAULT_ID_AFE_XREADY));
    TEST_CHECK(BMS_Fault_Contains(faults.active, BMS_FAULT_ID_AFE_COMM));
    diagnostics = BMS_Protect_GetDiagnostics();
    TEST_CHECK(diagnostics.w1c_finalization_ambiguous_mask ==
               BMS_PROTECT_STAT_DEVICE_XREADY);
    TEST_CHECK(BMS_Protect_GetXreadyState(&xready_state));
    TEST_CHECK(xready_state.xready_generation == 1UL);
    TEST_CHECK(xready_state.active);
    TEST_CHECK(!BMS_Protect_GetLatestCc(&latest_cc));
    TEST_CHECK(!latest_cc.valid);
    TEST_CHECK(TestP7_ProtectDrain() ==
               BMS_PROTECT_DRAIN_COMPLETE);
    TEST_CHECK(TestP7_RecoveryCallCount() == 1U);
    TEST_CHECK(TestP7_WriteCount() == 1U);
    faults = BMS_Protect_GetFaultSummary();
    TEST_CHECK(!BMS_Fault_Contains(faults.active, BMS_FAULT_ID_AFE_XREADY));
    TEST_CHECK(BMS_Fault_Contains(faults.latched, BMS_FAULT_ID_AFE_XREADY));
    TEST_CHECK(BMS_Protect_GetXreadyState(&xready_state));
    TEST_CHECK(xready_state.xready_generation == 1UL);
    TEST_CHECK(!xready_state.active);
    TEST_CHECK(!BMS_Protect_GetLatestCc(&latest_cc));
    TEST_CHECK(!latest_cc.valid);
    diagnostics = BMS_Protect_GetDiagnostics();
    TEST_CHECK(diagnostics.w1c_finalization_ambiguous_mask == 0U);

    return failures;
}

uint32_t Test_Phase7_BoundaryContracts(void)
{
    uint32_t failures;
    uint32_t suspend_before;
    uint32_t resume_before;
    uint8_t stat_values[2];
    uint8_t clear_mask;
    BMS_FaultBitmap_t active_before;
    BMS_FaultBitmap_t latched_before;
    BMS_FaultSummary_t faults;
    BQ76940_FetRequest_t request;

    failures = 0UL;

    /* event capture 后的 SYS_STAT=0 不清 recovery state。OV/UV/OCD 可恢复但不锁
     * history；SCD/OVRD active+latched 且不自动清。 */
    TestP7_StubReset();
    stat_values[0] = BMS_PROTECT_STAT_OV | BMS_PROTECT_STAT_UV |
                     BMS_PROTECT_STAT_OCD | BMS_PROTECT_STAT_SCD |
                     BMS_PROTECT_STAT_OVRD_ALERT;
    stat_values[1] = 0U;
    TestP7_SetStatScript(stat_values, NULL, 2U);
    TEST_CHECK(TestP7_ProtectDrain() ==
               BMS_PROTECT_DRAIN_COMPLETE);

    /* getter 必须包围 active+latched 两个 word 的复制；static verifier 检查正式顺序。 */
    suspend_before = TestP7_SchedulerSuspendCount();
    resume_before = TestP7_SchedulerResumeCount();
    faults = BMS_Protect_GetFaultSummary();
    TEST_CHECK(TestP7_SchedulerSuspendCount() == (suspend_before + 1UL));
    TEST_CHECK(TestP7_SchedulerResumeCount() == (resume_before + 1UL));
    TEST_CHECK(TestP7_SchedulerProtectionBalanced());

    TEST_CHECK(BMS_Fault_Contains(faults.active, BMS_FAULT_ID_HW_OV));
    TEST_CHECK(BMS_Fault_Contains(faults.active, BMS_FAULT_ID_HW_UV));
    TEST_CHECK(BMS_Fault_Contains(faults.active, BMS_FAULT_ID_HW_OCD));
    TEST_CHECK(BMS_Fault_Contains(faults.active, BMS_FAULT_ID_HW_SCD));
    TEST_CHECK(BMS_Fault_Contains(faults.active,
                                  BMS_FAULT_ID_AFE_OVRD_ALERT));
    TEST_CHECK(!BMS_Fault_Contains(faults.latched, BMS_FAULT_ID_HW_OV));
    TEST_CHECK(!BMS_Fault_Contains(faults.latched, BMS_FAULT_ID_HW_UV));
    TEST_CHECK(!BMS_Fault_Contains(faults.latched, BMS_FAULT_ID_HW_OCD));
    TEST_CHECK(BMS_Fault_Contains(faults.latched, BMS_FAULT_ID_HW_SCD));
    TEST_CHECK(BMS_Fault_Contains(faults.latched,
                                  BMS_FAULT_ID_AFE_OVRD_ALERT));

    /* pure decision API 只捕获；输入 zero status 不能作为 recovery shortcut。 */
    active_before = faults.active;
    latched_before = faults.latched;
    request.chg = BQ76940_FET_DESIRE_ENABLE;
    request.dsg = BQ76940_FET_DESIRE_ENABLE;
    clear_mask = 0xFFU;
    BMS_Protect_Decide(0U, &faults, &request, &clear_mask);
    TEST_CHECK(faults.active == active_before);
    TEST_CHECK(faults.latched == latched_before);
    TEST_CHECK(clear_mask == 0U);

    return failures;
}

uint32_t Test_Phase7_SimCommPolicy(void)
{
    uint32_t failures;
    uint8_t stat_values[1];
    BQ76940_Status_t stat_statuses[1];
    BMS_Policy_t policy;
    BMS_ProtectSafetySnapshot_t safety;

    failures = 0UL;
    (void)memset(&policy, 0, sizeof(policy));
    policy.afe_comm.consecutive_failures_to_active = 3U;
    policy.afe_comm.no_success_timeout_ms = 1000UL;
    policy.afe_comm.continuous_latch_ms = 5000UL;
    policy.afe_comm.consecutive_successes_to_recover = 3U;
    policy.ocd_escalation.event_count_to_latch = 3U;
    policy.ocd_escalation.event_window_ms = 60000UL;

    TestP7_StubReset();
    BMS_Protect_SetPolicy(&policy, 0UL);
    stat_values[0] = 0U;
    stat_statuses[0] = BQ76940_STATUS_I2C_NACK;
    TestP7_SetStatScript(stat_values, stat_statuses, 1U);
    TEST_CHECK(TestP7_ProtectDrain() ==
               BMS_PROTECT_DRAIN_RETRY_REQUIRED);
    safety = BMS_Protect_GetSafetySnapshot();
    TEST_CHECK(!BMS_Fault_Contains(safety.faults.active,
                                   BMS_FAULT_ID_AFE_COMM));
    TestP7_SetStatScript(stat_values, stat_statuses, 1U);
    TEST_CHECK(TestP7_ProtectDrain() ==
               BMS_PROTECT_DRAIN_RETRY_REQUIRED);
    safety = BMS_Protect_GetSafetySnapshot();
    TEST_CHECK(!BMS_Fault_Contains(safety.faults.active,
                                   BMS_FAULT_ID_AFE_COMM));
    TestP7_SetStatScript(stat_values, stat_statuses, 1U);
    TEST_CHECK(TestP7_ProtectDrain() ==
               BMS_PROTECT_DRAIN_RETRY_REQUIRED);
    safety = BMS_Protect_GetSafetySnapshot();
    TEST_CHECK(BMS_Fault_Contains(safety.faults.active,
                                  BMS_FAULT_ID_AFE_COMM));

    BMS_Protect_TestUpdateAfeCommPolicy(6000UL);
    safety = BMS_Protect_GetSafetySnapshot();
    TEST_CHECK(BMS_Fault_Contains(safety.faults.latched,
                                  BMS_FAULT_ID_AFE_COMM));

    stat_statuses[0] = BQ76940_STATUS_OK;
    TestP7_SetStatScript(stat_values, stat_statuses, 1U);
    TEST_CHECK(TestP7_ProtectDrain() ==
               BMS_PROTECT_DRAIN_COMPLETE);
    TestP7_SetStatScript(stat_values, stat_statuses, 1U);
    TEST_CHECK(TestP7_ProtectDrain() ==
               BMS_PROTECT_DRAIN_COMPLETE);
    safety = BMS_Protect_GetSafetySnapshot();
    TEST_CHECK(BMS_Fault_Contains(safety.faults.active,
                                  BMS_FAULT_ID_AFE_COMM));
    TestP7_SetStatScript(stat_values, stat_statuses, 1U);
    TEST_CHECK(TestP7_ProtectDrain() ==
               BMS_PROTECT_DRAIN_COMPLETE);
    safety = BMS_Protect_GetSafetySnapshot();
    TEST_CHECK(!BMS_Fault_Contains(safety.faults.active,
                                   BMS_FAULT_ID_AFE_COMM));
    TEST_CHECK(BMS_Fault_Contains(safety.faults.latched,
                                  BMS_FAULT_ID_AFE_COMM));
    return failures;
}
