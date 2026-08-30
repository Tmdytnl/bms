#include "bms_data.h"

#include <limits.h>

#include "apl_rtos.h"

BMS_DataSnapshot_t g_bms_data;

/*
 * 本模块把多个 owner 的诊断投影汇成一致快照，但不承担安全仲裁。
 * measurement 发布是唯一会推进 sample_sequence 的路径；afe_generation
 * 把序号限定在当前 AFE 生命周期，使 reset 前的数据不能被 reset 后继续采用。
 */

#define BMS_DATA_TS1_RAW14_MAX              ((uint16_t)0x3FFFU)

static void BMS_Data_InitMeasurement(BMS_MeasurementMetadata_t *metadata)
{
    metadata->timestamp_ms = (BMS_TimestampMs_t)0U;
    metadata->age_ms = BMS_DATA_AGE_UNKNOWN_MS;
    metadata->valid = false;
    metadata->in_range = false;
    metadata->stale_latched = false;
}

void BMS_Data_Init(void)
{
    uint32_t cell_index;

    for (cell_index = 0U;
         cell_index < (uint32_t)BMS_CELL_COUNT;
         cell_index++)
    {
        g_bms_data.cell_voltage_mv[cell_index] = (BMS_CellVoltageMv_t)0U;
        g_bms_data.cell_metadata.timestamp_ms[cell_index] =
            (BMS_TimestampMs_t)0U;
        g_bms_data.cell_metadata.age_ms[cell_index] =
            BMS_DATA_AGE_UNKNOWN_MS;
    }

    g_bms_data.cell_metadata.valid_bitmap = (uint16_t)0U;
    g_bms_data.cell_metadata.in_range_bitmap = (uint16_t)0U;
    g_bms_data.cell_metadata.stale_bitmap = (uint16_t)0U;

    g_bms_data.pack_voltage_mv = (BMS_PackVoltageMv_t)0U;
    g_bms_data.bq_pack_voltage_mv = (BMS_PackVoltageMv_t)0U;
    g_bms_data.current_ma = (BMS_CurrentMa_t)0;
    g_bms_data.ts1_raw14 = (uint16_t)0U;
    g_bms_data.ts1_resistance_ohm = (uint32_t)0U;
    g_bms_data.temperature_decic = (BMS_TemperatureDeciC_t)0;
    g_bms_data.remaining_capacity_mah = (BMS_CapacityMah_t)0U;
    g_bms_data.soc_permille = BMS_SOC_UNKNOWN_PERMILLE;

    g_bms_data.state = BMS_STATE_INIT;
    BMS_Fault_Init(&g_bms_data.faults);

    BMS_Data_InitMeasurement(&g_bms_data.pack_metadata);
    BMS_Data_InitMeasurement(&g_bms_data.bq_pack_metadata);
    BMS_Data_InitMeasurement(&g_bms_data.current_metadata);
    BMS_Data_InitMeasurement(&g_bms_data.ts1_metadata);
    BMS_Data_InitMeasurement(&g_bms_data.temperature_metadata);
    BMS_Data_InitMeasurement(&g_bms_data.soc_metadata);

    g_bms_data.snapshot_timestamp_ms = (BMS_TimestampMs_t)0U;
    g_bms_data.sample_sequence = (uint32_t)0U;
    g_bms_data.afe_generation = (uint32_t)0U;
}

static bool BMS_Data_FrameIsValid(const BMS_MeasurementFrame_t *frame)
{
    uint16_t undefined_valid_bits;
    uint16_t undefined_range_bits;

    /*
     * 发布前先验证 staging 内部关系，而不只是检查单个范围：未定义 bitmap 位
     * 必须为零，in_range 不能在 valid=0 时成立，temperature 必须建立在有效
     * TS1 证据上。这样错误 producer 不能把自相矛盾的元数据写进共享快照。
     */
    if (frame == NULL)
    {
        return false;
    }

    undefined_valid_bits =
        (uint16_t)(frame->cell_valid_bitmap &
                   (uint16_t)(~BMS_CELL_DEFINED_MASK));
    undefined_range_bits =
        (uint16_t)(frame->cell_in_range_bitmap &
                   (uint16_t)(~BMS_CELL_DEFINED_MASK));
    if ((undefined_valid_bits != (uint16_t)0U) ||
        (undefined_range_bits != (uint16_t)0U) ||
        (frame->cell_valid_bitmap != BMS_CELL_DEFINED_MASK) ||
        ((frame->cell_in_range_bitmap &
          (uint16_t)(~frame->cell_valid_bitmap)) != (uint16_t)0U) ||
        !frame->bq_pack_valid)
    {
        return false;
    }
    if (frame->update_current && frame->current_in_range &&
        !frame->current_valid)
    {
        return false;
    }
    if (frame->update_temperature)
    {
        if ((frame->ts1_valid &&
             (frame->ts1_raw14 > BMS_DATA_TS1_RAW14_MAX)) ||
            (frame->temperature_valid && !frame->ts1_valid) ||
            (frame->temperature_in_range &&
             !frame->temperature_valid))
        {
            return false;
        }
    }
    return true;
}

static BMS_DataAgeMs_t BMS_Data_AgeAtPublication(bool valid)
{
    return valid ? (BMS_DataAgeMs_t)0U : BMS_DATA_AGE_UNKNOWN_MS;
}

static void BMS_Data_PublishMetadata(BMS_MeasurementMetadata_t *metadata,
                                     BMS_TimestampMs_t timestamp_ms,
                                     bool valid,
                                     bool in_range)
{
    metadata->timestamp_ms = timestamp_ms;
    metadata->age_ms = BMS_Data_AgeAtPublication(valid);
    metadata->valid = valid;
    metadata->in_range = valid && in_range;
    metadata->stale_latched = false;
}

bool BMS_Data_PublishMeasurement(const BMS_MeasurementFrame_t *frame)
{
    BMS_PackVoltageMv_t pack_sum_mv;
    uint32_t cell_index;
    bool pack_valid;
    bool pack_in_range;

    /*
     * 先在 mutex 外完成校验与 cell sum，缩短共享数据锁的持有时间；只有 staging
     * 完整合法且能立即取得 xDataMutex，才一次替换 measurement-owned 字段。
     * 任一失败都保留上一帧，绝不把“新电芯+旧包压”之类半帧暴露给读者。
     */
    if (!BMS_Data_FrameIsValid(frame))
    {
        return false;
    }

    pack_sum_mv = (BMS_PackVoltageMv_t)0U;
    for (cell_index = 0U;
         cell_index < (uint32_t)BMS_CELL_COUNT;
         ++cell_index)
    {
        if ((UINT32_MAX - pack_sum_mv) <
            (uint32_t)frame->cell_voltage_mv[cell_index])
        {
            return false;
        }
        pack_sum_mv +=
            (BMS_PackVoltageMv_t)frame->cell_voltage_mv[cell_index];
    }

    if ((xDataMutex == NULL) ||
        (xSemaphoreTake(xDataMutex, (TickType_t)0U) != pdTRUE))
    {
        return false;
    }

    for (cell_index = 0U;
         cell_index < (uint32_t)BMS_CELL_COUNT;
         ++cell_index)
    {
        uint16_t cell_mask;
        bool cell_valid;

        cell_mask = (uint16_t)((uint16_t)1U << cell_index);
        cell_valid =
            (frame->cell_valid_bitmap & cell_mask) != (uint16_t)0U;
        g_bms_data.cell_voltage_mv[cell_index] =
            frame->cell_voltage_mv[cell_index];
        g_bms_data.cell_metadata.timestamp_ms[cell_index] =
            frame->timestamp_ms;
        g_bms_data.cell_metadata.age_ms[cell_index] =
            BMS_Data_AgeAtPublication(cell_valid);
    }
    g_bms_data.cell_metadata.valid_bitmap = frame->cell_valid_bitmap;
    g_bms_data.cell_metadata.in_range_bitmap =
        frame->cell_in_range_bitmap;
    g_bms_data.cell_metadata.stale_bitmap = (uint16_t)0U;

    pack_valid =
        frame->cell_valid_bitmap == BMS_CELL_DEFINED_MASK;
    pack_in_range = pack_valid &&
        (frame->cell_in_range_bitmap == BMS_CELL_DEFINED_MASK);
    g_bms_data.pack_voltage_mv = pack_sum_mv;
    BMS_Data_PublishMetadata(&g_bms_data.pack_metadata,
                             frame->timestamp_ms,
                             pack_valid,
                             pack_in_range);

    g_bms_data.bq_pack_voltage_mv = frame->bq_pack_voltage_mv;
    BMS_Data_PublishMetadata(&g_bms_data.bq_pack_metadata,
                             frame->timestamp_ms,
                             frame->bq_pack_valid,
                             frame->bq_pack_in_range);

    if (frame->update_current)
    {
        g_bms_data.current_ma = frame->current_ma;
        BMS_Data_PublishMetadata(&g_bms_data.current_metadata,
                                 frame->current_timestamp_ms,
                                 frame->current_valid,
                                 frame->current_in_range);
    }

    if (frame->update_temperature)
    {
        g_bms_data.ts1_raw14 = frame->ts1_raw14;
        g_bms_data.ts1_resistance_ohm = frame->ts1_resistance_ohm;
        BMS_Data_PublishMetadata(&g_bms_data.ts1_metadata,
                                 frame->temperature_timestamp_ms,
                                 frame->ts1_valid,
                                 frame->ts1_valid);
        g_bms_data.temperature_decic = frame->temperature_decic;
        BMS_Data_PublishMetadata(&g_bms_data.temperature_metadata,
                                 frame->temperature_timestamp_ms,
                                 frame->temperature_valid,
                                 frame->temperature_in_range);
    }

    /* sequence 最后推进，表示前面的 mandatory core 已全部写入同一 generation。 */
    g_bms_data.snapshot_timestamp_ms = frame->timestamp_ms;
    g_bms_data.afe_generation = frame->afe_generation;
    ++g_bms_data.sample_sequence;

    (void)xSemaphoreGive(xDataMutex);
    return true;
}

static BMS_DataAgeMs_t BMS_Data_DeriveAge(bool valid,
                                         BMS_TimestampMs_t timestamp_ms,
                                         BMS_TimestampMs_t now_ms)
{
    if (!valid)
    {
        return BMS_DATA_AGE_UNKNOWN_MS;
    }
    return (BMS_DataAgeMs_t)(now_ms - timestamp_ms);
}

static void BMS_Data_DeriveMetadataAge(
    BMS_MeasurementMetadata_t *metadata,
    BMS_TimestampMs_t now_ms)
{
    metadata->age_ms = BMS_Data_DeriveAge(metadata->valid,
                                          metadata->timestamp_ms,
                                          now_ms);
}

static void BMS_Data_LatchMetadataStale(
    BMS_MeasurementMetadata_t *metadata,
    BMS_TimestampMs_t now_ms,
    BMS_DataAgeMs_t max_age_ms)
{
    if (metadata->valid && !metadata->stale_latched &&
        (BMS_Data_DeriveAge(true, metadata->timestamp_ms, now_ms) >
         max_age_ms))
    {
        metadata->stale_latched = true;
    }
}

/*
 * 只能在持有 xDataMutex 时调用。周期读者把第一次 freshness 门限跨越变成
 * sticky 状态，阻止后续 uint32_t 时间戳回绕让旧数据重新显得新鲜。
 * 若读者停顿整整一个时间戳周期，32-bit clock 本身无法区分，由 watchdog 覆盖。
 */
static void BMS_Data_LatchStale(BMS_TimestampMs_t now_ms)
{
    uint32_t cell_index;

    for (cell_index = 0U;
         cell_index < (uint32_t)BMS_CELL_COUNT;
         ++cell_index)
    {
        uint16_t cell_mask;
        BMS_DataAgeMs_t cell_age_ms;

        cell_mask = (uint16_t)((uint16_t)1U << cell_index);
        if (((g_bms_data.cell_metadata.valid_bitmap & cell_mask) !=
             (uint16_t)0U) &&
            ((g_bms_data.cell_metadata.stale_bitmap & cell_mask) ==
             (uint16_t)0U))
        {
            cell_age_ms = BMS_Data_DeriveAge(
                true,
                g_bms_data.cell_metadata.timestamp_ms[cell_index],
                now_ms);
            if (cell_age_ms > BMS_DATA_VOLTAGE_FRESH_MAX_MS)
            {
                g_bms_data.cell_metadata.stale_bitmap |= cell_mask;
            }
        }
    }

    BMS_Data_LatchMetadataStale(&g_bms_data.pack_metadata, now_ms,
                                BMS_DATA_VOLTAGE_FRESH_MAX_MS);
    BMS_Data_LatchMetadataStale(&g_bms_data.bq_pack_metadata, now_ms,
                                BMS_DATA_VOLTAGE_FRESH_MAX_MS);
    BMS_Data_LatchMetadataStale(&g_bms_data.current_metadata, now_ms,
                                BMS_DATA_CURRENT_FRESH_MAX_MS);
    BMS_Data_LatchMetadataStale(&g_bms_data.ts1_metadata, now_ms,
                                BMS_DATA_TEMPERATURE_FRESH_MAX_MS);
    BMS_Data_LatchMetadataStale(&g_bms_data.temperature_metadata, now_ms,
                                BMS_DATA_TEMPERATURE_FRESH_MAX_MS);
}

bool BMS_Data_GetSnapshot(BMS_DataSnapshot_t *snapshot,
                          BMS_TimestampMs_t now_ms)
{
    uint32_t cell_index;

    /*
     * 锁内先更新 sticky stale，再整 struct 复制；锁外只对私有副本计算 age，避免
     * 在共享锁内做 O(cell_count) 的派生工作。读者得到的数值、质量元数据、
     * sample_sequence 与 afe_generation 因而来自同一次原子观察。
     */
    if ((snapshot == NULL) || (xDataMutex == NULL) ||
        (xSemaphoreTake(xDataMutex, (TickType_t)0U) != pdTRUE))
    {
        return false;
    }
    BMS_Data_LatchStale(now_ms);
    *snapshot = g_bms_data;
    (void)xSemaphoreGive(xDataMutex);

    for (cell_index = 0U;
         cell_index < (uint32_t)BMS_CELL_COUNT;
         ++cell_index)
    {
        uint16_t cell_mask;
        bool cell_valid;

        cell_mask = (uint16_t)((uint16_t)1U << cell_index);
        cell_valid =
            (snapshot->cell_metadata.valid_bitmap & cell_mask) !=
                (uint16_t)0U;
        snapshot->cell_metadata.age_ms[cell_index] =
            BMS_Data_DeriveAge(
                cell_valid,
                snapshot->cell_metadata.timestamp_ms[cell_index],
                now_ms);
    }
    BMS_Data_DeriveMetadataAge(&snapshot->pack_metadata, now_ms);
    BMS_Data_DeriveMetadataAge(&snapshot->bq_pack_metadata, now_ms);
    BMS_Data_DeriveMetadataAge(&snapshot->current_metadata, now_ms);
    BMS_Data_DeriveMetadataAge(&snapshot->ts1_metadata, now_ms);
    BMS_Data_DeriveMetadataAge(&snapshot->temperature_metadata, now_ms);
    BMS_Data_DeriveMetadataAge(&snapshot->soc_metadata, now_ms);
    return true;
}

bool BMS_Data_GetFreshnessSnapshot(
    BMS_DataFreshnessSnapshot_t *snapshot,
    BMS_TimestampMs_t now_ms)
{
    if ((snapshot == NULL) || (xDataMutex == NULL) ||
        (xSemaphoreTake(xDataMutex, (TickType_t)0U) != pdTRUE))
    {
        return false;
    }

    BMS_Data_LatchStale(now_ms);
    snapshot->pack_metadata = g_bms_data.pack_metadata;
    snapshot->current_metadata = g_bms_data.current_metadata;
    snapshot->temperature_metadata = g_bms_data.temperature_metadata;
    snapshot->sample_sequence = g_bms_data.sample_sequence;
    snapshot->afe_generation = g_bms_data.afe_generation;
    (void)xSemaphoreGive(xDataMutex);

    BMS_Data_DeriveMetadataAge(&snapshot->pack_metadata, now_ms);
    BMS_Data_DeriveMetadataAge(&snapshot->current_metadata, now_ms);
    BMS_Data_DeriveMetadataAge(&snapshot->temperature_metadata, now_ms);
    return true;
}

bool BMS_Data_GetIdentity(BMS_DataIdentity_t *identity)
{
    if ((identity == NULL) || (xDataMutex == NULL) ||
        (xSemaphoreTake(xDataMutex, (TickType_t)0U) != pdTRUE))
    {
        return false;
    }
    identity->sample_sequence = g_bms_data.sample_sequence;
    identity->afe_generation = g_bms_data.afe_generation;
    (void)xSemaphoreGive(xDataMutex);
    return true;
}

bool BMS_Data_PublishStateDiagnostic(BMS_State_t state,
                                     const BMS_FaultSummary_t *faults)
{
    if ((faults == NULL) || ((uint32_t)state >= (uint32_t)BMS_STATE_COUNT) ||
        (xDataMutex == NULL) ||
        (xSemaphoreTake(xDataMutex, (TickType_t)0U) != pdTRUE))
    {
        return false;
    }
    g_bms_data.state = state;
    g_bms_data.faults = *faults;
    (void)xSemaphoreGive(xDataMutex);
    return true;
}

bool BMS_Data_PublishSocDiagnostic(BMS_CapacityMah_t capacity_mah,
                                   BMS_SocPermille_t soc_permille,
                                   BMS_TimestampMs_t now_ms,
                                   bool valid)
{
    if ((valid && (soc_permille > BMS_SOC_PERMILLE_MAX)) ||
        (xDataMutex == NULL) ||
        (xSemaphoreTake(xDataMutex, (TickType_t)0U) != pdTRUE))
    {
        return false;
    }
    g_bms_data.remaining_capacity_mah = capacity_mah;
    g_bms_data.soc_permille = valid ? soc_permille :
        BMS_SOC_UNKNOWN_PERMILLE;
    BMS_Data_PublishMetadata(&g_bms_data.soc_metadata,
                             now_ms,
                             valid,
                             valid);
    (void)xSemaphoreGive(xDataMutex);
    return true;
}

bool BMS_Data_IsFresh(bool valid,
                      bool stale_latched,
                      uint32_t age_ms,
                      uint32_t max_age_ms)
{
    return valid && !stale_latched &&
           (age_ms != BMS_DATA_AGE_UNKNOWN_MS) &&
           (max_age_ms != BMS_DATA_AGE_UNKNOWN_MS) &&
           (age_ms <= max_age_ms);
}
