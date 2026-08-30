#include "test_phase8_sample.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bms_data.h"
#include "bms_ntc.h"
#include "bms_sample.h"
#include "test_phase8_sample_stub.h"

volatile uint32_t g_phase8_sample_test_failures;
volatile uint32_t g_phase8_sample_test_completed;
volatile uint32_t g_phase8_sample_contention_completed;
volatile uint32_t g_phase8_sample_contention_attempts;
volatile uint32_t g_phase8_sample_contention_timeouts;
volatile uint32_t g_phase8_sample_contention_successes;
volatile uint32_t g_phase8_sample_contention_gives;
volatile uint32_t g_phase8_sample_contention_wait_ticks;
volatile uint32_t g_phase8_sample_contention_first_take_order;
volatile uint32_t g_phase8_sample_contention_cell_give_order;
volatile uint32_t g_phase8_sample_contention_retry_take_order;
volatile uint32_t g_phase8_sample_contention_retry_give_order;
volatile uint32_t g_phase8_sample_contention_pack_call_order;
volatile uint32_t g_phase8_sample_xready_guard_completed;
volatile uint32_t g_phase8_sample_xready_cell_rejects;
volatile uint32_t g_phase8_sample_xready_pack_rejects;
volatile uint32_t g_phase8_sample_xready_wrap_rejects;
volatile uint32_t g_phase8_sample_xready_atomic_publishes;
volatile uint32_t g_phase8_sample_provenance_guard_completed;

#define TEST_SAMPLE_CHECK(condition_)                      \
    do                                                     \
    {                                                      \
        if (!(condition_))                                 \
        {                                                  \
            ++g_phase8_sample_test_failures;               \
        }                                                  \
    } while (0)

static const BMS_NtcPoint_t s_test_ntc_table[3] =
{
    { 20000UL, (BMS_TemperatureDeciC_t)0 },
    { 10000UL, (BMS_TemperatureDeciC_t)250 },
    {  5000UL, (BMS_TemperatureDeciC_t)500 }
};

static BQ76940_Calibration_t TestSample_ValidCalibration(void)
{
    BQ76940_Calibration_t calibration;

    calibration.gain_uv_per_lsb = 380U;
    calibration.offset_mv = 0;
    calibration.valid = true;
    return calibration;
}

static void TestSample_Reset(bool configured)
{
    BQ76940_Calibration_t calibration;

    TestPhase8SampleStub_Reset();
    BMS_Data_Init();
    BMS_Sample_Init();
    if (configured)
    {
        calibration = TestSample_ValidCalibration();
        BMS_Sample_SetDevice(TestPhase8SampleStub_Device());
        TEST_SAMPLE_CHECK(BMS_Sample_SetCalibration(&calibration));
        TEST_SAMPLE_CHECK(BMS_Sample_SetNtcTable(NULL, 0U));
    }
    g_phase8_sample_stub_control.scheduler_state = taskSCHEDULER_RUNNING;
    TestPhase8SampleStub_ClearObservation();
}

static BMS_PackVoltageMv_t TestSample_ExpectedCellSum(void)
{
    BMS_PackVoltageMv_t sum;
    uint32_t index;

    sum = (BMS_PackVoltageMv_t)0U;
    for (index = 0UL; index < (uint32_t)BMS_CELL_COUNT; ++index)
    {
        sum += (BMS_PackVoltageMv_t)
            g_phase8_sample_stub_control.cell_voltage_mv[index];
    }
    return sum;
}

static void TestSample_SetCc(bool valid,
                             int16_t raw,
                             TickType_t tick,
                             uint32_t sequence)
{
    g_phase8_sample_stub_control.latest_cc.raw = raw;
    g_phase8_sample_stub_control.latest_cc.sample_ms = tick;
    g_phase8_sample_stub_control.latest_cc.sequence = sequence;
    g_phase8_sample_stub_control.latest_cc.xready_generation =
        g_phase8_sample_stub_control.xready_state.xready_generation;
    g_phase8_sample_stub_control.latest_cc.valid = valid;
}

static bool TestSample_Snapshot(BMS_DataSnapshot_t *snapshot,
                                BMS_TimestampMs_t now_ms)
{
    g_phase8_sample_stub_control.fail_data_take_ordinal = 0UL;
    return BMS_Data_GetSnapshot(snapshot, now_ms);
}

static bool TestSample_SameCore(const BMS_DataSnapshot_t *left,
                                const BMS_DataSnapshot_t *right)
{
    uint32_t index;

    if ((left->sample_sequence != right->sample_sequence) ||
        (left->snapshot_timestamp_ms != right->snapshot_timestamp_ms) ||
        (left->pack_voltage_mv != right->pack_voltage_mv) ||
        (left->bq_pack_voltage_mv != right->bq_pack_voltage_mv))
    {
        return false;
    }
    for (index = 0UL; index < (uint32_t)BMS_CELL_COUNT; ++index)
    {
        if (left->cell_voltage_mv[index] !=
            right->cell_voltage_mv[index])
        {
            return false;
        }
    }
    return true;
}

static void TestSample_RunSevenSuccessfulCycles(void)
{
    uint32_t cycle;

    for (cycle = 1UL; cycle <= 7UL; ++cycle)
    {
        TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(
            (BMS_TimestampMs_t)(cycle * BMS_SAMPLE_PERIOD_MS)));
    }
}

static void TestSample_SetterSchedulerGuard(void)
{
    BQ76940_Calibration_t calibration;

    TestPhase8SampleStub_Reset();
    BMS_Data_Init();
    BMS_Sample_Init();
    calibration = TestSample_ValidCalibration();
    TestPhase8SampleStub_ClearObservation();
    BMS_Sample_SetDevice(TestPhase8SampleStub_Device());
    TEST_SAMPLE_CHECK(BMS_Sample_SetCalibration(&calibration));
    TEST_SAMPLE_CHECK(BMS_Sample_SetNtcTable(NULL, 0U));
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.scheduler_state_get_count == 3UL);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.scheduler_suspend_count == 2UL);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.scheduler_resume_count == 2UL);

    g_phase8_sample_stub_control.scheduler_state =
        taskSCHEDULER_SUSPENDED;
    BMS_Sample_SetDevice(TestPhase8SampleStub_Device());
    TEST_SAMPLE_CHECK(BMS_Sample_SetCalibration(&calibration));
    TEST_SAMPLE_CHECK(BMS_Sample_SetNtcTable(s_test_ntc_table, 3U));
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.scheduler_state_get_count == 6UL);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.scheduler_suspend_count == 4UL);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.scheduler_resume_count == 4UL);

    g_phase8_sample_stub_control.scheduler_state = taskSCHEDULER_RUNNING;
    BMS_Sample_SetDevice(TestPhase8SampleStub_Device());
    TEST_SAMPLE_CHECK(BMS_Sample_SetCalibration(&calibration));
    TEST_SAMPLE_CHECK(BMS_Sample_SetNtcTable(NULL, 0U));
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.scheduler_state_get_count == 9UL);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.scheduler_suspend_count == 9UL);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.scheduler_resume_count == 9UL);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.scheduler_max_depth == 2UL);
    TEST_SAMPLE_CHECK(TestPhase8SampleStub_LocksBalanced());
}

static void TestSample_StartupAndSignedCurrent(void)
{
    BMS_DataSnapshot_t snapshot;
    BMS_SampleDiagnostics_t diagnostics;
    BQ76940_Calibration_t calibration;
    BQ76940_Calibration_t invalid_calibration;
    uint32_t index;
    uint32_t current_calls;

    TestSample_Reset(false);
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 5000UL));
    TEST_SAMPLE_CHECK(snapshot.sample_sequence == 0UL);
    TEST_SAMPLE_CHECK(!snapshot.pack_metadata.valid);
    TestPhase8SampleStub_ClearObservation();
    TEST_SAMPLE_CHECK(!BMS_Sample_RunOnce(5000UL));
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.configuration_not_ready_count == 1UL);
    TEST_SAMPLE_CHECK(diagnostics.stale_sample_count == 0UL);
    TEST_SAMPLE_CHECK(diagnostics.stale_transition_count == 0UL);
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 5000UL));
    TEST_SAMPLE_CHECK(snapshot.sample_sequence == 0UL);
    TEST_SAMPLE_CHECK(!snapshot.pack_metadata.valid);

    BMS_Sample_SetDevice(TestPhase8SampleStub_Device());
    invalid_calibration = TestSample_ValidCalibration();
    invalid_calibration.valid = false;
    TEST_SAMPLE_CHECK(!BMS_Sample_SetCalibration(&invalid_calibration));
    TestPhase8SampleStub_ClearObservation();
    TEST_SAMPLE_CHECK(!BMS_Sample_RunOnce(5250UL));
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.calibration_invalid_count == 1UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.cell_call_count ==
                      0UL);

    calibration = TestSample_ValidCalibration();
    TEST_SAMPLE_CHECK(BMS_Sample_SetCalibration(&calibration));
    TestSample_SetCc(true, (int16_t)1000, (TickType_t)100U, 1UL);
    TestPhase8SampleStub_ClearObservation();
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(5500UL));
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.cell_call_count ==
                      1UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.pack_call_count ==
                      1UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.i2c_take_success_count ==
                      2UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.i2c_give_count == 2UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.data_take_success_count ==
                      2UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.data_give_count == 2UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.i2c_data_nesting_count ==
                      0UL);
    TEST_SAMPLE_CHECK(TestPhase8SampleStub_LocksBalanced());
    /* FML 只发布 coherent data；APL event group 不属于该层的写权限。 */
    TEST_SAMPLE_CHECK((g_phase8_sample_stub_observation.event_bits &
                       EVT_SAMPLE_READY) == (EventBits_t)0U);
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 5500UL));
    TEST_SAMPLE_CHECK(snapshot.sample_sequence == 1UL);
    TEST_SAMPLE_CHECK(snapshot.snapshot_timestamp_ms == 5500UL);
    TEST_SAMPLE_CHECK(snapshot.pack_voltage_mv ==
                      TestSample_ExpectedCellSum());
    TEST_SAMPLE_CHECK(snapshot.bq_pack_voltage_mv ==
                      g_phase8_sample_stub_control.pack_voltage_mv);
    for (index = 0UL; index < (uint32_t)BMS_CELL_COUNT; ++index)
    {
        TEST_SAMPLE_CHECK(snapshot.cell_voltage_mv[index] ==
            g_phase8_sample_stub_control.cell_voltage_mv[index]);
    }
    TEST_SAMPLE_CHECK(snapshot.current_metadata.valid);
    TEST_SAMPLE_CHECK(snapshot.current_metadata.timestamp_ms == 100UL);
    TEST_SAMPLE_CHECK(snapshot.current_ma == (BMS_CurrentMa_t)2110);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.last_rsense_uohm ==
                      BMS_RSENSE_REFERENCE_UOHM);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.last_current_polarity ==
                      (int8_t)BMS_CURRENT_POLARITY);

    current_calls =
        g_phase8_sample_stub_observation.current_convert_call_count;
    ++g_phase8_sample_stub_control.cell_voltage_mv[0];
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(5625UL));
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.current_convert_call_count ==
        current_calls);
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 5625UL));
    TEST_SAMPLE_CHECK(snapshot.sample_sequence == 2UL);
    TEST_SAMPLE_CHECK(snapshot.cell_voltage_mv[0] ==
                      g_phase8_sample_stub_control.cell_voltage_mv[0]);
    TEST_SAMPLE_CHECK(snapshot.current_ma == (BMS_CurrentMa_t)2110);
    TEST_SAMPLE_CHECK(snapshot.current_metadata.timestamp_ms == 100UL);

    TestSample_SetCc(true, (int16_t)-1000, (TickType_t)5750U, 2UL);
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(5750UL));
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 5750UL));
    TEST_SAMPLE_CHECK(snapshot.sample_sequence == 3UL);
    TEST_SAMPLE_CHECK(snapshot.current_ma == (BMS_CurrentMa_t)-2110);
    TEST_SAMPLE_CHECK(snapshot.current_metadata.timestamp_ms == 5750UL);
}

static void TestSample_CurrentFailureAndCellRanges(void)
{
    BMS_DataSnapshot_t previous;
    BMS_DataSnapshot_t snapshot;
    BMS_SampleDiagnostics_t diagnostics;
    uint16_t cell2_mask;
    uint16_t cell9_mask;

    TestSample_Reset(true);
    TestSample_SetCc(true, (int16_t)100, (TickType_t)90U, 1UL);
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(100UL));
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&previous, 100UL));
    TestSample_SetCc(true, (int16_t)200, (TickType_t)300U, 2UL);
    g_phase8_sample_stub_control.current_status =
        BQ76940_STATUS_RANGE_ERROR;
    TestPhase8SampleStub_ClearObservation();
    TEST_SAMPLE_CHECK(!BMS_Sample_RunOnce(350UL));
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.i2c_take_success_count ==
                      2UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.i2c_give_count == 2UL);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.current_convert_call_count == 1UL);
    TEST_SAMPLE_CHECK(TestPhase8SampleStub_LocksBalanced());
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 350UL));
    TEST_SAMPLE_CHECK(TestSample_SameCore(&snapshot, &previous));
    TEST_SAMPLE_CHECK(snapshot.current_ma == previous.current_ma);
    TEST_SAMPLE_CHECK(snapshot.current_metadata.timestamp_ms ==
                      previous.current_metadata.timestamp_ms);
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.current_group_failure_count == 1UL);
    TEST_SAMPLE_CHECK(diagnostics.transport_failure_count == 0UL);
    g_phase8_sample_stub_control.current_status = BQ76940_STATUS_OK;
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(600UL));
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 600UL));
    TEST_SAMPLE_CHECK(snapshot.sample_sequence == 2UL);
    TEST_SAMPLE_CHECK(snapshot.current_metadata.timestamp_ms == 300UL);
    TEST_SAMPLE_CHECK(snapshot.current_ma == (BMS_CurrentMa_t)422);

    TestSample_Reset(true);
    g_phase8_sample_stub_control.cell_voltage_mv[2] =
        (BMS_CellVoltageMv_t)1999U;
    g_phase8_sample_stub_control.cell_voltage_mv[9] =
        (BMS_CellVoltageMv_t)5001U;
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(250UL));
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 250UL));
    cell2_mask = (uint16_t)((uint16_t)1U << 2U);
    cell9_mask = (uint16_t)((uint16_t)1U << 9U);
    TEST_SAMPLE_CHECK(snapshot.cell_metadata.valid_bitmap ==
                      BMS_CELL_DEFINED_MASK);
    TEST_SAMPLE_CHECK((snapshot.cell_metadata.in_range_bitmap & cell2_mask) ==
                      (uint16_t)0U);
    TEST_SAMPLE_CHECK((snapshot.cell_metadata.in_range_bitmap & cell9_mask) ==
                      (uint16_t)0U);
    TEST_SAMPLE_CHECK(snapshot.pack_metadata.valid);
    TEST_SAMPLE_CHECK(!snapshot.pack_metadata.in_range);
}

static void TestSample_CoreFailuresRetainPrevious(void)
{
    BMS_DataSnapshot_t previous;
    BMS_DataSnapshot_t snapshot;
    BMS_SampleDiagnostics_t diagnostics;
    BQ76940_Calibration_t calibration;

    TestSample_Reset(true);
    TestSample_SetCc(true, (int16_t)100, (TickType_t)90U, 1UL);
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(100UL));
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&previous, 100UL));

    g_phase8_sample_stub_control.cell_status =
        BQ76940_STATUS_CRC_MISMATCH;
    g_phase8_sample_stub_control.write_one_cell_before_cell_failure = true;
    TestPhase8SampleStub_ClearObservation();
    TEST_SAMPLE_CHECK(!BMS_Sample_RunOnce(350UL));
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.cell_call_count == 1UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.pack_call_count == 0UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.i2c_take_success_count ==
                      1UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.i2c_give_count == 1UL);
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 350UL));
    TEST_SAMPLE_CHECK(TestSample_SameCore(&snapshot, &previous));
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.cell_group_failure_count == 1UL);
    TEST_SAMPLE_CHECK(diagnostics.transport_failure_count == 1UL);

    g_phase8_sample_stub_control.cell_status = BQ76940_STATUS_OK;
    g_phase8_sample_stub_control.write_one_cell_before_cell_failure = false;
    g_phase8_sample_stub_control.pack_status = BQ76940_STATUS_I2C_NACK;
    TestPhase8SampleStub_ClearObservation();
    TEST_SAMPLE_CHECK(!BMS_Sample_RunOnce(600UL));
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.cell_call_count == 1UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.pack_call_count == 1UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.i2c_take_success_count ==
                      2UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.i2c_give_count == 2UL);
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 600UL));
    TEST_SAMPLE_CHECK(TestSample_SameCore(&snapshot, &previous));
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.pack_group_failure_count == 1UL);
    TEST_SAMPLE_CHECK(diagnostics.transport_failure_count == 2UL);

    g_phase8_sample_stub_control.pack_status = BQ76940_STATUS_OK;
    TEST_SAMPLE_CHECK(!BMS_Sample_SetCalibration(NULL));
    TestPhase8SampleStub_ClearObservation();
    TEST_SAMPLE_CHECK(!BMS_Sample_RunOnce(850UL));
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.i2c_take_attempt_count ==
                      0UL);
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 850UL));
    TEST_SAMPLE_CHECK(TestSample_SameCore(&snapshot, &previous));
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.calibration_invalid_count == 1UL);
    TEST_SAMPLE_CHECK(diagnostics.failure_count == 3UL);
    TEST_SAMPLE_CHECK(diagnostics.consecutive_failure_count == 3UL);
    TEST_SAMPLE_CHECK(diagnostics.max_consecutive_failure_count == 3UL);

    calibration = TestSample_ValidCalibration();
    TEST_SAMPLE_CHECK(BMS_Sample_SetCalibration(&calibration));
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(1100UL));
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.success_count == 2UL);
    TEST_SAMPLE_CHECK(diagnostics.failure_count == 3UL);
    TEST_SAMPLE_CHECK(diagnostics.consecutive_failure_count == 0UL);
    TEST_SAMPLE_CHECK(diagnostics.max_consecutive_failure_count == 3UL);
}

static void TestSample_MutexTimeouts(void)
{
    BMS_SampleDiagnostics_t diagnostics;
    uint32_t i2c_attempts_before;
    uint32_t i2c_gives_before;

    TestSample_Reset(true);
    g_phase8_sample_stub_control.fail_i2c_take_ordinal = 1UL;
    TEST_SAMPLE_CHECK(!BMS_Sample_RunOnce(250UL));
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.i2c_take_attempt_count ==
                      1UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.i2c_give_count == 0UL);
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.i2c_timeout_count == 1UL);
    TEST_SAMPLE_CHECK(diagnostics.cell_group_failure_count == 1UL);
    TEST_SAMPLE_CHECK(diagnostics.transport_failure_count == 0UL);

    TestSample_Reset(true);
    g_phase8_sample_stub_control.fail_i2c_take_ordinal = 2UL;
    TEST_SAMPLE_CHECK(!BMS_Sample_RunOnce(250UL));
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.i2c_take_attempt_count ==
                      2UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.i2c_give_count == 1UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.pack_call_count == 0UL);
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.pack_group_failure_count == 1UL);
    TEST_SAMPLE_CHECK(diagnostics.i2c_timeout_count == 1UL);

    TestSample_Reset(true);
    TestSample_RunSevenSuccessfulCycles();
    i2c_attempts_before =
        g_phase8_sample_stub_observation.i2c_take_attempt_count;
    i2c_gives_before = g_phase8_sample_stub_observation.i2c_give_count;
    g_phase8_sample_stub_control.fail_i2c_take_ordinal =
        i2c_attempts_before + 3UL;
    TEST_SAMPLE_CHECK(!BMS_Sample_RunOnce(2000UL));
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.i2c_take_attempt_count ==
                      (i2c_attempts_before + 3UL));
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.i2c_give_count ==
                      (i2c_gives_before + 2UL));
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.ts_read_call_count ==
                      0UL);
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.temperature_group_failure_count == 1UL);
    TEST_SAMPLE_CHECK(diagnostics.i2c_timeout_count == 1UL);
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(2250UL));
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.ts_read_call_count ==
                      1UL);
    TEST_SAMPLE_CHECK(TestPhase8SampleStub_LocksBalanced());
}

static void TestSample_TemperatureCadenceAndCurve(void)
{
    BMS_DataSnapshot_t snapshot;
    BMS_DataSnapshot_t previous;
    BMS_SampleDiagnostics_t diagnostics;
    uint32_t sequence_before;

    TestSample_Reset(true);
    TestSample_RunSevenSuccessfulCycles();
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.ts_read_call_count ==
                      0UL);
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(2000UL));
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.ts_read_call_count ==
                      1UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.ts_convert_call_count ==
                      1UL);
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 2000UL));
    TEST_SAMPLE_CHECK(snapshot.ts1_metadata.valid);
    TEST_SAMPLE_CHECK(snapshot.ts1_raw14 ==
                      g_phase8_sample_stub_control.ts1_raw14);
    TEST_SAMPLE_CHECK(snapshot.ts1_resistance_ohm ==
                      g_phase8_sample_stub_control.ts1_resistance_ohm);
    TEST_SAMPLE_CHECK(!snapshot.temperature_metadata.valid);
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.ntc_curve_unavailable_count == 1UL);
    TEST_SAMPLE_CHECK(
        diagnostics.temperature_conversion_unavailable_count == 1UL);

    TestSample_Reset(true);
    TEST_SAMPLE_CHECK(BMS_Sample_SetNtcTable(s_test_ntc_table, 3U));
    TestSample_RunSevenSuccessfulCycles();
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(2000UL));
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 2000UL));
    TEST_SAMPLE_CHECK(snapshot.temperature_metadata.valid);
    TEST_SAMPLE_CHECK(snapshot.temperature_metadata.in_range);
    TEST_SAMPLE_CHECK(snapshot.temperature_decic ==
                      (BMS_TemperatureDeciC_t)125);

    TestSample_Reset(true);
    TestSample_RunSevenSuccessfulCycles();
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&previous, 1750UL));
    sequence_before = previous.sample_sequence;
    g_phase8_sample_stub_control.ts_read_status =
        BQ76940_STATUS_CRC_REJECTED;
    TestPhase8SampleStub_ClearObservation();
    TEST_SAMPLE_CHECK(!BMS_Sample_RunOnce(2000UL));
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.i2c_take_success_count ==
                      3UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.i2c_give_count == 3UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.ts_read_call_count ==
                      1UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.ts_convert_call_count ==
                      0UL);
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 2000UL));
    TEST_SAMPLE_CHECK(snapshot.sample_sequence == sequence_before);
    TEST_SAMPLE_CHECK(TestSample_SameCore(&snapshot, &previous));
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.temperature_group_failure_count == 1UL);
    TEST_SAMPLE_CHECK(diagnostics.transport_failure_count == 1UL);
    g_phase8_sample_stub_control.ts_read_status = BQ76940_STATUS_OK;
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(2250UL));

    TestSample_Reset(true);
    TestSample_RunSevenSuccessfulCycles();
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&previous, 1750UL));
    g_phase8_sample_stub_control.ts_convert_status =
        BQ76940_STATUS_RANGE_ERROR;
    TestPhase8SampleStub_ClearObservation();
    TEST_SAMPLE_CHECK(!BMS_Sample_RunOnce(2000UL));
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.i2c_take_success_count ==
                      3UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.i2c_give_count == 3UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.ts_convert_call_count ==
                      1UL);
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 2000UL));
    TEST_SAMPLE_CHECK(TestSample_SameCore(&snapshot, &previous));
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.temperature_group_failure_count == 1UL);
    TEST_SAMPLE_CHECK(diagnostics.transport_failure_count == 0UL);
}

static void TestSample_PublishContentionKeepsPending(void)
{
    BMS_DataSnapshot_t snapshot;
    BMS_SampleDiagnostics_t diagnostics;
    uint32_t current_calls;
    uint32_t ts_calls;

    TestSample_Reset(true);
    TestSample_RunSevenSuccessfulCycles();
    TestSample_SetCc(true, (int16_t)-1000, (TickType_t)1800U, 1UL);
    TestPhase8SampleStub_ClearObservation();
    g_phase8_sample_stub_control.fail_data_take_ordinal = 2UL;
    TEST_SAMPLE_CHECK(!BMS_Sample_RunOnce(2000UL));
    current_calls =
        g_phase8_sample_stub_observation.current_convert_call_count;
    ts_calls = g_phase8_sample_stub_observation.ts_read_call_count;
    TEST_SAMPLE_CHECK(current_calls == 1UL);
    TEST_SAMPLE_CHECK(ts_calls == 1UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.i2c_take_success_count ==
                      3UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.i2c_give_count == 3UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.i2c_data_nesting_count ==
                      0UL);
    TEST_SAMPLE_CHECK(TestPhase8SampleStub_LocksBalanced());
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 2000UL));
    TEST_SAMPLE_CHECK(snapshot.sample_sequence == 7UL);
    TEST_SAMPLE_CHECK(!snapshot.current_metadata.valid);
    TEST_SAMPLE_CHECK(!snapshot.ts1_metadata.valid);
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.data_publish_failure_count == 1UL);
    TEST_SAMPLE_CHECK(diagnostics.frame_reject_count == 1UL);

    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(2250UL));
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.current_convert_call_count ==
        (current_calls + 1UL));
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.ts_read_call_count ==
                      (ts_calls + 1UL));
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 2250UL));
    TEST_SAMPLE_CHECK(snapshot.sample_sequence == 8UL);
    TEST_SAMPLE_CHECK(snapshot.current_metadata.valid);
    TEST_SAMPLE_CHECK(snapshot.current_metadata.timestamp_ms == 1800UL);
    TEST_SAMPLE_CHECK(snapshot.current_ma == (BMS_CurrentMa_t)-2110);
    TEST_SAMPLE_CHECK(snapshot.ts1_metadata.valid);
    TEST_SAMPLE_CHECK(!snapshot.temperature_metadata.valid);
}

static void TestSample_StaleAndProtectOrdering(void)
{
    BMS_DataSnapshot_t snapshot;
    BMS_SampleDiagnostics_t diagnostics;

    TestSample_Reset(true);
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(100UL));
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.stale_sample_count == 0UL);
    TEST_SAMPLE_CHECK(diagnostics.stale_transition_count == 0UL);
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(1101UL));
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.stale_sample_count == 1UL);
    TEST_SAMPLE_CHECK(diagnostics.stale_transition_count == 1UL);
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(1200UL));
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(2202UL));
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.stale_sample_count == 2UL);
    TEST_SAMPLE_CHECK(diagnostics.stale_transition_count == 2UL);

    TestSample_Reset(true);
    TestSample_SetCc(true, (int16_t)500, (TickType_t)10U, 1UL);
    g_phase8_sample_stub_control.replacement_cc.raw = (int16_t)-500;
    g_phase8_sample_stub_control.replacement_cc.sample_ms = (TickType_t)200U;
    g_phase8_sample_stub_control.replacement_cc.sequence = 2UL;
    g_phase8_sample_stub_control.replacement_cc.xready_generation =
        g_phase8_sample_stub_control.xready_state.xready_generation;
    g_phase8_sample_stub_control.replacement_cc.valid = true;
    g_phase8_sample_stub_control.replace_latest_cc_on_pack_give = true;
    TestPhase8SampleStub_ClearObservation();
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(250UL));
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.latest_cc_replacement_count == 1UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.pack_give_order <
                      g_phase8_sample_stub_observation.protect_get_order);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.protect_get_order <
                      g_phase8_sample_stub_observation.current_convert_order);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.current_convert_order <
                      g_phase8_sample_stub_observation.second_data_take_order);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.protect_get_while_locked_count ==
        0UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.conversion_while_i2c_count ==
                      0UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.i2c_data_nesting_count ==
                      0UL);
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 250UL));
    TEST_SAMPLE_CHECK(snapshot.current_ma == (BMS_CurrentMa_t)-1055);
    TEST_SAMPLE_CHECK(snapshot.current_metadata.timestamp_ms == 200UL);
}

static void TestSample_BoundedStaleProjection(void)
{
    BMS_DataFreshnessSnapshot_t freshness;
    BMS_SampleDiagnostics_t diagnostics;

    TestSample_Reset(true);
    g_phase8_sample_stub_control.fail_data_take_ordinal = 1UL;
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(250UL));
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.stale_check_failure_count == 1UL);
    TEST_SAMPLE_CHECK(diagnostics.failure_count == 0UL);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.data_take_attempt_count == 2UL);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.data_take_success_count == 1UL);
    TEST_SAMPLE_CHECK(TestPhase8SampleStub_LocksBalanced());

    /* 先锁存 voltage stale，再模拟完整 uint32_t wrap；即使算术 age 变小也必须保持 sticky。 */
    TestSample_Reset(true);
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(100UL));
    TEST_SAMPLE_CHECK(BMS_Data_GetFreshnessSnapshot(&freshness, 1101UL));
    TEST_SAMPLE_CHECK(freshness.pack_metadata.stale_latched);
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(150UL));
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.stale_sample_count == 1UL);
    TEST_SAMPLE_CHECK(diagnostics.stale_transition_count == 1UL);
    TEST_SAMPLE_CHECK(BMS_Data_GetFreshnessSnapshot(&freshness, 150UL));
    TEST_SAMPLE_CHECK(!freshness.pack_metadata.stale_latched);
}

static void TestSample_ConfigurationRevisionGuard(void)
{
    BMS_DataSnapshot_t previous;
    BMS_DataSnapshot_t snapshot;
    BMS_SampleDiagnostics_t diagnostics;
    uint32_t captured_revision;
    uint32_t current_revision;

    /* 正式 revision primitive 可区分立即自然回绕；完整 2^32 alias 由生命周期约束。 */
    captured_revision = UINT32_MAX;
    current_revision =
        BMS_SAMPLE_CONFIGURATION_REVISION_NEXT(captured_revision);
    TEST_SAMPLE_CHECK(current_revision == 0UL);
    TEST_SAMPLE_CHECK(captured_revision != current_revision);

    /* 捕获 table A 后注入 A→B→A；pointer/count 相等会漏掉 ABA，revision 必须拒绝
     * 本帧并保留 previous-good。 */
    TestSample_Reset(true);
    TEST_SAMPLE_CHECK(BMS_Sample_SetNtcTable(
        TestPhase8SampleStub_AbaNtcTableA(),
        TestPhase8SampleStub_AbaNtcPointCount()));
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(100UL));
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&previous, 100UL));
    TestPhase8SampleStub_ClearObservation();
    g_phase8_sample_stub_control
        .inject_ntc_configuration_aba_after_pack_give = true;
    TEST_SAMPLE_CHECK(!BMS_Sample_RunOnce(350UL));
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation
            .configuration_aba_injection_count == 1UL);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation
            .configuration_aba_set_success_count == 2UL);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.ts_read_call_count == 0UL);
    TEST_SAMPLE_CHECK(TestPhase8SampleStub_LocksBalanced());
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 350UL));
    TEST_SAMPLE_CHECK(TestSample_SameCore(&snapshot, &previous));
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.configuration_not_ready_count == 1UL);
    TEST_SAMPLE_CHECK(diagnostics.data_publish_failure_count == 0UL);
    TEST_SAMPLE_CHECK(diagnostics.xready_postcheck_reject_count == 0UL);

    TestPhase8SampleStub_ClearObservation();
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(600UL));
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 600UL));
    TEST_SAMPLE_CHECK(snapshot.sample_sequence ==
                      (previous.sample_sequence + 1UL));
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.success_count == 2UL);
    TEST_SAMPLE_CHECK(diagnostics.failure_count == 1UL);
}

static void TestSample_ConfigurationRevisionImmediateWrapGuard(void)
{
    BMS_DataSnapshot_t previous;
    BMS_DataSnapshot_t snapshot;
    BMS_SampleDiagnostics_t diagnostics;
    uint32_t captured_revision;
    uint32_t current_revision;

    captured_revision = UINT32_MAX;
    current_revision =
        BMS_SAMPLE_CONFIGURATION_REVISION_NEXT(captured_revision);
    TEST_SAMPLE_CHECK(current_revision == 0UL);
    TEST_SAMPLE_CHECK(captured_revision != current_revision);

    /* revision seed 为 MAX；RunOnce 捕获后 setter 重装同 pointer/count 并 MAX→0，
     * 只有 revision guard 能拒绝。 */
    TestSample_Reset(true);
    TEST_SAMPLE_CHECK(BMS_Sample_SetNtcTable(
        TestPhase8SampleStub_AbaNtcTableA(),
        TestPhase8SampleStub_AbaNtcPointCount()));
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(100UL));
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&previous, 100UL));
    BMS_Sample_TestSeedConfigurationRevision(captured_revision);
    TestPhase8SampleStub_ClearObservation();
    g_phase8_sample_stub_control
        .inject_ntc_configuration_wrap_after_pack_give = true;
    TEST_SAMPLE_CHECK(!BMS_Sample_RunOnce(350UL));
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation
            .configuration_wrap_injection_count == 1UL);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation
            .configuration_wrap_set_success_count == 1UL);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.ts_read_call_count == 0UL);
    TEST_SAMPLE_CHECK(TestPhase8SampleStub_LocksBalanced());
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 350UL));
    TEST_SAMPLE_CHECK(TestSample_SameCore(&snapshot, &previous));
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.configuration_not_ready_count == 1UL);
    TEST_SAMPLE_CHECK(diagnostics.data_publish_failure_count == 0UL);
    TEST_SAMPLE_CHECK(diagnostics.xready_postcheck_reject_count == 0UL);

    TestPhase8SampleStub_ClearObservation();
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(600UL));
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 600UL));
    TEST_SAMPLE_CHECK(snapshot.sample_sequence ==
                      (previous.sample_sequence + 1UL));
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.success_count == 2UL);
    TEST_SAMPLE_CHECK(diagnostics.failure_count == 1UL);
}

static void TestSample_XreadyGenerationGuard(void)
{
    BMS_DataSnapshot_t snapshot;
    BMS_SampleDiagnostics_t diagnostics;
    BQ76940_Calibration_t calibration;

    /* SetDevice 使 prior calibration binding 失效。 */
    TestSample_Reset(true);
    BMS_Sample_SetDevice(TestPhase8SampleStub_Device());
    TestPhase8SampleStub_ClearObservation();
    TEST_SAMPLE_CHECK(!BMS_Sample_RunOnce(250UL));
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.i2c_take_attempt_count == 0UL);
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.calibration_invalid_count == 1UL);

    /* XREADY active 在第一笔 AFE transaction 前即拒绝。 */
    TestSample_Reset(true);
    g_phase8_sample_stub_control.xready_state.xready_generation = 1UL;
    g_phase8_sample_stub_control.xready_state.active = true;
    TEST_SAMPLE_CHECK(!BMS_Sample_RunOnce(250UL));
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.i2c_take_attempt_count == 0UL);
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.xready_precheck_reject_count == 1UL);
    TEST_SAMPLE_CHECK(diagnostics.configuration_not_ready_count == 1UL);

    /* 即使 generation 数值相同，SetDevice 也创建新 physical epoch；无新 CC 时首个
     * successful core 只淘汰一次 old current。 */
    TestSample_Reset(true);
    TestSample_SetCc(true, (int16_t)100, (TickType_t)90U, 1UL);
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(100UL));
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 100UL));
    TEST_SAMPLE_CHECK(snapshot.current_metadata.valid);
    BMS_Sample_SetDevice(TestPhase8SampleStub_Device());
    calibration = TestSample_ValidCalibration();
    TEST_SAMPLE_CHECK(BMS_Sample_SetCalibration(&calibration));
    TestPhase8SampleStub_ClearObservation();
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(350UL));
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 350UL));
    TEST_SAMPLE_CHECK(!snapshot.current_metadata.valid);
    TEST_SAMPLE_CHECK(snapshot.current_metadata.timestamp_ms == 350UL);
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(600UL));
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 600UL));
    TEST_SAMPLE_CHECK(!snapshot.current_metadata.valid);
    TEST_SAMPLE_CHECK(snapshot.current_metadata.timestamp_ms == 350UL);

    /* SetDevice 同时消费未读 old-device mailbox identity，只有后续 CC 可提供新 current。 */
    TestSample_Reset(true);
    TestSample_SetCc(true, (int16_t)100, (TickType_t)90U, 7UL);
    BMS_Sample_SetDevice(TestPhase8SampleStub_Device());
    calibration = TestSample_ValidCalibration();
    TEST_SAMPLE_CHECK(BMS_Sample_SetCalibration(&calibration));
    TestPhase8SampleStub_ClearObservation();
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(250UL));
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.current_convert_call_count == 0UL);
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 250UL));
    TEST_SAMPLE_CHECK(!snapshot.current_metadata.valid);
    TestSample_SetCc(true, (int16_t)200, (TickType_t)400U, 8UL);
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(500UL));
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 500UL));
    TEST_SAMPLE_CHECK(snapshot.current_metadata.valid);

    /* failed core 后 prior current/stale mailbox 只作为 previous-good 保留；下一成功
     * new-epoch core 清 current，同 epoch CC 再恢复。 */
    TestSample_Reset(true);
    TestSample_SetCc(true, (int16_t)100, (TickType_t)90U, 1UL);
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(100UL));
    g_phase8_sample_stub_control.xready_state.xready_generation = 1UL;
    g_phase8_sample_stub_control.xready_state.active = false;
    calibration = TestSample_ValidCalibration();
    TEST_SAMPLE_CHECK(BMS_Sample_SetCalibration(&calibration));
    TestPhase8SampleStub_ClearObservation();
    g_phase8_sample_stub_control.fail_data_take_ordinal = 2UL;
    TEST_SAMPLE_CHECK(!BMS_Sample_RunOnce(350UL));
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 350UL));
    TEST_SAMPLE_CHECK(snapshot.current_metadata.valid);
    TestPhase8SampleStub_ClearObservation();
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(600UL));
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 600UL));
    TEST_SAMPLE_CHECK(!snapshot.current_metadata.valid);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.current_convert_call_count == 0UL);
    TestSample_SetCc(true, (int16_t)200, (TickType_t)800U, 2UL);
    TestPhase8SampleStub_ClearObservation();
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(850UL));
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 850UL));
    TEST_SAMPLE_CHECK(snapshot.current_metadata.valid);
    TEST_SAMPLE_CHECK(snapshot.current_ma == (BMS_CurrentMa_t)422);

    /* recovery/rebinding 后 old-epoch mailbox 即使 stub 故意保留 valid，也不能成为 current。 */
    TestSample_Reset(true);
    TestSample_SetCc(true, (int16_t)300, (TickType_t)100U, 7UL);
    g_phase8_sample_stub_control.xready_state.xready_generation = 1UL;
    g_phase8_sample_stub_control.xready_state.active = false;
    calibration = TestSample_ValidCalibration();
    TEST_SAMPLE_CHECK(BMS_Sample_SetCalibration(&calibration));
    TestPhase8SampleStub_ClearObservation();
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(250UL));
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.current_convert_call_count == 0UL);
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 250UL));
    TEST_SAMPLE_CHECK(!snapshot.current_metadata.valid);
    TestSample_SetCc(true, (int16_t)300, (TickType_t)400U, 8UL);
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(500UL));
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 500UL));
    TEST_SAMPLE_CHECK(snapshot.current_metadata.valid);

    /* cell transaction 后 Protect transition 必须拒绝 staging；清 active 不复活旧 binding。 */
    TestSample_Reset(true);
    g_phase8_sample_stub_control.inject_xready_after_cell_give = true;
    TEST_SAMPLE_CHECK(!BMS_Sample_RunOnce(250UL));
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.cell_call_count == 1UL);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.pack_call_count == 1UL);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_control.xready_state.xready_generation == 1UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_control.xready_state.active);
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.xready_postcheck_reject_count == 1UL);
    TEST_SAMPLE_CHECK(diagnostics.data_publish_failure_count == 0UL);
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 250UL));
    TEST_SAMPLE_CHECK(snapshot.sample_sequence == 0UL);
    g_phase8_sample_xready_cell_rejects =
        diagnostics.xready_postcheck_reject_count;

    g_phase8_sample_stub_control.xready_state.active = false;
    TestPhase8SampleStub_ClearObservation();
    TEST_SAMPLE_CHECK(!BMS_Sample_RunOnce(500UL));
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.i2c_take_attempt_count == 0UL);
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.xready_precheck_reject_count == 1UL);
    calibration = TestSample_ValidCalibration();
    TEST_SAMPLE_CHECK(BMS_Sample_SetCalibration(&calibration));
    TestPhase8SampleStub_ClearObservation();
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(750UL));
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 750UL));
    TEST_SAMPLE_CHECK(snapshot.sample_sequence == 1UL);

    /* 同一 guard 覆盖 pack read 后立即发生的 transition。 */
    TestSample_Reset(true);
    g_phase8_sample_stub_control.inject_xready_after_pack_give = true;
    TEST_SAMPLE_CHECK(!BMS_Sample_RunOnce(250UL));
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.cell_call_count == 1UL);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.pack_call_count == 1UL);
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.xready_postcheck_reject_count == 1UL);
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 250UL));
    TEST_SAMPLE_CHECK(snapshot.sample_sequence == 0UL);
    g_phase8_sample_xready_pack_rejects =
        diagnostics.xready_postcheck_reject_count;

    /* 在 UINT32_MAX bind 后转移到 0，再清 active；旧 binding 仍失效直到重装 calibration。 */
    TestPhase8SampleStub_Reset();
    g_phase8_sample_stub_control.xready_state.xready_generation = UINT32_MAX;
    g_phase8_sample_stub_control.xready_state.active = false;
    BMS_Data_Init();
    BMS_Sample_Init();
    calibration = TestSample_ValidCalibration();
    BMS_Sample_SetDevice(TestPhase8SampleStub_Device());
    TEST_SAMPLE_CHECK(BMS_Sample_SetCalibration(&calibration));
    TEST_SAMPLE_CHECK(BMS_Sample_SetNtcTable(NULL, 0U));
    g_phase8_sample_stub_control.scheduler_state = taskSCHEDULER_RUNNING;
    TestSample_SetCc(true, (int16_t)123, (TickType_t)100U, 1UL);
    TestPhase8SampleStub_ClearObservation();
    g_phase8_sample_stub_control.inject_xready_after_pack_give = true;
    TEST_SAMPLE_CHECK(!BMS_Sample_RunOnce(250UL));
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_control.xready_state.xready_generation == 0UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_control.xready_state.active);
    g_phase8_sample_stub_control.xready_state.active = false;
    TestPhase8SampleStub_ClearObservation();
    TEST_SAMPLE_CHECK(!BMS_Sample_RunOnce(500UL));
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.xready_precheck_reject_count == 1UL);
    g_phase8_sample_xready_wrap_rejects =
        diagnostics.xready_precheck_reject_count;
    TEST_SAMPLE_CHECK(BMS_Sample_SetCalibration(&calibration));
    g_phase8_sample_stub_control.latest_cc.raw = (int16_t)123;
    g_phase8_sample_stub_control.latest_cc.sample_ms = (TickType_t)100U;
    g_phase8_sample_stub_control.latest_cc.sequence = 1UL;
    g_phase8_sample_stub_control.latest_cc.xready_generation = UINT32_MAX;
    g_phase8_sample_stub_control.latest_cc.valid = true;
    TestPhase8SampleStub_ClearObservation();
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(750UL));
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.current_convert_call_count == 0UL);
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 750UL));
    TEST_SAMPLE_CHECK(!snapshot.current_metadata.valid);
    TestSample_SetCc(true, (int16_t)123, (TickType_t)900U, 2UL);
    TestPhase8SampleStub_ClearObservation();
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(1000UL));
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 1000UL));
    TEST_SAMPLE_CHECK(snapshot.current_metadata.valid);

    /* final getter 唤醒的 ProtectTask 在外层 scheduler lock 下延后到 data publish 之后。 */
    TestSample_Reset(true);
    g_phase8_sample_stub_control
        .pend_xready_transition_on_final_guard = true;
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(250UL));
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.final_guard_outer_depth == 1UL);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.publish_outer_depth == 1UL);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.final_guard_scheduler_epoch ==
        g_phase8_sample_stub_observation.publish_scheduler_epoch);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.final_guard_get_order <
        g_phase8_sample_stub_observation.publish_take_order);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.publish_take_order <
        g_phase8_sample_stub_observation.pending_transition_apply_order);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation
            .pending_transition_deferred_count == 1UL);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.xready_transition_count == 1UL);
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 250UL));
    TEST_SAMPLE_CHECK(snapshot.sample_sequence == 1UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_control.xready_state.active);
    g_phase8_sample_xready_atomic_publishes = 1UL;
    g_phase8_sample_xready_guard_completed = 1UL;
}

static void TestSample_RecoveryCalibrationProvenance(void)
{
    BMS_DataSnapshot_t snapshot;
    BMS_SampleCalibrationEvidence_t evidence;
    BQ76940_Calibration_t cached_calibration;

    TestSample_Reset(true);
    cached_calibration = TestSample_ValidCalibration();

    /* 首次 runtime XREADY 永久关闭 legacy setter；cached pre-XREADY calibration
     * 不能在 active clear 后被重新贴成新 epoch。 */
    BMS_Sample_InvalidateCalibrationForXready(1UL);
    g_phase8_sample_stub_control.xready_state.xready_generation = 1UL;
    g_phase8_sample_stub_control.xready_state.active = false;
    TEST_SAMPLE_CHECK(!BMS_Sample_SetCalibration(&cached_calibration));
    TestPhase8SampleStub_ClearObservation();
    TEST_SAMPLE_CHECK(!BMS_Sample_RunOnce(250UL));
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.i2c_take_attempt_count == 0UL);

    evidence.xready_generation = 0UL;
    evidence.recovery_revision = 42UL;
    evidence.post_clear_verified = true;
    evidence.calibration = cached_calibration;
    TEST_SAMPLE_CHECK(!BMS_Sample_SetRecoveryCalibration(
        &evidence, 42UL, true));

    evidence.xready_generation = 1UL;
    evidence.post_clear_verified = false;
    TEST_SAMPLE_CHECK(!BMS_Sample_SetRecoveryCalibration(
        &evidence, 42UL, true));

    evidence.post_clear_verified = true;
    TEST_SAMPLE_CHECK(!BMS_Sample_SetRecoveryCalibration(
        &evidence, 41UL, true));
    TEST_SAMPLE_CHECK(!BMS_Sample_SetRecoveryCalibration(
        &evidence, 42UL, false));

    TEST_SAMPLE_CHECK(BMS_Sample_SetRecoveryCalibration(
        &evidence, 42UL, true));
    TestPhase8SampleStub_ClearObservation();
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(500UL));
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 500UL));
    TEST_SAMPLE_CHECK(snapshot.sample_sequence == 1UL);
    TEST_SAMPLE_CHECK(snapshot.afe_generation == 1UL);

    BMS_Sample_InvalidateCalibrationForXready(2UL);
    g_phase8_sample_stub_control.xready_state.xready_generation = 2UL;
    TEST_SAMPLE_CHECK(!BMS_Sample_SetRecoveryCalibration(
        &evidence, 42UL, true));
    TEST_SAMPLE_CHECK(!BMS_Sample_SetCalibration(&cached_calibration));
    g_phase8_sample_provenance_guard_completed = 1UL;
}

static void TestSample_RepeatedFailureRecovery(void)
{
    BMS_SampleDiagnostics_t diagnostics;
    uint32_t attempt;

    TestSample_Reset(true);
    g_phase8_sample_stub_control.cell_status = BQ76940_STATUS_I2C_ERROR;
    for (attempt = 0UL; attempt < 3UL; ++attempt)
    {
        TEST_SAMPLE_CHECK(!BMS_Sample_RunOnce(
            (BMS_TimestampMs_t)(attempt * BMS_SAMPLE_PERIOD_MS)));
    }
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.failure_count == 3UL);
    TEST_SAMPLE_CHECK(diagnostics.frame_reject_count == 3UL);
    TEST_SAMPLE_CHECK(diagnostics.cell_group_failure_count == 3UL);
    TEST_SAMPLE_CHECK(diagnostics.transport_failure_count == 3UL);
    TEST_SAMPLE_CHECK(diagnostics.consecutive_failure_count == 3UL);
    TEST_SAMPLE_CHECK(diagnostics.max_consecutive_failure_count == 3UL);
    g_phase8_sample_stub_control.cell_status = BQ76940_STATUS_OK;
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(750UL));
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.success_count == 1UL);
    TEST_SAMPLE_CHECK(diagnostics.failure_count == 3UL);
    TEST_SAMPLE_CHECK(diagnostics.consecutive_failure_count == 0UL);
    TEST_SAMPLE_CHECK(diagnostics.max_consecutive_failure_count == 3UL);
    TEST_SAMPLE_CHECK(TestPhase8SampleStub_LocksBalanced());
}

static void TestSample_ProtectI2cContentionModel(void)
{
    BMS_DataSnapshot_t snapshot;
    BMS_SampleDiagnostics_t diagnostics;

    /* 确定性组合：在 Sample driver stub 内执行 Protect-shaped bounded take，并在
     * Sample give 后立即 retry；证明 mutex ordering，不声称真实 preemptive timing。 */
    TestSample_Reset(true);
    g_phase8_sample_stub_control.inject_protect_i2c_contention = true;
    TEST_SAMPLE_CHECK(BMS_Sample_RunOnce(250UL));

    g_phase8_sample_contention_attempts =
        g_phase8_sample_stub_observation.protect_i2c_take_attempt_count;
    g_phase8_sample_contention_timeouts =
        g_phase8_sample_stub_observation.protect_i2c_timeout_count;
    g_phase8_sample_contention_successes =
        g_phase8_sample_stub_observation.protect_i2c_take_success_count;
    g_phase8_sample_contention_gives =
        g_phase8_sample_stub_observation.protect_i2c_give_count;
    g_phase8_sample_contention_wait_ticks =
        (uint32_t)g_phase8_sample_stub_observation.protect_last_wait_ticks;
    g_phase8_sample_contention_first_take_order =
        g_phase8_sample_stub_observation.protect_first_take_order;
    g_phase8_sample_contention_cell_give_order =
        g_phase8_sample_stub_observation.cell_give_order;
    g_phase8_sample_contention_retry_take_order =
        g_phase8_sample_stub_observation.protect_retry_take_order;
    g_phase8_sample_contention_retry_give_order =
        g_phase8_sample_stub_observation.protect_retry_give_order;
    g_phase8_sample_contention_pack_call_order =
        g_phase8_sample_stub_observation.pack_call_order;

    TEST_SAMPLE_CHECK(g_phase8_sample_contention_attempts == 2UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_contention_timeouts == 1UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_contention_successes == 1UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_contention_gives == 1UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation
                          .protect_i2c_while_sample_count == 1UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_contention_wait_ticks ==
                      (uint32_t)pdMS_TO_TICKS(
                          BMS_PROTECT_I2C_TIMEOUT_MS));
    TEST_SAMPLE_CHECK(g_phase8_sample_contention_first_take_order <
                      g_phase8_sample_contention_cell_give_order);
    TEST_SAMPLE_CHECK(g_phase8_sample_contention_cell_give_order <
                      g_phase8_sample_contention_retry_take_order);
    TEST_SAMPLE_CHECK(g_phase8_sample_contention_retry_take_order <
                      g_phase8_sample_contention_retry_give_order);
    TEST_SAMPLE_CHECK(g_phase8_sample_contention_retry_give_order <
                      g_phase8_sample_contention_pack_call_order);
    TEST_SAMPLE_CHECK(
        g_phase8_sample_stub_observation.lock_protocol_violation_count ==
        0UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.i2c_data_nesting_count ==
                      0UL);
    TEST_SAMPLE_CHECK(g_phase8_sample_stub_observation.driver_without_i2c_count ==
                      0UL);
    TEST_SAMPLE_CHECK(TestPhase8SampleStub_LocksBalanced());
    TEST_SAMPLE_CHECK(TestSample_Snapshot(&snapshot, 250UL));
    TEST_SAMPLE_CHECK(snapshot.sample_sequence == 1UL);
    diagnostics = BMS_Sample_GetDiagnostics();
    TEST_SAMPLE_CHECK(diagnostics.i2c_timeout_count == 0UL);
    g_phase8_sample_contention_completed = 1UL;
}

uint32_t Test_Phase8_Sample(void)
{
    g_phase8_sample_test_failures = 0UL;
    g_phase8_sample_test_completed = 0UL;
    g_phase8_sample_contention_completed = 0UL;
    g_phase8_sample_contention_attempts = 0UL;
    g_phase8_sample_contention_timeouts = 0UL;
    g_phase8_sample_contention_successes = 0UL;
    g_phase8_sample_contention_gives = 0UL;
    g_phase8_sample_contention_wait_ticks = 0UL;
    g_phase8_sample_contention_first_take_order = 0UL;
    g_phase8_sample_contention_cell_give_order = 0UL;
    g_phase8_sample_contention_retry_take_order = 0UL;
    g_phase8_sample_contention_retry_give_order = 0UL;
    g_phase8_sample_contention_pack_call_order = 0UL;
    g_phase8_sample_xready_guard_completed = 0UL;
    g_phase8_sample_xready_cell_rejects = 0UL;
    g_phase8_sample_xready_pack_rejects = 0UL;
    g_phase8_sample_xready_wrap_rejects = 0UL;
    g_phase8_sample_xready_atomic_publishes = 0UL;
    g_phase8_sample_provenance_guard_completed = 0UL;

    TestSample_SetterSchedulerGuard();
    TestSample_StartupAndSignedCurrent();
    TestSample_CurrentFailureAndCellRanges();
    TestSample_CoreFailuresRetainPrevious();
    TestSample_MutexTimeouts();
    TestSample_TemperatureCadenceAndCurve();
    TestSample_PublishContentionKeepsPending();
    TestSample_StaleAndProtectOrdering();
    TestSample_BoundedStaleProjection();
    TestSample_ConfigurationRevisionGuard();
    TestSample_ConfigurationRevisionImmediateWrapGuard();
    TestSample_XreadyGenerationGuard();
    TestSample_RecoveryCalibrationProvenance();
    TestSample_RepeatedFailureRecovery();
    TestSample_ProtectI2cContentionModel();

    g_phase8_sample_test_completed = 1UL;
    return g_phase8_sample_test_failures;
}
