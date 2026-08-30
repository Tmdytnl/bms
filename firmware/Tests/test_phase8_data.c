#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(BMS_PHASE8_HOST_TEST)
#include "test_phase8_data_host_shim.h"
#else
#include "apl_rtos.h"
#endif

#include "bms_data.h"
#include "bms_ntc.h"
#include "test_phase8_data.h"

struct QueueDefinition
{
    uint8_t marker;
};

enum
{
    TEST_DATA_GIVE_HOOK_NONE = 0,
    TEST_DATA_GIVE_HOOK_CAPTURE_PUBLISH,
    TEST_DATA_GIVE_HOOK_REPLACE_AFTER_READ
};

static struct QueueDefinition s_data_mutex_object;
static bool s_mutex_available;
static bool s_force_take_failure;
static uint32_t s_take_count;
static uint32_t s_give_count;
static uint8_t s_give_hook;
static BMS_DataSnapshot_t s_publish_observation;

SemaphoreHandle_t xDataMutex;
SemaphoreHandle_t xI2CMutex;

void vTaskSuspendAll(void)
{
}

BaseType_t xTaskResumeAll(void)
{
    return pdFALSE;
}

volatile uint32_t g_phase8_data_test_failures;
volatile uint32_t g_phase8_data_test_completed;

#define TEST_DATA_CHECK(condition)                    \
    do                                                \
    {                                                 \
        if (!(condition))                             \
        {                                             \
            ++g_phase8_data_test_failures;            \
        }                                             \
    } while (0)

static void TestData_ReplaceGlobalGeneration(void)
{
    uint32_t index;

    for (index = 0U; index < (uint32_t)BMS_CELL_COUNT; ++index)
    {
        g_bms_data.cell_voltage_mv[index] =
            (BMS_CellVoltageMv_t)(4100U + index);
        g_bms_data.cell_metadata.timestamp_ms[index] =
            (BMS_TimestampMs_t)900U;
    }
    g_bms_data.pack_voltage_mv = (BMS_PackVoltageMv_t)53378U;
    g_bms_data.bq_pack_voltage_mv = (BMS_PackVoltageMv_t)53380U;
    g_bms_data.pack_metadata.timestamp_ms = (BMS_TimestampMs_t)900U;
    g_bms_data.current_metadata.timestamp_ms = (BMS_TimestampMs_t)900U;
    g_bms_data.temperature_metadata.timestamp_ms =
        (BMS_TimestampMs_t)900U;
    g_bms_data.snapshot_timestamp_ms = (BMS_TimestampMs_t)900U;
    g_bms_data.sample_sequence = (uint32_t)77U;
}

BaseType_t xQueueSemaphoreTake(QueueHandle_t queue,
                               TickType_t ticks_to_wait)
{
    ++s_take_count;
    TEST_DATA_CHECK(ticks_to_wait == (TickType_t)0U);
    if ((queue != &s_data_mutex_object) || s_force_take_failure ||
        !s_mutex_available)
    {
        return pdFALSE;
    }
    s_mutex_available = false;
    return pdTRUE;
}

BaseType_t xQueueGenericSend(QueueHandle_t queue,
                             const void *item,
                             TickType_t ticks_to_wait,
                             BaseType_t copy_position)
{
    (void)item;
    (void)ticks_to_wait;
    (void)copy_position;
    ++s_give_count;
    if ((queue != &s_data_mutex_object) || s_mutex_available)
    {
        return pdFALSE;
    }

    if (s_give_hook == TEST_DATA_GIVE_HOOK_CAPTURE_PUBLISH)
    {
        s_publish_observation = g_bms_data;
    }
    else if (s_give_hook == TEST_DATA_GIVE_HOOK_REPLACE_AFTER_READ)
    {
        /* 模拟 mutex release 后立即运行的高优先级 writer。 */
        TestData_ReplaceGlobalGeneration();
    }
    s_give_hook = TEST_DATA_GIVE_HOOK_NONE;
    s_mutex_available = true;
    return pdTRUE;
}

static void TestData_Reset(void)
{
    s_data_mutex_object.marker = (uint8_t)0xA5U;
    xDataMutex = &s_data_mutex_object;
    s_mutex_available = true;
    s_force_take_failure = false;
    s_take_count = (uint32_t)0U;
    s_give_count = (uint32_t)0U;
    s_give_hook = TEST_DATA_GIVE_HOOK_NONE;
    (void)memset(&s_publish_observation, 0,
                 sizeof(s_publish_observation));
    BMS_Data_Init();
}

static BMS_MeasurementFrame_t TestData_MakeFrame(
    BMS_TimestampMs_t timestamp_ms,
    uint16_t cell_base_mv)
{
    BMS_MeasurementFrame_t frame;
    uint32_t index;

    (void)memset(&frame, 0, sizeof(frame));
    frame.timestamp_ms = timestamp_ms;
    for (index = 0U; index < (uint32_t)BMS_CELL_COUNT; ++index)
    {
        frame.cell_voltage_mv[index] =
            (BMS_CellVoltageMv_t)(cell_base_mv + (uint16_t)index);
    }
    frame.cell_valid_bitmap = BMS_CELL_DEFINED_MASK;
    frame.cell_in_range_bitmap = BMS_CELL_DEFINED_MASK;
    frame.bq_pack_voltage_mv =
        (BMS_PackVoltageMv_t)((uint32_t)cell_base_mv *
                              (uint32_t)BMS_CELL_COUNT + 80U);
    frame.bq_pack_valid = true;
    frame.bq_pack_in_range = true;

    frame.update_current = true;
    frame.current_ma = (BMS_CurrentMa_t)-1234;
    frame.current_timestamp_ms = timestamp_ms;
    frame.current_valid = true;
    frame.current_in_range = true;

    frame.update_temperature = true;
    frame.ts1_raw14 = (uint16_t)4321U;
    frame.ts1_resistance_ohm = (uint32_t)9876U;
    frame.temperature_timestamp_ms = timestamp_ms;
    frame.ts1_valid = true;
    frame.temperature_decic = (BMS_TemperatureDeciC_t)253;
    frame.temperature_valid = true;
    frame.temperature_in_range = true;
    return frame;
}

static BMS_PackVoltageMv_t TestData_ExpectedCellSum(uint16_t cell_base_mv)
{
    return (BMS_PackVoltageMv_t)(
        ((uint32_t)cell_base_mv * (uint32_t)BMS_CELL_COUNT) + 78U);
}

static void TestData_StartupIsInvalid(void)
{
    BMS_DataSnapshot_t snapshot;
    uint32_t index;

    TestData_Reset();
    (void)memset(&snapshot, 0x5A, sizeof(snapshot));
    TEST_DATA_CHECK(BMS_Data_GetSnapshot(&snapshot, 500U));
    TEST_DATA_CHECK(snapshot.sample_sequence == (uint32_t)0U);
    TEST_DATA_CHECK(snapshot.cell_metadata.valid_bitmap == (uint16_t)0U);
    TEST_DATA_CHECK(!snapshot.pack_metadata.valid);
    TEST_DATA_CHECK(!snapshot.bq_pack_metadata.valid);
    TEST_DATA_CHECK(!snapshot.current_metadata.valid);
    TEST_DATA_CHECK(!snapshot.ts1_metadata.valid);
    TEST_DATA_CHECK(!snapshot.temperature_metadata.valid);
    TEST_DATA_CHECK(snapshot.pack_metadata.age_ms ==
                    BMS_DATA_AGE_UNKNOWN_MS);
    for (index = 0U; index < (uint32_t)BMS_CELL_COUNT; ++index)
    {
        TEST_DATA_CHECK(snapshot.cell_metadata.age_ms[index] ==
                        BMS_DATA_AGE_UNKNOWN_MS);
    }
}

static void TestData_FullPublishAndNarrowOwnership(void)
{
    BMS_MeasurementFrame_t frame;
    BMS_DataSnapshot_t snapshot;
    BMS_FaultBitmap_t active_before;

    TestData_Reset();
    g_bms_data.state = BMS_STATE_FAULT;
    g_bms_data.soc_permille = (BMS_SocPermille_t)654U;
    g_bms_data.remaining_capacity_mah = (BMS_CapacityMah_t)12345U;
    active_before = (BMS_FaultBitmap_t)0x00000005UL;
    g_bms_data.faults.active = active_before;
    g_bms_data.faults.latched = (BMS_FaultBitmap_t)0x00000004UL;

    frame = TestData_MakeFrame((BMS_TimestampMs_t)1000U, 3000U);
    s_give_hook = TEST_DATA_GIVE_HOOK_CAPTURE_PUBLISH;
    TEST_DATA_CHECK(BMS_Data_PublishMeasurement(&frame));
    TEST_DATA_CHECK(s_take_count == (uint32_t)1U);
    TEST_DATA_CHECK(s_give_count == (uint32_t)1U);
    TEST_DATA_CHECK(s_publish_observation.sample_sequence == (uint32_t)1U);
    TEST_DATA_CHECK(s_publish_observation.cell_voltage_mv[0] == 3000U);
    TEST_DATA_CHECK(s_publish_observation.cell_voltage_mv[12] == 3012U);
    TEST_DATA_CHECK(s_publish_observation.pack_voltage_mv ==
                    TestData_ExpectedCellSum(3000U));

    TEST_DATA_CHECK(BMS_Data_GetSnapshot(&snapshot, 1250U));
    TEST_DATA_CHECK(snapshot.sample_sequence == (uint32_t)1U);
    TEST_DATA_CHECK(snapshot.snapshot_timestamp_ms == 1000U);
    TEST_DATA_CHECK(snapshot.pack_voltage_mv ==
                    TestData_ExpectedCellSum(3000U));
    TEST_DATA_CHECK(snapshot.bq_pack_voltage_mv ==
                    frame.bq_pack_voltage_mv);
    TEST_DATA_CHECK(snapshot.current_ma == (BMS_CurrentMa_t)-1234);
    TEST_DATA_CHECK(snapshot.ts1_raw14 == (uint16_t)4321U);
    TEST_DATA_CHECK(snapshot.ts1_resistance_ohm == (uint32_t)9876U);
    TEST_DATA_CHECK(snapshot.temperature_decic ==
                    (BMS_TemperatureDeciC_t)253);
    TEST_DATA_CHECK(snapshot.cell_metadata.valid_bitmap ==
                    BMS_CELL_DEFINED_MASK);
    TEST_DATA_CHECK(snapshot.pack_metadata.valid);
    TEST_DATA_CHECK(snapshot.bq_pack_metadata.valid);
    TEST_DATA_CHECK(snapshot.current_metadata.valid);
    TEST_DATA_CHECK(snapshot.ts1_metadata.valid);
    TEST_DATA_CHECK(snapshot.temperature_metadata.valid);
    TEST_DATA_CHECK(snapshot.pack_metadata.age_ms == 250U);
    TEST_DATA_CHECK(snapshot.current_metadata.age_ms == 250U);
    TEST_DATA_CHECK(snapshot.temperature_metadata.age_ms == 250U);

    TEST_DATA_CHECK(snapshot.state == BMS_STATE_FAULT);
    TEST_DATA_CHECK(snapshot.soc_permille == (BMS_SocPermille_t)654U);
    TEST_DATA_CHECK(snapshot.remaining_capacity_mah ==
                    (BMS_CapacityMah_t)12345U);
    TEST_DATA_CHECK(snapshot.faults.active == active_before);
    TEST_DATA_CHECK(snapshot.faults.latched ==
                    (BMS_FaultBitmap_t)0x00000004UL);
}

static void TestData_OptionalGroupsAreIndependent(void)
{
    BMS_MeasurementFrame_t first;
    BMS_MeasurementFrame_t second;
    BMS_DataSnapshot_t snapshot;

    TestData_Reset();
    first = TestData_MakeFrame(100U, 3000U);
    TEST_DATA_CHECK(BMS_Data_PublishMeasurement(&first));

    second = TestData_MakeFrame(350U, 3100U);
    second.update_current = false;
    second.update_temperature = false;
    TEST_DATA_CHECK(BMS_Data_PublishMeasurement(&second));
    TEST_DATA_CHECK(BMS_Data_GetSnapshot(&snapshot, 400U));

    TEST_DATA_CHECK(snapshot.sample_sequence == (uint32_t)2U);
    TEST_DATA_CHECK(snapshot.cell_voltage_mv[0] == 3100U);
    TEST_DATA_CHECK(snapshot.pack_metadata.timestamp_ms == 350U);
    TEST_DATA_CHECK(snapshot.pack_metadata.age_ms == 50U);
    TEST_DATA_CHECK(snapshot.current_ma == first.current_ma);
    TEST_DATA_CHECK(snapshot.current_metadata.timestamp_ms == 100U);
    TEST_DATA_CHECK(snapshot.current_metadata.age_ms == 300U);
    TEST_DATA_CHECK(snapshot.ts1_raw14 == first.ts1_raw14);
    TEST_DATA_CHECK(snapshot.temperature_metadata.timestamp_ms == 100U);
    TEST_DATA_CHECK(snapshot.temperature_metadata.age_ms == 300U);
}

static void TestData_RejectedFramesRetainPreviousGood(void)
{
    BMS_MeasurementFrame_t good;
    BMS_MeasurementFrame_t rejected;
    BMS_DataSnapshot_t before;

    TestData_Reset();
    good = TestData_MakeFrame(100U, 3000U);
    TEST_DATA_CHECK(BMS_Data_PublishMeasurement(&good));
    before = g_bms_data;

    rejected = TestData_MakeFrame(200U, 3100U);
    rejected.cell_valid_bitmap =
        (uint16_t)(BMS_CELL_DEFINED_MASK & (uint16_t)(~1U));
    rejected.cell_in_range_bitmap = rejected.cell_valid_bitmap;
    TEST_DATA_CHECK(!BMS_Data_PublishMeasurement(&rejected));
    TEST_DATA_CHECK(memcmp(&before, &g_bms_data, sizeof(before)) == 0);

    rejected = TestData_MakeFrame(200U, 3100U);
    rejected.bq_pack_valid = false;
    rejected.bq_pack_in_range = false;
    TEST_DATA_CHECK(!BMS_Data_PublishMeasurement(&rejected));
    TEST_DATA_CHECK(memcmp(&before, &g_bms_data, sizeof(before)) == 0);

    rejected = TestData_MakeFrame(200U, 3100U);
    rejected.cell_in_range_bitmap = (uint16_t)0x8000U;
    TEST_DATA_CHECK(!BMS_Data_PublishMeasurement(&rejected));
    TEST_DATA_CHECK(memcmp(&before, &g_bms_data, sizeof(before)) == 0);

    rejected = TestData_MakeFrame(200U, 3100U);
    rejected.current_valid = false;
    rejected.current_in_range = true;
    TEST_DATA_CHECK(!BMS_Data_PublishMeasurement(&rejected));
    TEST_DATA_CHECK(memcmp(&before, &g_bms_data, sizeof(before)) == 0);

    rejected = TestData_MakeFrame(200U, 3100U);
    rejected.ts1_valid = false;
    TEST_DATA_CHECK(!BMS_Data_PublishMeasurement(&rejected));
    TEST_DATA_CHECK(memcmp(&before, &g_bms_data, sizeof(before)) == 0);

    rejected = TestData_MakeFrame(200U, 3100U);
    rejected.ts1_raw14 = (uint16_t)0x4000U;
    TEST_DATA_CHECK(!BMS_Data_PublishMeasurement(&rejected));
    TEST_DATA_CHECK(memcmp(&before, &g_bms_data, sizeof(before)) == 0);

    TEST_DATA_CHECK(!BMS_Data_PublishMeasurement(NULL));
    TEST_DATA_CHECK(memcmp(&before, &g_bms_data, sizeof(before)) == 0);

    s_force_take_failure = true;
    rejected = TestData_MakeFrame(200U, 3100U);
    TEST_DATA_CHECK(!BMS_Data_PublishMeasurement(&rejected));
    TEST_DATA_CHECK(memcmp(&before, &g_bms_data, sizeof(before)) == 0);
    s_force_take_failure = false;

    xDataMutex = NULL;
    TEST_DATA_CHECK(!BMS_Data_PublishMeasurement(&rejected));
    TEST_DATA_CHECK(memcmp(&before, &g_bms_data, sizeof(before)) == 0);
}

static void TestData_FreshnessAndWrap(void)
{
    BMS_MeasurementFrame_t frame;
    BMS_DataSnapshot_t snapshot;
    BMS_TimestampMs_t timestamp;

    TestData_Reset();
    timestamp = (BMS_TimestampMs_t)(UINT32_MAX - 99UL);
    frame = TestData_MakeFrame(timestamp, 3000U);
    TEST_DATA_CHECK(BMS_Data_PublishMeasurement(&frame));
    TEST_DATA_CHECK(BMS_Data_GetSnapshot(&snapshot, 50U));
    TEST_DATA_CHECK(snapshot.pack_metadata.age_ms == 150U);
    TEST_DATA_CHECK(snapshot.current_metadata.age_ms == 150U);
    TEST_DATA_CHECK(snapshot.temperature_metadata.age_ms == 150U);
    TEST_DATA_CHECK(!snapshot.pack_metadata.stale_latched);
    TEST_DATA_CHECK(!snapshot.current_metadata.stale_latched);
    TEST_DATA_CHECK(!snapshot.temperature_metadata.stale_latched);

    TEST_DATA_CHECK(BMS_Data_IsFresh(true, false, 1000U,
                                    BMS_DATA_VOLTAGE_FRESH_MAX_MS));
    TEST_DATA_CHECK(!BMS_Data_IsFresh(true, false, 1001U,
                                     BMS_DATA_VOLTAGE_FRESH_MAX_MS));
    TEST_DATA_CHECK(!BMS_Data_IsFresh(false, false, 0U,
                                     BMS_DATA_VOLTAGE_FRESH_MAX_MS));
    TEST_DATA_CHECK(!BMS_Data_IsFresh(true, true, 0U,
                                     BMS_DATA_VOLTAGE_FRESH_MAX_MS));
    TEST_DATA_CHECK(!BMS_Data_IsFresh(true, false,
                                     BMS_DATA_AGE_UNKNOWN_MS,
                                     BMS_DATA_VOLTAGE_FRESH_MAX_MS));
    TEST_DATA_CHECK(!BMS_Data_IsFresh(true, false, 0U,
                                     BMS_DATA_AGE_UNKNOWN_MS));

    TestData_Reset();
    frame = TestData_MakeFrame(100U, 3000U);
    TEST_DATA_CHECK(BMS_Data_PublishMeasurement(&frame));
    TEST_DATA_CHECK(BMS_Data_GetSnapshot(&snapshot, 1101U));
    TEST_DATA_CHECK(snapshot.pack_metadata.valid);
    TEST_DATA_CHECK(snapshot.pack_metadata.age_ms == 1001U);
    TEST_DATA_CHECK(snapshot.pack_metadata.stale_latched);
    TEST_DATA_CHECK(snapshot.cell_metadata.stale_bitmap ==
                    BMS_CELL_DEFINED_MASK);
    TEST_DATA_CHECK(!BMS_Data_IsFresh(snapshot.pack_metadata.valid,
                                     snapshot.pack_metadata.stale_latched,
                                     snapshot.pack_metadata.age_ms,
                                     BMS_DATA_VOLTAGE_FRESH_MAX_MS));
}

static void TestData_StickyStaleCannotResurrectAfterWrap(void)
{
    BMS_MeasurementFrame_t first;
    BMS_MeasurementFrame_t core_only;
    BMS_MeasurementFrame_t all_groups;
    BMS_DataSnapshot_t snapshot;

    TestData_Reset();
    first = TestData_MakeFrame(100U, 3000U);
    TEST_DATA_CHECK(BMS_Data_PublishMeasurement(&first));

    /* 第一次观察 freshness threshold crossing 时锁存每个 valid group。 */
    TEST_DATA_CHECK(BMS_Data_GetSnapshot(&snapshot, 5101U));
    TEST_DATA_CHECK(snapshot.pack_metadata.stale_latched);
    TEST_DATA_CHECK(snapshot.bq_pack_metadata.stale_latched);
    TEST_DATA_CHECK(snapshot.current_metadata.stale_latched);
    TEST_DATA_CHECK(snapshot.ts1_metadata.stale_latched);
    TEST_DATA_CHECK(snapshot.temperature_metadata.stale_latched);
    TEST_DATA_CHECK(snapshot.cell_metadata.stale_bitmap ==
                    BMS_CELL_DEFINED_MASK);

    /* 完整 uint32_t wrap 会让算术 age 变小，但 sticky state 保持旧代 non-fresh。 */
    TEST_DATA_CHECK(BMS_Data_GetSnapshot(&snapshot, 150U));
    TEST_DATA_CHECK(snapshot.pack_metadata.age_ms == 50U);
    TEST_DATA_CHECK(snapshot.pack_metadata.stale_latched);
    TEST_DATA_CHECK(!BMS_Data_IsFresh(snapshot.pack_metadata.valid,
                                     snapshot.pack_metadata.stale_latched,
                                     snapshot.pack_metadata.age_ms,
                                     BMS_DATA_VOLTAGE_FRESH_MAX_MS));

    /* core-only publication 只恢复 core voltage group。 */
    core_only = TestData_MakeFrame(200U, 3100U);
    core_only.update_current = false;
    core_only.update_temperature = false;
    TEST_DATA_CHECK(BMS_Data_PublishMeasurement(&core_only));
    TEST_DATA_CHECK(BMS_Data_GetSnapshot(&snapshot, 200U));
    TEST_DATA_CHECK(!snapshot.pack_metadata.stale_latched);
    TEST_DATA_CHECK(!snapshot.bq_pack_metadata.stale_latched);
    TEST_DATA_CHECK(snapshot.cell_metadata.stale_bitmap == (uint16_t)0U);
    TEST_DATA_CHECK(snapshot.current_metadata.stale_latched);
    TEST_DATA_CHECK(snapshot.ts1_metadata.stale_latched);
    TEST_DATA_CHECK(snapshot.temperature_metadata.stale_latched);
    TEST_DATA_CHECK(BMS_Data_IsFresh(snapshot.pack_metadata.valid,
                                    snapshot.pack_metadata.stale_latched,
                                    snapshot.pack_metadata.age_ms,
                                    BMS_DATA_VOLTAGE_FRESH_MAX_MS));

    /* 只有重新发布对应 optional group 才能清各自 latch。 */
    all_groups = TestData_MakeFrame(250U, 3200U);
    TEST_DATA_CHECK(BMS_Data_PublishMeasurement(&all_groups));
    TEST_DATA_CHECK(BMS_Data_GetSnapshot(&snapshot, 250U));
    TEST_DATA_CHECK(!snapshot.current_metadata.stale_latched);
    TEST_DATA_CHECK(!snapshot.ts1_metadata.stale_latched);
    TEST_DATA_CHECK(!snapshot.temperature_metadata.stale_latched);
}

static void TestData_ValidityAndRangeRemainSeparate(void)
{
    BMS_MeasurementFrame_t frame;
    BMS_DataSnapshot_t snapshot;

    TestData_Reset();
    frame = TestData_MakeFrame(100U, 3000U);
    frame.cell_in_range_bitmap =
        (uint16_t)(BMS_CELL_DEFINED_MASK & (uint16_t)(~1U));
    frame.temperature_valid = false;
    frame.temperature_in_range = false;
    TEST_DATA_CHECK(BMS_Data_PublishMeasurement(&frame));
    TEST_DATA_CHECK(BMS_Data_GetSnapshot(&snapshot, 100U));
    TEST_DATA_CHECK(snapshot.cell_metadata.valid_bitmap ==
                    BMS_CELL_DEFINED_MASK);
    TEST_DATA_CHECK(snapshot.cell_metadata.in_range_bitmap ==
                    frame.cell_in_range_bitmap);
    TEST_DATA_CHECK(snapshot.pack_metadata.valid);
    TEST_DATA_CHECK(!snapshot.pack_metadata.in_range);
    TEST_DATA_CHECK(snapshot.ts1_metadata.valid);
    TEST_DATA_CHECK(!snapshot.temperature_metadata.valid);
    TEST_DATA_CHECK(snapshot.temperature_metadata.age_ms ==
                    BMS_DATA_AGE_UNKNOWN_MS);
}

static void TestData_SequenceWrapsNaturally(void)
{
    BMS_MeasurementFrame_t frame;

    TestData_Reset();
    g_bms_data.sample_sequence = UINT32_MAX;
    frame = TestData_MakeFrame(100U, 3000U);
    TEST_DATA_CHECK(BMS_Data_PublishMeasurement(&frame));
    TEST_DATA_CHECK(g_bms_data.sample_sequence == (uint32_t)0U);
}

static void TestData_ReaderReturnsOneGeneration(void)
{
    BMS_MeasurementFrame_t frame;
    BMS_DataSnapshot_t snapshot;

    TestData_Reset();
    frame = TestData_MakeFrame(100U, 3000U);
    TEST_DATA_CHECK(BMS_Data_PublishMeasurement(&frame));

    s_give_hook = TEST_DATA_GIVE_HOOK_REPLACE_AFTER_READ;
    TEST_DATA_CHECK(BMS_Data_GetSnapshot(&snapshot, 150U));
    TEST_DATA_CHECK(snapshot.sample_sequence == (uint32_t)1U);
    TEST_DATA_CHECK(snapshot.cell_voltage_mv[0] == 3000U);
    TEST_DATA_CHECK(snapshot.cell_voltage_mv[12] == 3012U);
    TEST_DATA_CHECK(snapshot.pack_voltage_mv ==
                    TestData_ExpectedCellSum(3000U));
    TEST_DATA_CHECK(snapshot.pack_metadata.timestamp_ms == 100U);
    TEST_DATA_CHECK(snapshot.pack_metadata.age_ms == 50U);
    TEST_DATA_CHECK(g_bms_data.sample_sequence == (uint32_t)77U);
    TEST_DATA_CHECK(g_bms_data.cell_voltage_mv[0] == 4100U);
}

static void TestData_FailedReadLeavesOutput(void)
{
    BMS_DataSnapshot_t output;
    BMS_DataSnapshot_t before;
    BMS_DataFreshnessSnapshot_t freshness;
    BMS_DataFreshnessSnapshot_t freshness_before;

    TestData_Reset();
    (void)memset(&output, 0xC3, sizeof(output));
    before = output;
    s_force_take_failure = true;
    TEST_DATA_CHECK(!BMS_Data_GetSnapshot(&output, 100U));
    TEST_DATA_CHECK(memcmp(&before, &output, sizeof(output)) == 0);
    (void)memset(&freshness, 0x6D, sizeof(freshness));
    freshness_before = freshness;
    TEST_DATA_CHECK(!BMS_Data_GetFreshnessSnapshot(&freshness, 100U));
    TEST_DATA_CHECK(memcmp(&freshness_before, &freshness,
                           sizeof(freshness)) == 0);
    s_force_take_failure = false;
    TEST_DATA_CHECK(!BMS_Data_GetSnapshot(NULL, 100U));
    TEST_DATA_CHECK(!BMS_Data_GetFreshnessSnapshot(NULL, 100U));
}

static void TestData_FreshnessProjectionIsOneGeneration(void)
{
    BMS_MeasurementFrame_t frame;
    BMS_DataFreshnessSnapshot_t snapshot;
    BMS_TimestampMs_t timestamp;

    TestData_Reset();
    timestamp = (BMS_TimestampMs_t)(UINT32_MAX - 99UL);
    frame = TestData_MakeFrame(timestamp, 3000U);
    TEST_DATA_CHECK(BMS_Data_PublishMeasurement(&frame));
    s_give_hook = TEST_DATA_GIVE_HOOK_REPLACE_AFTER_READ;
    TEST_DATA_CHECK(BMS_Data_GetFreshnessSnapshot(&snapshot, 50U));
    TEST_DATA_CHECK(sizeof(snapshot) <=
                    BMS_DATA_FRESHNESS_SNAPSHOT_MAX_BYTES);
    TEST_DATA_CHECK(snapshot.sample_sequence == 1UL);
    TEST_DATA_CHECK(snapshot.pack_metadata.timestamp_ms == timestamp);
    TEST_DATA_CHECK(snapshot.current_metadata.timestamp_ms == timestamp);
    TEST_DATA_CHECK(snapshot.temperature_metadata.timestamp_ms == timestamp);
    TEST_DATA_CHECK(snapshot.pack_metadata.age_ms == 150U);
    TEST_DATA_CHECK(snapshot.current_metadata.age_ms == 150U);
    TEST_DATA_CHECK(snapshot.temperature_metadata.age_ms == 150U);
    TEST_DATA_CHECK(!snapshot.pack_metadata.stale_latched);
    TEST_DATA_CHECK(g_bms_data.sample_sequence == 77UL);
    TEST_DATA_CHECK(g_bms_data.pack_metadata.timestamp_ms == 900U);
}

static void TestData_NtcValidationAndInterpolation(void)
{
    static const BMS_NtcPoint_t ascending_resistance[] =
    {
        {1000UL, (BMS_TemperatureDeciC_t)800},
        {2000UL, (BMS_TemperatureDeciC_t)400},
        {4000UL, (BMS_TemperatureDeciC_t)0}
    };
    static const BMS_NtcPoint_t descending_resistance[] =
    {
        {4000UL, (BMS_TemperatureDeciC_t)0},
        {2000UL, (BMS_TemperatureDeciC_t)400},
        {1000UL, (BMS_TemperatureDeciC_t)800}
    };
    static const BMS_NtcPoint_t duplicate_resistance[] =
    {
        {1000UL, (BMS_TemperatureDeciC_t)800},
        {1000UL, (BMS_TemperatureDeciC_t)400}
    };
    static const BMS_NtcPoint_t non_ntc_direction[] =
    {
        {1000UL, (BMS_TemperatureDeciC_t)0},
        {2000UL, (BMS_TemperatureDeciC_t)100}
    };
    static const BMS_NtcPoint_t wide_range[] =
    {
        {0UL, (BMS_TemperatureDeciC_t)32767},
        {UINT32_MAX, (BMS_TemperatureDeciC_t)-32768}
    };
    BMS_TemperatureDeciC_t temperature;

    TEST_DATA_CHECK(BMS_Ntc_ValidateTable(ascending_resistance, 3U));
    TEST_DATA_CHECK(BMS_Ntc_ValidateTable(descending_resistance, 3U));
    TEST_DATA_CHECK(!BMS_Ntc_ValidateTable(NULL, 3U));
    TEST_DATA_CHECK(!BMS_Ntc_ValidateTable(ascending_resistance, 1U));
    TEST_DATA_CHECK(!BMS_Ntc_ValidateTable(duplicate_resistance, 2U));
    TEST_DATA_CHECK(!BMS_Ntc_ValidateTable(non_ntc_direction, 2U));

    temperature = (BMS_TemperatureDeciC_t)-1;
    TEST_DATA_CHECK(BMS_Ntc_Interpolate(ascending_resistance, 3U,
                                       1500UL, &temperature));
    TEST_DATA_CHECK(temperature == (BMS_TemperatureDeciC_t)600);
    TEST_DATA_CHECK(BMS_Ntc_Interpolate(descending_resistance, 3U,
                                       3000UL, &temperature));
    TEST_DATA_CHECK(temperature == (BMS_TemperatureDeciC_t)200);
    TEST_DATA_CHECK(BMS_Ntc_Interpolate(ascending_resistance, 3U,
                                       1000UL, &temperature));
    TEST_DATA_CHECK(temperature == (BMS_TemperatureDeciC_t)800);

    temperature = (BMS_TemperatureDeciC_t)123;
    TEST_DATA_CHECK(!BMS_Ntc_Interpolate(ascending_resistance, 3U,
                                        999UL, &temperature));
    TEST_DATA_CHECK(temperature == (BMS_TemperatureDeciC_t)123);
    TEST_DATA_CHECK(!BMS_Ntc_Interpolate(duplicate_resistance, 2U,
                                        1000UL, &temperature));
    TEST_DATA_CHECK(temperature == (BMS_TemperatureDeciC_t)123);
    TEST_DATA_CHECK(!BMS_Ntc_Interpolate(ascending_resistance, 3U,
                                        1500UL, NULL));

    TEST_DATA_CHECK(BMS_Ntc_Interpolate(wide_range, 2U,
                                       UINT32_MAX / 2UL, &temperature));
    TEST_DATA_CHECK(temperature == (BMS_TemperatureDeciC_t)0);
}

uint32_t Test_Phase8_Data(void)
{
    g_phase8_data_test_failures = (uint32_t)0U;
    g_phase8_data_test_completed = (uint32_t)0U;

    TestData_StartupIsInvalid();
    TestData_FullPublishAndNarrowOwnership();
    TestData_OptionalGroupsAreIndependent();
    TestData_RejectedFramesRetainPreviousGood();
    TestData_FreshnessAndWrap();
    TestData_StickyStaleCannotResurrectAfterWrap();
    TestData_ValidityAndRangeRemainSeparate();
    TestData_SequenceWrapsNaturally();
    TestData_ReaderReturnsOneGeneration();
    TestData_FailedReadLeavesOutput();
    TestData_FreshnessProjectionIsOneGeneration();
    TestData_NtcValidationAndInterpolation();

    g_phase8_data_test_completed = (uint32_t)1U;
    return g_phase8_data_test_failures;
}

#if defined(BMS_PHASE8_DATA_STANDALONE)
int main(void)
{
    return (Test_Phase8_Data() == (uint32_t)0U) ? 0 : 1;
}
#endif
