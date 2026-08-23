#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bms_balance.h"
#include "bms_can.h"
#include "bms_persistence.h"
#include "bms_soc.h"
#include "bq76940_regs.h"
#include "test_phase9_stub.h"

volatile uint32_t g_phase10_test_failures;
volatile uint32_t g_phase10_test_completed;
volatile uint32_t g_phase10_soc_failures;
volatile uint32_t g_phase10_balance_failures;
volatile uint32_t g_phase11_can_failures;
volatile uint32_t g_storage_codec_failures;
volatile uint32_t g_continuation_scenarios_completed;

#define TEST_CONT_CHECK(counter_, expression_) \
    do                                          \
    {                                           \
        if (!(expression_))                     \
        {                                       \
            ++(counter_);                       \
        }                                       \
    } while (0)

static void TestContinuation_FreshMeasurement(
    BMS_DataSnapshot_t *measurement,
    uint16_t cell_mv,
    int32_t current_ma,
    int16_t temperature_decic,
    uint32_t sequence,
    uint32_t generation)
{
    uint8_t index;

    (void)memset(measurement, 0, sizeof(*measurement));
    for (index = 0U; index < BMS_CELL_COUNT; ++index)
    {
        measurement->cell_voltage_mv[index] = cell_mv;
        measurement->cell_metadata.age_ms[index] = 0UL;
    }
    measurement->cell_metadata.valid_bitmap = BMS_CELL_DEFINED_MASK;
    measurement->cell_metadata.in_range_bitmap = BMS_CELL_DEFINED_MASK;
    measurement->pack_metadata.valid = true;
    measurement->current_metadata.valid = true;
    measurement->current_metadata.in_range = true;
    measurement->current_metadata.age_ms = 0UL;
    measurement->temperature_metadata.valid = true;
    measurement->temperature_metadata.in_range = true;
    measurement->temperature_metadata.age_ms = 0UL;
    measurement->current_ma = current_ma;
    measurement->temperature_decic = temperature_decic;
    measurement->sample_sequence = sequence;
    measurement->afe_generation = generation;
}

static uint32_t TestContinuation_Soc(void)
{
    const BMS_Policy_t *policy;
    BMS_DataSnapshot_t measurement;
    BMS_SocEngine_t engine;
    BMS_SocSnapshot_t snapshot;
    int64_t before;
    uint32_t failures;

    failures = 0UL;
    policy = BMS_Policy_Get();
    TestContinuation_FreshMeasurement(
        &measurement, 3700U, 0, 250, 1UL, 0UL);
    TEST_CONT_CHECK(failures, BMS_Soc_EngineInit(
        &engine, &policy->soc, &measurement, 0UL));
    snapshot = BMS_Soc_GetEngineSnapshot(&engine, &policy->soc);
    TEST_CONT_CHECK(failures, snapshot.soc_permille == 500U);

    TEST_CONT_CHECK(failures, BMS_Soc_IntegrateCurrent(
        &engine, &policy->soc, 1000, 0UL, 0UL));
    before = engine.remaining_mams;
    TEST_CONT_CHECK(failures, BMS_Soc_IntegrateCurrent(
        &engine, &policy->soc, 1000, 1000UL, 0UL));
    TEST_CONT_CHECK(failures,
        engine.remaining_mams == (before + 995000LL));
    before = engine.remaining_mams;
    TEST_CONT_CHECK(failures, BMS_Soc_IntegrateCurrent(
        &engine, &policy->soc, 0, 2000UL, 0UL));
    TEST_CONT_CHECK(failures, engine.remaining_mams == before);
    ++g_continuation_scenarios_completed; /* SIM-26 */

    before = engine.remaining_mams;
    TEST_CONT_CHECK(failures, BMS_Soc_IntegrateCurrent(
        &engine, &policy->soc, -1000, 3000UL, 0UL));
    TEST_CONT_CHECK(failures,
        engine.remaining_mams == (before - 1000000LL));
    ++g_continuation_scenarios_completed; /* SIM-27 */

    engine.remaining_mams =
        ((int64_t)policy->soc.capacity_mah * 3600000LL) - 1LL;
    (void)BMS_Soc_IntegrateCurrent(
        &engine, &policy->soc, INT32_MAX, 4000UL, 0UL);
    snapshot = BMS_Soc_GetEngineSnapshot(&engine, &policy->soc);
    TEST_CONT_CHECK(failures, snapshot.soc_permille == 1000U);

    engine.last_sample_ms = UINT32_MAX - 499UL;
    engine.have_sample_time = true;
    before = engine.remaining_mams;
    TEST_CONT_CHECK(failures, BMS_Soc_IntegrateCurrent(
        &engine, &policy->soc, -1000, 500UL, 0UL));
    TEST_CONT_CHECK(failures,
        engine.remaining_mams == (before - 1000000LL));

    BMS_Soc_MarkQueueGap(&engine);
    snapshot = BMS_Soc_GetEngineSnapshot(&engine, &policy->soc);
    TEST_CONT_CHECK(failures, !snapshot.valid &&
                                snapshot.queue_gap_latched);

    measurement.current_ma = 100;
    measurement.cell_voltage_mv[0] = policy->soc.full_cell_mv;
    {
        uint8_t index;
        for (index = 1U; index < BMS_CELL_COUNT; ++index)
        {
            measurement.cell_voltage_mv[index] = policy->soc.full_cell_mv;
        }
    }
    BMS_Soc_ObserveCorrection(&engine, &policy->soc, &measurement, 0UL);
    BMS_Soc_ObserveCorrection(&engine, &policy->soc, &measurement,
                              policy->soc.full_qualify_ms);
    snapshot = BMS_Soc_GetEngineSnapshot(&engine, &policy->soc);
    TEST_CONT_CHECK(failures, snapshot.valid &&
                                (snapshot.soc_permille == 1000U));
    ++g_continuation_scenarios_completed; /* SIM-28 */

    measurement.current_ma = -100;
    {
        uint8_t index;
        for (index = 0U; index < BMS_CELL_COUNT; ++index)
        {
            measurement.cell_voltage_mv[index] = policy->soc.empty_cell_mv;
        }
    }
    BMS_Soc_ObserveCorrection(&engine, &policy->soc, &measurement, 100000UL);
    BMS_Soc_ObserveCorrection(&engine, &policy->soc, &measurement,
        (uint32_t)(100000UL + policy->soc.empty_qualify_ms));
    snapshot = BMS_Soc_GetEngineSnapshot(&engine, &policy->soc);
    TEST_CONT_CHECK(failures, snapshot.valid &&
                                (snapshot.soc_permille == 0U));
    return failures;
}

static uint32_t TestContinuation_Balance(void)
{
    const BMS_Policy_t *policy;
    BMS_BalanceEngine_t engine;
    BMS_DataSnapshot_t measurement;
    BMS_StateSafetySnapshot_t state;
    BMS_ProtectSafetySnapshot_t protect;
    BMS_RecoverySnapshot_t recovery;
    BMS_BalanceSnapshot_t balance_snapshot;
    uint16_t selected;
    uint8_t bal1;
    uint8_t bal2;
    uint8_t bal3;
    uint32_t failures;

    failures = 0UL;
    policy = BMS_Policy_Get();
    (void)memset(&engine, 0, sizeof(engine));
    (void)memset(&state, 0, sizeof(state));
    (void)memset(&protect, 0, sizeof(protect));
    (void)memset(&recovery, 0, sizeof(recovery));
    TestContinuation_FreshMeasurement(
        &measurement, 4100U, 1000, 250, 1UL, 0UL);
    state.state = BMS_STATE_STANDBY;
    recovery.technical_ready = true;
    measurement.cell_voltage_mv[0] = 4125U;
    measurement.cell_voltage_mv[1] = 4130U;
    measurement.cell_voltage_mv[2] = 4140U;
    measurement.cell_voltage_mv[4] = 4150U;
    selected = BMS_Balance_Evaluate(
        &engine, &policy->balance, &measurement,
        &state, &protect, &recovery, 0UL);
    TEST_CONT_CHECK(failures, (selected & 0x0001U) != 0U);
    TEST_CONT_CHECK(failures, (selected & 0x0002U) == 0U);
    TEST_CONT_CHECK(failures,
        (selected & (uint16_t)(selected << 1U)) == 0U);
    TEST_CONT_CHECK(failures, BQ76940_Control_ComposeCellBalPolicy(
        selected, 2U, false, &bal1, &bal2, &bal3));
    TEST_CONT_CHECK(failures,
        BQ76940_Control_DecodeCellBal(bal1, bal2, bal3) == selected);
    TEST_CONT_CHECK(failures,
        !BQ76940_Control_ComposeCellBalPolicy(
            0x0003U, 2U, false, &bal1, &bal2, &bal3));

    selected = BMS_Balance_Evaluate(
        &engine, &policy->balance, &measurement,
        &state, &protect, &recovery, policy->balance.rotation_ms);
    TEST_CONT_CHECK(failures, (selected & 0x0010U) != 0U);

    measurement.temperature_decic = 500;
    TEST_CONT_CHECK(failures, BMS_Balance_Evaluate(
        &engine, &policy->balance, &measurement,
        &state, &protect, &recovery, 6000UL) == 0U);
    measurement.temperature_decic = 250;
    state.state = BMS_STATE_DISCHARGE;
    TEST_CONT_CHECK(failures, BMS_Balance_Evaluate(
        &engine, &policy->balance, &measurement,
        &state, &protect, &recovery, 7000UL) == 0U);
    state.state = BMS_STATE_STANDBY;
    state.inhibit_chg_reasons = 1UL;
    TEST_CONT_CHECK(failures, BMS_Balance_Evaluate(
        &engine, &policy->balance, &measurement,
        &state, &protect, &recovery, 8000UL) == 0U);
    state.inhibit_chg_reasons = 0UL;
    measurement.cell_metadata.stale_bitmap = 1U;
    TEST_CONT_CHECK(failures, BMS_Balance_Evaluate(
        &engine, &policy->balance, &measurement,
        &state, &protect, &recovery, 9000UL) == 0U);
    measurement.cell_metadata.stale_bitmap = 0U;
    recovery.recovery_in_progress = true;
    TEST_CONT_CHECK(failures, BMS_Balance_Evaluate(
        &engine, &policy->balance, &measurement,
        &state, &protect, &recovery, 10000UL) == 0U);

    /* Exercise the real owner transaction and its fail-safe all-off attempt
     * after a transport failure. */
    TestP9_StubReset();
    TestContinuation_FreshMeasurement(
        &measurement, 4100U, 1000, 250, 1UL, 0UL);
    measurement.cell_voltage_mv[0] = 4130U;
    measurement.cell_voltage_mv[2] = 4140U;
    TestP9_SetMeasurement(&measurement);
    (void)memset(&protect, 0, sizeof(protect));
    TestP9_SetProtectSnapshot(&protect);
    BMS_State_Init(policy, 0UL);
    BMS_Recovery_Init(TestP9_GetDevice(), policy);
    (void)BMS_State_RunOnce(0UL, true, false, NULL);
    measurement.sample_sequence = 2UL;
    TestP9_SetMeasurement(&measurement);
    (void)BMS_State_RunOnce(100UL, true, false, NULL);
    BMS_Balance_Init(TestP9_GetDevice(), policy, true);
    BMS_Balance_RunOnce(100UL);
    balance_snapshot = BMS_Balance_GetSnapshot();
    TEST_CONT_CHECK(failures, balance_snapshot.register_state_confirmed);
    TEST_CONT_CHECK(failures, balance_snapshot.confirmed_bitmap != 0U);
    TestP9_SetNextWriteStatus(BQ76940_STATUS_I2C_TIMEOUT);
    BMS_Balance_RunOnce(1100UL);
    balance_snapshot = BMS_Balance_GetSnapshot();
    TEST_CONT_CHECK(failures, !balance_snapshot.register_state_confirmed);
    TEST_CONT_CHECK(failures,
        TestP9_GetRegister(BQ76940_REG_CELLBAL1) == 0U &&
        TestP9_GetRegister(BQ76940_REG_CELLBAL2) == 0U &&
        TestP9_GetRegister(BQ76940_REG_CELLBAL3) == 0U);
    ++g_continuation_scenarios_completed; /* SIM-25 */
    return failures;
}

static uint32_t TestContinuation_Can(void)
{
    const BMS_Policy_t *policy;
    BMS_DataSnapshot_t measurement;
    BMS_StateSafetySnapshot_t state;
    BMS_ProtectSafetySnapshot_t protect;
    BMS_RecoverySnapshot_t recovery;
    BMS_FetManagerSnapshot_t fet;
    BMS_CanFrame_t frames[BMS_CAN_TX_FRAME_COUNT];
    BMS_CanFrame_t command;
    BMS_DataIdentity_t identity;
    BMS_ServiceResetRequest_t request;
    BMS_CanDiagnostics_t diagnostics;
    uint8_t count;
    uint32_t failures;

    failures = 0UL;
    policy = BMS_Policy_Get();
    TestContinuation_FreshMeasurement(
        &measurement, 3700U, -1200, 250, 0x1234UL, 2UL);
    measurement.pack_voltage_mv = 48100UL;
    measurement.soc_permille = 500U;
    measurement.remaining_capacity_mah = 10000UL;
    (void)memset(&state, 0, sizeof(state));
    (void)memset(&protect, 0, sizeof(protect));
    (void)memset(&recovery, 0, sizeof(recovery));
    (void)memset(&fet, 0, sizeof(fet));
    state.state = BMS_STATE_DISCHARGE;
    recovery.technical_ready = true;
    count = BMS_Can_BuildTxFrames(
        &measurement, &state, &protect, &recovery, &fet, frames);
    TEST_CONT_CHECK(failures, count == BMS_CAN_TX_FRAME_COUNT);
    TEST_CONT_CHECK(failures, frames[0].ext_id == 0x180UL &&
                                frames[5].ext_id == 0x185UL);
    TEST_CONT_CHECK(failures, frames[1].data[0] == 0xE4U &&
                                frames[1].data[1] == 0xBBU);

    (void)memset(&command, 0, sizeof(command));
    command.ext_id = policy->can.service_rx_id;
    command.dlc = 8U;
    command.data[0] = BMS_CAN_SERVICE_MAGIC;
    command.data[1] = BMS_CAN_SERVICE_RESET_COMMAND;
    command.data[2] = (uint8_t)BMS_SERVICE_RESET_AFE_COMM;
    command.data[4] = 1U;
    identity.sample_sequence = 10UL;
    identity.afe_generation = 2UL;
    TEST_CONT_CHECK(failures, BMS_Can_DecodeServiceReset(
        &command, 100UL, 200UL, policy, &identity, 7UL, &request));
    TEST_CONT_CHECK(failures,
        request.source == BMS_SERVICE_RESET_AFE_COMM &&
        request.request_id == 1UL && request.valid);
    TEST_CONT_CHECK(failures, !BMS_Can_DecodeServiceReset(
        &command, 100UL, 1200UL, policy, &identity, 7UL, &request));
    command.dlc = 7U;
    TEST_CONT_CHECK(failures, !BMS_Can_DecodeServiceReset(
        &command, 100UL, 200UL, policy, &identity, 7UL, &request));
    command.dlc = 8U;
    command.data[2] = (uint8_t)BMS_SERVICE_RESET_SOURCE_COUNT;
    TEST_CONT_CHECK(failures, !BMS_Can_DecodeServiceReset(
        &command, 100UL, 200UL, policy, &identity, 7UL, &request));

    TestP9_StubReset();
    TestP9_SetMeasurement(&measurement);
    BMS_Can_Init(policy);
    BMS_Can_TxRunOnce(0UL);
    diagnostics = BMS_Can_GetDiagnostics();
    TEST_CONT_CHECK(failures,
        diagnostics.tx_drop_count == BMS_CAN_TX_FRAME_COUNT);
    TEST_CONT_CHECK(failures, BMS_Protect_GetSafetySnapshot().faults.active ==
                                0UL);
    ++g_continuation_scenarios_completed; /* SIM-30 */
    return failures;
}

static uint32_t TestContinuation_Persistence(void)
{
    BMS_PersistencePayload_t payload_a;
    BMS_PersistencePayload_t payload_b;
    BMS_PersistencePayload_t selected;
    uint8_t slot_a[BMS_PERSISTENCE_RECORD_BYTES];
    uint8_t slot_b[BMS_PERSISTENCE_RECORD_BYTES];
    uint32_t failures;

    failures = 0UL;
    (void)memset(&payload_a, 0, sizeof(payload_a));
    payload_a.sequence = 1UL;
    payload_a.soc_permille = 500U;
    payload_a.remaining_capacity_mah = 10000UL;
    payload_b = payload_a;
    payload_b.sequence = 2UL;
    payload_b.soc_permille = 600U;
    TEST_CONT_CHECK(failures, BMS_Persistence_Encode(&payload_a, slot_a));
    TEST_CONT_CHECK(failures, BMS_Persistence_Encode(&payload_b, slot_b));
    TEST_CONT_CHECK(failures, BMS_Persistence_SelectNewest(
        slot_a, slot_b, &selected) == BMS_PERSISTENCE_SLOT_B);
    TEST_CONT_CHECK(failures, selected.sequence == 2UL);
    slot_b[10] ^= 0x01U;
    TEST_CONT_CHECK(failures, BMS_Persistence_SelectNewest(
        slot_a, slot_b, &selected) == BMS_PERSISTENCE_SLOT_A);
    slot_a[0] = 0U;
    TEST_CONT_CHECK(failures, BMS_Persistence_SelectNewest(
        slot_a, slot_b, &selected) == BMS_PERSISTENCE_SLOT_NONE);

    payload_a.sequence = UINT32_MAX;
    payload_b.sequence = 0UL;
    TEST_CONT_CHECK(failures, BMS_Persistence_Encode(&payload_a, slot_a));
    TEST_CONT_CHECK(failures, BMS_Persistence_Encode(&payload_b, slot_b));
    TEST_CONT_CHECK(failures, BMS_Persistence_SelectNewest(
        slot_a, slot_b, &selected) == BMS_PERSISTENCE_SLOT_B);
    ++g_continuation_scenarios_completed; /* SIM-29 */
    return failures;
}

uint32_t Test_Continuation(void)
{
    g_phase10_test_failures = 0UL;
    g_phase10_test_completed = 0UL;
    g_continuation_scenarios_completed = 0UL;
    g_phase10_soc_failures = TestContinuation_Soc();
    g_phase10_balance_failures = TestContinuation_Balance();
    g_phase11_can_failures = TestContinuation_Can();
    g_storage_codec_failures = TestContinuation_Persistence();
    g_phase10_test_failures = g_phase10_soc_failures +
        g_phase10_balance_failures + g_phase11_can_failures +
        g_storage_codec_failures;
    g_phase10_test_completed = 1UL;
    return g_phase10_test_failures;
}
