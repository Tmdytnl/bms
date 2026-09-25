#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "fml_fet_manager.h"
#include "fml_balance.h"
#include "fml_health.h"
#include "fml_hw_recovery.h"
#include "fml_policy.h"
#include "fml_recovery.h"
#include "fml_state.h"
#include "test_phase9_stub.h"

volatile uint32_t g_phase9_logic_failures;
volatile uint32_t g_phase9_fet_failures;
volatile uint32_t g_phase9_recovery_failures;
volatile uint32_t g_phase9_health_failures;
volatile uint32_t g_phase9_hw_handshake_failures;
volatile uint32_t g_phase9_scenarios_completed;
volatile uint32_t g_phase9_races_completed;

#define TEST_CHECK_IN(counter_, expr_) \
    do                                  \
    {                                   \
        if (!(expr_))                   \
        {                               \
            ++(counter_);               \
        }                               \
    } while (0)

static void TestP9_InitEngine(BMS_StateEngine_t *engine,
                              uint32_t now_ms)
{
    (void)memset(engine, 0, sizeof(*engine));
    engine->state = BMS_STATE_INIT;
    engine->transition_candidate = BMS_STATE_STANDBY;
    engine->init_started_ms = now_ms;
    engine->data_stale_active = true;
    engine->initialized = true;
}

static void TestP9_FreshMeasurement(BMS_DataSnapshot_t *measurement,
                                    uint32_t sequence,
                                    int32_t current_ma,
                                    int16_t temperature_decic,
                                    uint16_t cell_mv)
{
    uint32_t index;

    (void)memset(measurement, 0, sizeof(*measurement));
    for (index = 0UL; index < (uint32_t)BMS_CELL_COUNT; ++index)
    {
        measurement->cell_voltage_mv[index] = cell_mv;
        measurement->cell_metadata.age_ms[index] = 0UL;
    }
    measurement->cell_metadata.valid_bitmap = BMS_CELL_DEFINED_MASK;
    measurement->cell_metadata.in_range_bitmap = BMS_CELL_DEFINED_MASK;
    measurement->cell_metadata.stale_bitmap = 0U;
    measurement->pack_metadata.valid = true;
    measurement->pack_metadata.in_range = true;
    measurement->current_metadata.valid = true;
    measurement->current_metadata.in_range = true;
    measurement->temperature_metadata.valid = true;
    measurement->temperature_metadata.in_range = true;
    measurement->current_ma = current_ma;
    measurement->temperature_decic = temperature_decic;
    measurement->sample_sequence = sequence;
    measurement->afe_generation = 0UL;
}

static uint32_t TestP9_StateAndProtection(void)
{
    const BMS_Policy_t *policy;
    BMS_StateEngine_t engine;
    BMS_DataSnapshot_t measurement;
    BMS_StateSafetySnapshot_t decision;
    uint32_t failures;

    failures = 0UL;
    policy = FML_Policy_Get();
    TEST_CHECK_IN(failures, FML_Policy_Validate(policy));

    TestP9_InitEngine(&engine, 0UL);
    TestP9_FreshMeasurement(&measurement, 1UL, 0, 250, 3700U);
    TEST_CHECK_IN(failures, FML_State_Evaluate(
        &engine, policy, &measurement, 0UL, true, false, &decision));
    TEST_CHECK_IN(failures,
        FML_Fault_Contains(decision.faults.active,
                           BMS_FAULT_ID_DATA_STALE));
    measurement.sample_sequence = 2UL;
    TEST_CHECK_IN(failures, FML_State_Evaluate(
        &engine, policy, &measurement, 100UL, true, false, &decision));
    TEST_CHECK_IN(failures, decision.state == BMS_STATE_STANDBY);
    TEST_CHECK_IN(failures, decision.inhibit_chg_reasons == 0UL);
    TEST_CHECK_IN(failures, decision.inhibit_dsg_reasons == 0UL);
    g_phase9_scenarios_completed += 2UL; /* SIM-01, SIM-02 */

    measurement.current_ma = 400;
    measurement.sample_sequence = 3UL;
    (void)FML_State_Evaluate(&engine, policy, &measurement,
                             200UL, true, false, &decision);
    measurement.sample_sequence = 4UL;
    (void)FML_State_Evaluate(&engine, policy, &measurement,
                             700UL, true, false, &decision);
    TEST_CHECK_IN(failures, decision.state == BMS_STATE_CHARGE);
    g_phase9_scenarios_completed += 1UL; /* SIM-03 */

    TestP9_InitEngine(&engine, 0UL);
    engine.data_stale_active = false;
    measurement.current_ma = -400;
    measurement.sample_sequence = 5UL;
    (void)FML_State_Evaluate(&engine, policy, &measurement,
                             0UL, true, false, &decision);
    measurement.sample_sequence = 6UL;
    (void)FML_State_Evaluate(&engine, policy, &measurement,
                             500UL, true, false, &decision);
    TEST_CHECK_IN(failures, decision.state == BMS_STATE_DISCHARGE);
    g_phase9_scenarios_completed += 1UL; /* SIM-04 */

    TestP9_InitEngine(&engine, 0UL);
    engine.data_stale_active = false;
    TestP9_FreshMeasurement(&measurement, 10UL, 0, 250, 4200U);
    (void)FML_State_Evaluate(&engine, policy, &measurement,
                             0UL, true, false, &decision);
    (void)FML_State_Evaluate(&engine, policy, &measurement,
                             500UL, true, false, &decision);
    TEST_CHECK_IN(failures,
        FML_Fault_Contains(decision.faults.active, BMS_FAULT_ID_SW_OV));
    TEST_CHECK_IN(failures, decision.inhibit_chg_reasons != 0UL);
    TEST_CHECK_IN(failures, decision.inhibit_dsg_reasons == 0UL);
    g_phase9_scenarios_completed += 1UL; /* SIM-05 */

    TestP9_InitEngine(&engine, 0UL);
    engine.data_stale_active = false;
    TestP9_FreshMeasurement(&measurement, 11UL, 0, 250, 3000U);
    (void)FML_State_Evaluate(&engine, policy, &measurement,
                             0UL, true, false, &decision);
    (void)FML_State_Evaluate(&engine, policy, &measurement,
                             500UL, true, false, &decision);
    TEST_CHECK_IN(failures,
        FML_Fault_Contains(decision.faults.active, BMS_FAULT_ID_SW_UV));
    TEST_CHECK_IN(failures, decision.inhibit_chg_reasons == 0UL);
    TEST_CHECK_IN(failures, decision.inhibit_dsg_reasons != 0UL);
    g_phase9_scenarios_completed += 1UL; /* SIM-07 */

    TestP9_InitEngine(&engine, 0UL);
    engine.data_stale_active = false;
    TestP9_FreshMeasurement(&measurement, 12UL, 5000, 250, 3700U);
    (void)FML_State_Evaluate(&engine, policy, &measurement,
                             0UL, true, false, &decision);
    (void)FML_State_Evaluate(&engine, policy, &measurement,
                             500UL, true, false, &decision);
    TEST_CHECK_IN(failures,
        FML_Fault_Contains(decision.faults.active,
                           BMS_FAULT_ID_SW_OC_CHARGE));
    g_phase9_scenarios_completed += 1UL; /* SIM-09 */

    TestP9_InitEngine(&engine, 0UL);
    engine.data_stale_active = false;
    measurement.current_ma = -10000;
    (void)FML_State_Evaluate(&engine, policy, &measurement,
                             0UL, true, false, &decision);
    (void)FML_State_Evaluate(&engine, policy, &measurement,
                             500UL, true, false, &decision);
    TEST_CHECK_IN(failures,
        FML_Fault_Contains(decision.faults.active,
                           BMS_FAULT_ID_SW_OC_DISCHARGE));
    g_phase9_scenarios_completed += 1UL; /* SIM-10 */

    TestP9_InitEngine(&engine, 0UL);
    engine.data_stale_active = false;
    TestP9_FreshMeasurement(&measurement, 13UL, 0, 0, 3700U);
    (void)FML_State_Evaluate(&engine, policy, &measurement,
                             0UL, true, false, &decision);
    (void)FML_State_Evaluate(&engine, policy, &measurement,
                             1000UL, true, false, &decision);
    TEST_CHECK_IN(failures, decision.inhibit_chg_reasons != 0UL);
    TEST_CHECK_IN(failures, decision.inhibit_dsg_reasons == 0UL);
    g_phase9_scenarios_completed += 1UL; /* SIM-13 */

    TestP9_InitEngine(&engine, 0UL);
    engine.data_stale_active = false;
    measurement.temperature_decic = 600;
    (void)FML_State_Evaluate(&engine, policy, &measurement,
                             0UL, true, false, &decision);
    (void)FML_State_Evaluate(&engine, policy, &measurement,
                             1000UL, true, false, &decision);
    TEST_CHECK_IN(failures, decision.inhibit_chg_reasons != 0UL);
    TEST_CHECK_IN(failures, decision.inhibit_dsg_reasons != 0UL);
    g_phase9_scenarios_completed += 3UL; /* SIM-14, SIM-15, SIM-16 */

    measurement.current_metadata.age_ms = 1001UL;
    (void)FML_State_Evaluate(&engine, policy, &measurement,
                             1100UL, true, false, &decision);
    TEST_CHECK_IN(failures,
        FML_Fault_Contains(decision.faults.active,
                           BMS_FAULT_ID_DATA_STALE));
    TEST_CHECK_IN(failures, decision.inhibit_chg_reasons != 0UL &&
                            decision.inhibit_dsg_reasons != 0UL);
    g_phase9_scenarios_completed += 1UL; /* SIM-17 */

    TestP9_InitEngine(&engine, UINT32_MAX - 249UL);
    engine.data_stale_active = false;
    TestP9_FreshMeasurement(&measurement, 20UL, 0, 250, 4200U);
    (void)FML_State_Evaluate(&engine, policy, &measurement,
                             UINT32_MAX - 249UL, true, false, &decision);
    (void)FML_State_Evaluate(&engine, policy, &measurement,
                             250UL, true, false, &decision);
    TEST_CHECK_IN(failures, engine.sw_ov.active);
    g_phase9_scenarios_completed += 1UL; /* SIM-32 */

    return failures;
}

static void TestP9_PublishIntent(bool chg, bool dsg)
{
    BMS_StateSafetySnapshot_t decision;

    (void)memset(&decision, 0, sizeof(decision));
    decision.evaluated_sample_sequence = 1UL;
    decision.evaluated_afe_generation = 0UL;
    decision.state = BMS_STATE_STANDBY;
    decision.technical_ready = true;
    decision.operational_intent.chg = chg ?
        BQ76940_FET_DESIRE_ENABLE : BQ76940_FET_DESIRE_DISABLE;
    decision.operational_intent.dsg = dsg ?
        BQ76940_FET_DESIRE_ENABLE : BQ76940_FET_DESIRE_DISABLE;
    (void)FML_State_PublishIfCurrent(&decision);
}

static void TestP9_ProtectRevisionRace(void)
{
    BMS_ProtectSafetySnapshot_t protect;

    protect = FML_Protect_GetSafetySnapshot();
    ++protect.publication_revision;
    protect.inhibit_chg_reasons = BMS_INHIBIT_REASON_UNKNOWN_SOURCE;
    TestP9_SetProtectSnapshot(&protect);
}

static void TestP9_CorruptReadback(void)
{
    TestP9_SetReadbackCorruption(true);
}

static uint32_t TestP9_FetManager(void)
{
    const BMS_Policy_t *policy;
    BMS_ProtectSafetySnapshot_t protect;
    BMS_FetManagerSnapshot_t fet;
    uint32_t failures;

    failures = 0UL;
    policy = FML_Policy_Get();
    TestP9_StubReset();
    (void)memset(&protect, 0, sizeof(protect));
    protect.publication_revision = 1UL;
    protect.xready_generation = 0UL;
    TestP9_SetProtectSnapshot(&protect);
    FML_State_Init(policy, 0UL);
    TestP9_PublishIntent(true, false);
    FML_Recovery_Init(TestP9_GetDevice(), policy);
    FML_FetManager_Init(TestP9_GetDevice());
    FML_FetManager_Service();
    fet = FML_FetManager_GetSnapshot();
    TEST_CHECK_IN(failures,
        fet.transaction_state == BMS_FET_TRANSACTION_CONFIRMED_APPLIED);
    TEST_CHECK_IN(failures, (TestP9_GetRegister(0x05U) & 0x03U) == 0x01U);

    protect.inhibit_chg_reasons =
        BMS_INHIBIT_REASON_FAULT(BMS_FAULT_ID_HW_OV);
    ++protect.publication_revision;
    TestP9_SetProtectSnapshot(&protect);
    TestP9_PublishIntent(true, true);
    FML_FetManager_Service();
    TEST_CHECK_IN(failures, (TestP9_GetRegister(0x05U) & 0x03U) == 0x02U);
    g_phase9_scenarios_completed += 5UL; /* SIM-06/08/11/12/31 */

    protect.inhibit_chg_reasons = 0UL;
    ++protect.publication_revision;
    TestP9_SetProtectSnapshot(&protect);
    FML_FetManager_TestSetAfterReadHook(TestP9_ProtectRevisionRace);
    FML_FetManager_Service();
    FML_FetManager_TestSetAfterReadHook(NULL);
    fet = FML_FetManager_GetSnapshot();
    TEST_CHECK_IN(failures, !fet.observed.chg_on && !fet.observed.dsg_on);
    ++g_phase9_races_completed;

    TestP9_StubReset();
    TestP9_SetProtectSnapshot(&protect);
    FML_State_Init(policy, 0UL);
    TestP9_PublishIntent(true, false);
    FML_Recovery_Init(TestP9_GetDevice(), policy);
    FML_FetManager_Init(TestP9_GetDevice());
    TestP9_SetNextWriteStatus(
        BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS);
    FML_FetManager_Service();
    fet = FML_FetManager_GetSnapshot();
    TEST_CHECK_IN(failures,
        fet.transaction_state == BMS_FET_TRANSACTION_QUARANTINED);
    TEST_CHECK_IN(failures, (TestP9_GetRegister(0x05U) & 0x03U) == 0U);
    FML_FetManager_Service();
    fet = FML_FetManager_GetSnapshot();
    TEST_CHECK_IN(failures, fet.enable_denied_by_quarantine);
    g_phase9_scenarios_completed += 1UL; /* SIM-22 */

    TestP9_StubReset();
    TestP9_SetProtectSnapshot(&protect);
    FML_State_Init(policy, 0UL);
    TestP9_PublishIntent(true, false);
    FML_Recovery_Init(TestP9_GetDevice(), policy);
    FML_FetManager_Init(TestP9_GetDevice());
    FML_FetManager_TestSetAfterWriteHook(TestP9_CorruptReadback);
    FML_FetManager_Service();
    FML_FetManager_TestSetAfterWriteHook(NULL);
    fet = FML_FetManager_GetSnapshot();
    TEST_CHECK_IN(failures,
        fet.transaction_state == BMS_FET_TRANSACTION_QUARANTINED);

    TestP9_StubReset();
    TestP9_SetProtectSnapshot(&protect);
    FML_State_Init(policy, 0UL);
    TestP9_PublishIntent(true, false);
    FML_Recovery_Init(TestP9_GetDevice(), policy);
    FML_FetManager_Init(TestP9_GetDevice());
    TestP9_SetNextReadStatus(BQ76940_STATUS_I2C_TIMEOUT);
    FML_FetManager_Service();
    fet = FML_FetManager_GetSnapshot();
    TEST_CHECK_IN(failures,
        fet.transaction_state == BMS_FET_TRANSACTION_UNVERIFIED);
    return failures;
}

static void TestP9_HandoffGenerationRace(void)
{
    TestP9_SetXready(2UL, true);
}

static bool TestP9_AdvanceRecoveryToHandoff(uint32_t *now_ms)
{
    BMS_RecoverySnapshot_t recovery;
    uint32_t limit;

    limit = 0UL;
    do
    {
        *now_ms = (uint32_t)(*now_ms + 100UL);
        FML_Recovery_Service(*now_ms);
        recovery = FML_Recovery_GetSnapshot();
        ++limit;
    } while ((recovery.phase != BMS_RECOVERY_PHASE_CALIBRATION_HANDOFF) &&
             (recovery.phase != BMS_RECOVERY_PHASE_FAILED) &&
             (limit < 80UL));
    return recovery.phase == BMS_RECOVERY_PHASE_CALIBRATION_HANDOFF;
}

static uint32_t TestP9_Recovery(void)
{
    const BMS_Policy_t *policy;
    BMS_RecoverySnapshot_t recovery;
    uint32_t failures;
    uint32_t now_ms;

    failures = 0UL;
    policy = FML_Policy_Get();
    TestP9_StubReset();
    TestP9_SetIdentity(UINT32_MAX, 0UL);
    TestP9_SetXready(1UL, true);
    FML_Recovery_Init(TestP9_GetDevice(), policy);
    FML_Recovery_Service(0UL);
    FML_Balance_Init(TestP9_GetDevice(), policy, false);
    FML_Balance_RunOnce(50UL);
    FML_FetManager_Init(TestP9_GetDevice());
    FML_FetManager_Service();
    recovery = FML_Recovery_GetSnapshot();
    TEST_CHECK_IN(failures, recovery.recovery_in_progress);
    TEST_CHECK_IN(failures, recovery.inhibit_chg_reasons != 0UL &&
                            recovery.inhibit_dsg_reasons != 0UL);
    FML_Recovery_Service(100UL);
    FML_Recovery_Service(200UL);
    recovery = FML_Recovery_GetSnapshot();
    TestP9_SetXready(1UL, false);
    TestP9_SetXreadyAck(1UL, recovery.recovery_revision, true, false);
    FML_Recovery_Service(300UL);
    now_ms = 300UL;
    TEST_CHECK_IN(failures, TestP9_AdvanceRecoveryToHandoff(&now_ms));
    FML_Recovery_Service((uint32_t)(now_ms + 100UL));
    recovery = FML_Recovery_GetSnapshot();
    TEST_CHECK_IN(failures,
        recovery.phase == BMS_RECOVERY_PHASE_WAIT_FIRST_VALID_SAMPLE);
    TestP9_SetIdentity(0UL, 1UL); /* sequence 自然回绕 */
    FML_Recovery_Service((uint32_t)(now_ms + 200UL));
    recovery = FML_Recovery_GetSnapshot();
    TEST_CHECK_IN(failures, recovery.phase == BMS_RECOVERY_PHASE_COMPLETE);
    TEST_CHECK_IN(failures, recovery.technical_ready);
    TEST_CHECK_IN(failures, TestP9_GetCalibrationHandoffCount() == 1UL);
    g_phase9_scenarios_completed += 1UL; /* SIM-20 */

    TestP9_StubReset();
    TestP9_SetXready(1UL, true);
    FML_Recovery_Init(TestP9_GetDevice(), policy);
    FML_Recovery_Service(0UL);
    FML_Balance_Init(TestP9_GetDevice(), policy, false);
    FML_Balance_RunOnce(50UL);
    FML_FetManager_Init(TestP9_GetDevice());
    FML_FetManager_Service();
    FML_Recovery_Service(100UL);
    FML_Recovery_Service(200UL);
    recovery = FML_Recovery_GetSnapshot();
    TestP9_SetXready(1UL, false);
    TestP9_SetXreadyAck(1UL, recovery.recovery_revision, true, false);
    FML_Recovery_Service(300UL);
    FML_Recovery_Service(400UL);
    TestP9_SetXready(2UL, true);
    FML_Recovery_Service(500UL);
    recovery = FML_Recovery_GetSnapshot();
    TEST_CHECK_IN(failures, recovery.xready_generation == 2UL);
    TEST_CHECK_IN(failures,
        recovery.phase == BMS_RECOVERY_PHASE_PRE_CLEAR_PREPARE);
    TEST_CHECK_IN(failures,
        TestP9_GetCalibrationInvalidationCount() >= 2UL);
    g_phase9_scenarios_completed += 1UL; /* SIM-21 */

    TestP9_StubReset();
    TestP9_SetXready(1UL, true);
    FML_Recovery_Init(TestP9_GetDevice(), policy);
    FML_Recovery_Service(0UL);
    FML_Balance_Init(TestP9_GetDevice(), policy, false);
    FML_Balance_RunOnce(50UL);
    FML_FetManager_Init(TestP9_GetDevice());
    FML_FetManager_Service();
    FML_Recovery_Service(100UL);
    FML_Recovery_Service(200UL);
    recovery = FML_Recovery_GetSnapshot();
    TestP9_SetXready(1UL, false);
    TestP9_SetXreadyAck(1UL, recovery.recovery_revision, true, false);
    FML_Recovery_Service(300UL);
    now_ms = 300UL;
    TEST_CHECK_IN(failures, TestP9_AdvanceRecoveryToHandoff(&now_ms));
    FML_Recovery_TestSetPreHandoffHook(TestP9_HandoffGenerationRace);
    FML_Recovery_Service((uint32_t)(now_ms + 100UL));
    FML_Recovery_TestSetPreHandoffHook(NULL);
    recovery = FML_Recovery_GetSnapshot();
    TEST_CHECK_IN(failures, !recovery.calibration_handed_off);
    FML_Recovery_Service((uint32_t)(now_ms + 200UL));
    recovery = FML_Recovery_GetSnapshot();
    TEST_CHECK_IN(failures, recovery.xready_generation == 2UL);
    ++g_phase9_races_completed;
    return failures;
}

static uint32_t TestP9_Health(void)
{
    const BMS_Policy_t *policy;
    BMS_HealthMonitor_t monitor;
    BMS_HealthDecision_t decision;
    BMS_HealthTaskId_t task;
    uint32_t failures;

    failures = 0UL;
    policy = FML_Policy_Get();
    FML_Health_Init();
    FML_Health_MonitorInit(&monitor, 0UL);
    for (task = BMS_HEALTH_TASK_PROTECT;
         task < BMS_HEALTH_TASK_COUNT;
         task = (BMS_HealthTaskId_t)((uint32_t)task + 1UL))
    {
        FML_Health_Heartbeat(task);
    }
    decision = FML_Health_Evaluate(&monitor, &policy->health, 100UL);
    TEST_CHECK_IN(failures, decision.all_tasks_advanced);
    TEST_CHECK_IN(failures, decision.watchdog_armed);
    TEST_CHECK_IN(failures, decision.feed_allowed);
    for (task = BMS_HEALTH_TASK_SAMPLE;
         task < BMS_HEALTH_TASK_COUNT;
         task = (BMS_HealthTaskId_t)((uint32_t)task + 1UL))
    {
        FML_Health_Heartbeat(task);
    }
    decision = FML_Health_Evaluate(&monitor, &policy->health, 401UL);
    TEST_CHECK_IN(failures, decision.rtos_health_fault);
    TEST_CHECK_IN(failures, !decision.feed_allowed);
    TEST_CHECK_IN(failures,
        (decision.stale_task_bitmap &
         ((uint32_t)1UL << BMS_HEALTH_TASK_PROTECT)) != 0UL);
    g_phase9_scenarios_completed += 2UL; /* SIM-23, SIM-24 */
    return failures;
}

static uint32_t TestP9_HwHandshakeQualification(void)
{
    const BMS_Policy_t *policy;
    BMS_HwRecoveryEngine_t engine;
    BMS_ProtectSafetySnapshot_t protect;
    BMS_DataSnapshot_t measurement;
    BMS_ProtectHwRecoveryRequest_t request;
    uint32_t failures;

    failures = 0UL;
    policy = FML_Policy_Get();
    FML_HwRecovery_Init(&engine);
    (void)memset(&protect, 0, sizeof(protect));
    protect.faults.active = FML_Fault_Mask(BMS_FAULT_ID_HW_OV);
    protect.source_generation[BMS_PROTECT_SOURCE_HW_OV] = 1UL;
    TestP9_FreshMeasurement(&measurement, 50UL, 0, 250, 4100U);
    TEST_CHECK_IN(failures, !FML_HwRecovery_Evaluate(
        &engine, policy, &protect, &measurement, 0UL, &request));
    TEST_CHECK_IN(failures, FML_HwRecovery_Evaluate(
        &engine, policy, &protect, &measurement, 2000UL, &request));
    TEST_CHECK_IN(failures, request.valid);
    TEST_CHECK_IN(failures, request.fault_id == BMS_FAULT_ID_HW_OV);
    TEST_CHECK_IN(failures, request.expected_source_generation == 1UL);
    ++protect.source_generation[BMS_PROTECT_SOURCE_HW_OV];
    TEST_CHECK_IN(failures, !FML_HwRecovery_Evaluate(
        &engine, policy, &protect, &measurement, 2100UL, &request));
    ++g_phase9_races_completed;
    return failures;
}

uint32_t Test_Phase9(void)
{
    uint32_t failures;

    g_phase9_scenarios_completed = 0UL;
    g_phase9_races_completed = 0UL;
    TestP9_StubReset();
    g_phase9_logic_failures = TestP9_StateAndProtection();
    g_phase9_fet_failures = TestP9_FetManager();
    g_phase9_recovery_failures = TestP9_Recovery();
    g_phase9_health_failures = TestP9_Health();
    g_phase9_hw_handshake_failures =
        TestP9_HwHandshakeQualification();
    failures = g_phase9_logic_failures + g_phase9_fet_failures +
        g_phase9_recovery_failures + g_phase9_health_failures +
        g_phase9_hw_handshake_failures;
    return failures;
}
