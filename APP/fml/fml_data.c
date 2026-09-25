#include "fml_data.h"
#include "os_runtime.h"

#include <limits.h>

/* backing store 只在本 translation unit 可写；跨模块读写必须经过一致性 API。 */
static BMS_DataSnapshot_t s_data;

#define BMS_DATA_STORE                         (s_data)

#if defined(TEST_PHASE8_DATA_IMAGE) || defined(BMS_PHASE8_HOST_TEST)
/* 仅测试镜像暴露可写快照，以验证发布边界和失效路径。 */
BMS_DataSnapshot_t *FML_Data_TestMutableStorage(void)
{
    return &s_data;
}
#endif

/*
 * 本模块把多个 owner 的诊断投影汇成一致快照，但不承担安全仲裁。
 * measurement 发布是唯一会推进 sample_sequence 的路径；afe_generation
 * 把序号限定在当前 AFE 生命周期，使 reset 前的数据不能被 reset 后继续采用。
 */

#define BMS_DATA_TS1_RAW14_MAX              ((uint16_t)0x3FFFU)

/* 把每组测量质量、时效和原始值设置为不可用初值。 */
static void FML_Data_InitMeasurement(BMS_MeasurementMetadata_t *metadata)
{
    metadata->timestamp_ms = (BMS_TimestampMs_t)0U;
    metadata->age_ms = BMS_DATA_AGE_UNKNOWN_MS;
    metadata->valid = false;
    metadata->in_range = false;
    metadata->stale_latched = false;
}

/* 建立所有测量组无效、状态为 INIT 的诊断快照初值。 */
void FML_Data_Init(void)
{
    /* 当前逻辑电芯的索引。 */
    uint32_t cell_index;

    for (cell_index = 0U;
         cell_index < (uint32_t)BMS_CELL_COUNT;
         cell_index++)
    {
        BMS_DATA_STORE.cell_voltage_mv[cell_index] =
            (BMS_CellVoltageMv_t)0U;
        BMS_DATA_STORE.cell_metadata.timestamp_ms[cell_index] =
            (BMS_TimestampMs_t)0U;
        BMS_DATA_STORE.cell_metadata.age_ms[cell_index] =
            BMS_DATA_AGE_UNKNOWN_MS;
    }

    BMS_DATA_STORE.cell_metadata.valid_bitmap = (uint16_t)0U;
    BMS_DATA_STORE.cell_metadata.in_range_bitmap = (uint16_t)0U;
    BMS_DATA_STORE.cell_metadata.stale_bitmap = (uint16_t)0U;

    BMS_DATA_STORE.pack_voltage_mv = (BMS_PackVoltageMv_t)0U;
    BMS_DATA_STORE.bq_pack_voltage_mv = (BMS_PackVoltageMv_t)0U;
    BMS_DATA_STORE.current_ma = (BMS_CurrentMa_t)0;
    BMS_DATA_STORE.ts1_raw14 = (uint16_t)0U;
    BMS_DATA_STORE.ts1_resistance_ohm = (uint32_t)0U;
    BMS_DATA_STORE.temperature_decic = (BMS_TemperatureDeciC_t)0;
    BMS_DATA_STORE.remaining_capacity_mah = (BMS_CapacityMah_t)0U;
    BMS_DATA_STORE.soc_permille = BMS_SOC_UNKNOWN_PERMILLE;

    BMS_DATA_STORE.state = BMS_STATE_INIT;
    FML_Fault_Init(&BMS_DATA_STORE.faults);

    FML_Data_InitMeasurement(&BMS_DATA_STORE.pack_metadata);
    FML_Data_InitMeasurement(&BMS_DATA_STORE.bq_pack_metadata);
    FML_Data_InitMeasurement(&BMS_DATA_STORE.current_metadata);
    FML_Data_InitMeasurement(&BMS_DATA_STORE.ts1_metadata);
    FML_Data_InitMeasurement(&BMS_DATA_STORE.temperature_metadata);
    FML_Data_InitMeasurement(&BMS_DATA_STORE.soc_metadata);

    BMS_DATA_STORE.snapshot_timestamp_ms = (BMS_TimestampMs_t)0U;
    BMS_DATA_STORE.sample_sequence = (uint32_t)0U;
    BMS_DATA_STORE.afe_generation = (uint32_t)0U;
}

/* 校验 staging 帧各有效位、范围位和可选温度字段的内部关系。 */
static bool FML_Data_FrameIsValid(const BMS_MeasurementFrame_t *frame)
{
    /* 有效位图中超出已定义电芯范围的位。 */
    uint16_t undefined_valid_bits;
    /* 超出已定义电芯范围的有效位。 */
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

/* 计算发布时刻的测量年龄，保留回绕安全的时间语义。 */
static BMS_DataAgeMs_t FML_Data_AgeAtPublication(bool valid)
{
    return valid ? (BMS_DataAgeMs_t)0U : BMS_DATA_AGE_UNKNOWN_MS;
}

/* 仅在帧身份仍一致时发布一组测量元数据。 */
static void FML_Data_PublishMetadata(BMS_MeasurementMetadata_t *metadata,
                                     BMS_TimestampMs_t timestamp_ms,
                                     bool valid,
                                     bool in_range)
{
    metadata->timestamp_ms = timestamp_ms;
    metadata->age_ms = FML_Data_AgeAtPublication(valid);
    metadata->valid = valid;
    metadata->in_range = valid && in_range;
    metadata->stale_latched = false;
}

/* 原子发布完整测量帧；输入无效或数据锁忙时保持旧快照不变。 */
bool FML_Data_PublishMeasurement(const BMS_MeasurementFrame_t *frame)
{
    /* 所有有效电芯电压之和，单位毫伏。 */
    BMS_PackVoltageMv_t pack_sum_mv;
    /* 当前逻辑电芯的索引。 */
    uint32_t cell_index;
    /* 电池包电压数据是否可用于本轮决策。 */
    bool pack_valid;
    /* 电池包电压是否落在有效测量范围内。 */
    bool pack_in_range;

    /*
     * 先在 mutex 外完成校验与 cell sum，缩短共享数据锁的持有时间；只有 staging
     * 完整合法且能立即取得 runtime data port，才一次替换 measurement-owned 字段。
     * 任一失败都保留上一帧，绝不把“新电芯+旧包压”之类半帧暴露给读者。
     */
    if (!FML_Data_FrameIsValid(frame))
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

    if (!OS_DataLock())
    {
        return false;
    }

    for (cell_index = 0U;
         cell_index < (uint32_t)BMS_CELL_COUNT;
         ++cell_index)
    {
        /* 本轮存在有效电压的逻辑电芯位图。 */
        uint16_t cell_mask;
        /* 目标逻辑电芯是否具有可用的电压数据。 */
        bool cell_valid;

        cell_mask = (uint16_t)((uint16_t)1U << cell_index);
        cell_valid =
            (frame->cell_valid_bitmap & cell_mask) != (uint16_t)0U;
        BMS_DATA_STORE.cell_voltage_mv[cell_index] =
            frame->cell_voltage_mv[cell_index];
        BMS_DATA_STORE.cell_metadata.timestamp_ms[cell_index] =
            frame->timestamp_ms;
        BMS_DATA_STORE.cell_metadata.age_ms[cell_index] =
            FML_Data_AgeAtPublication(cell_valid);
    }
    BMS_DATA_STORE.cell_metadata.valid_bitmap = frame->cell_valid_bitmap;
    BMS_DATA_STORE.cell_metadata.in_range_bitmap =
        frame->cell_in_range_bitmap;
    BMS_DATA_STORE.cell_metadata.stale_bitmap = (uint16_t)0U;

    pack_valid =
        frame->cell_valid_bitmap == BMS_CELL_DEFINED_MASK;
    pack_in_range = pack_valid &&
        (frame->cell_in_range_bitmap == BMS_CELL_DEFINED_MASK);
    BMS_DATA_STORE.pack_voltage_mv = pack_sum_mv;
    FML_Data_PublishMetadata(&BMS_DATA_STORE.pack_metadata,
                             frame->timestamp_ms,
                             pack_valid,
                             pack_in_range);

    BMS_DATA_STORE.bq_pack_voltage_mv = frame->bq_pack_voltage_mv;
    FML_Data_PublishMetadata(&BMS_DATA_STORE.bq_pack_metadata,
                             frame->timestamp_ms,
                             frame->bq_pack_valid,
                             frame->bq_pack_in_range);

    if (frame->update_current)
    {
        BMS_DATA_STORE.current_ma = frame->current_ma;
        FML_Data_PublishMetadata(&BMS_DATA_STORE.current_metadata,
                                 frame->current_timestamp_ms,
                                 frame->current_valid,
                                 frame->current_in_range);
    }

    if (frame->update_temperature)
    {
        BMS_DATA_STORE.ts1_raw14 = frame->ts1_raw14;
        BMS_DATA_STORE.ts1_resistance_ohm = frame->ts1_resistance_ohm;
        FML_Data_PublishMetadata(&BMS_DATA_STORE.ts1_metadata,
                                 frame->temperature_timestamp_ms,
                                 frame->ts1_valid,
                                 frame->ts1_valid);
        BMS_DATA_STORE.temperature_decic = frame->temperature_decic;
        FML_Data_PublishMetadata(&BMS_DATA_STORE.temperature_metadata,
                                 frame->temperature_timestamp_ms,
                                 frame->temperature_valid,
                                 frame->temperature_in_range);
    }

    /* sequence 最后推进，表示前面的 mandatory core 已全部写入同一 generation。 */
    BMS_DATA_STORE.snapshot_timestamp_ms = frame->timestamp_ms;
    BMS_DATA_STORE.afe_generation = frame->afe_generation;
    ++BMS_DATA_STORE.sample_sequence;

    OS_DataUnlock();
    return true;
}

/* 依据当前时刻和发布时间推导读取时年龄。 */
static BMS_DataAgeMs_t FML_Data_DeriveAge(bool valid,
                                         BMS_TimestampMs_t timestamp_ms,
                                         BMS_TimestampMs_t now_ms)
{
    if (!valid)
    {
        return BMS_DATA_AGE_UNKNOWN_MS;
    }
    return (BMS_DataAgeMs_t)(now_ms - timestamp_ms);
}

/* 为一个测量组更新读取时年龄及相关质量标志。 */
static void FML_Data_DeriveMetadataAge(
    BMS_MeasurementMetadata_t *metadata,
    BMS_TimestampMs_t now_ms)
{
    metadata->age_ms = FML_Data_DeriveAge(metadata->valid,
                                          metadata->timestamp_ms,
                                          now_ms);
}

/* 当测量组超过时效门限时锁存 stale，不靠时钟回绕自动清除。 */
static void FML_Data_LatchMetadataStale(
    BMS_MeasurementMetadata_t *metadata,
    BMS_TimestampMs_t now_ms,
    BMS_DataAgeMs_t max_age_ms)
{
    if (metadata->valid && !metadata->stale_latched &&
        (FML_Data_DeriveAge(true, metadata->timestamp_ms, now_ms) >
         max_age_ms))
    {
        metadata->stale_latched = true;
    }
}

/*
 * 只能在持有 runtime data port 时调用。周期读者把第一次 freshness 门限跨越变成
 * sticky 状态，阻止后续 uint32_t 时间戳回绕让旧数据重新显得新鲜。
 * 若读者停顿整整一个时间戳周期，32-bit clock 本身无法区分，由 watchdog 覆盖。
 */
static void FML_Data_LatchStale(BMS_TimestampMs_t now_ms)
{
    /* 当前逻辑电芯的索引。 */
    uint32_t cell_index;

    for (cell_index = 0U;
         cell_index < (uint32_t)BMS_CELL_COUNT;
         ++cell_index)
    {
        /* 本轮存在有效电压的逻辑电芯位图。 */
        uint16_t cell_mask;
        /* 电芯电压快照距今的时间，单位毫秒。 */
        BMS_DataAgeMs_t cell_age_ms;

        cell_mask = (uint16_t)((uint16_t)1U << cell_index);
        if (((BMS_DATA_STORE.cell_metadata.valid_bitmap & cell_mask) !=
             (uint16_t)0U) &&
            ((BMS_DATA_STORE.cell_metadata.stale_bitmap & cell_mask) ==
             (uint16_t)0U))
        {
            cell_age_ms = FML_Data_DeriveAge(
                true,
                BMS_DATA_STORE.cell_metadata.timestamp_ms[cell_index],
                now_ms);
            if (cell_age_ms > BMS_DATA_VOLTAGE_FRESH_MAX_MS)
            {
                BMS_DATA_STORE.cell_metadata.stale_bitmap |= cell_mask;
            }
        }
    }

    FML_Data_LatchMetadataStale(&BMS_DATA_STORE.pack_metadata, now_ms,
                                BMS_DATA_VOLTAGE_FRESH_MAX_MS);
    FML_Data_LatchMetadataStale(&BMS_DATA_STORE.bq_pack_metadata, now_ms,
                                BMS_DATA_VOLTAGE_FRESH_MAX_MS);
    FML_Data_LatchMetadataStale(&BMS_DATA_STORE.current_metadata, now_ms,
                                BMS_DATA_CURRENT_FRESH_MAX_MS);
    FML_Data_LatchMetadataStale(&BMS_DATA_STORE.ts1_metadata, now_ms,
                                BMS_DATA_TEMPERATURE_FRESH_MAX_MS);
    FML_Data_LatchMetadataStale(&BMS_DATA_STORE.temperature_metadata, now_ms,
                                BMS_DATA_TEMPERATURE_FRESH_MAX_MS);
}

/* 在数据锁内复制同代完整快照，并在锁外计算读取时年龄。 */
bool FML_Data_GetSnapshot(BMS_DataSnapshot_t *snapshot,
                          BMS_TimestampMs_t now_ms)
{
    /* 当前逻辑电芯的索引。 */
    uint32_t cell_index;

    /*
     * 锁内先更新 sticky stale，再整 struct 复制；锁外只对私有副本计算 age，避免
     * 在共享锁内做 O(cell_count) 的派生工作。读者得到的数值、质量元数据、
     * sample_sequence 与 afe_generation 因而来自同一次原子观察。
     */
    if ((snapshot == NULL) || !OS_DataLock())
    {
        return false;
    }
    FML_Data_LatchStale(now_ms);
    *snapshot = BMS_DATA_STORE;
    OS_DataUnlock();

    for (cell_index = 0U;
         cell_index < (uint32_t)BMS_CELL_COUNT;
         ++cell_index)
    {
        /* 本轮存在有效电压的逻辑电芯位图。 */
        uint16_t cell_mask;
        /* 目标逻辑电芯是否具有可用的电压数据。 */
        bool cell_valid;

        cell_mask = (uint16_t)((uint16_t)1U << cell_index);
        cell_valid =
            (snapshot->cell_metadata.valid_bitmap & cell_mask) !=
                (uint16_t)0U;
        snapshot->cell_metadata.age_ms[cell_index] =
            FML_Data_DeriveAge(
                cell_valid,
                snapshot->cell_metadata.timestamp_ms[cell_index],
                now_ms);
    }
    FML_Data_DeriveMetadataAge(&snapshot->pack_metadata, now_ms);
    FML_Data_DeriveMetadataAge(&snapshot->bq_pack_metadata, now_ms);
    FML_Data_DeriveMetadataAge(&snapshot->current_metadata, now_ms);
    FML_Data_DeriveMetadataAge(&snapshot->ts1_metadata, now_ms);
    FML_Data_DeriveMetadataAge(&snapshot->temperature_metadata, now_ms);
    FML_Data_DeriveMetadataAge(&snapshot->soc_metadata, now_ms);
    return true;
}

/* 按当前时刻推导各测量组年龄并复制只读新鲜度快照。 */
bool FML_Data_GetFreshnessSnapshot(
    BMS_DataFreshnessSnapshot_t *snapshot,
    BMS_TimestampMs_t now_ms)
{
    if ((snapshot == NULL) || !OS_DataLock())
    {
        return false;
    }

    FML_Data_LatchStale(now_ms);
    snapshot->pack_metadata = BMS_DATA_STORE.pack_metadata;
    snapshot->current_metadata = BMS_DATA_STORE.current_metadata;
    snapshot->temperature_metadata = BMS_DATA_STORE.temperature_metadata;
    snapshot->sample_sequence = BMS_DATA_STORE.sample_sequence;
    snapshot->afe_generation = BMS_DATA_STORE.afe_generation;
    OS_DataUnlock();

    FML_Data_DeriveMetadataAge(&snapshot->pack_metadata, now_ms);
    FML_Data_DeriveMetadataAge(&snapshot->current_metadata, now_ms);
    FML_Data_DeriveMetadataAge(&snapshot->temperature_metadata, now_ms);
    return true;
}

/* 只复制当前 sample sequence 与 AFE generation，供提交前轻量复核。 */
bool FML_Data_GetIdentity(BMS_DataIdentity_t *identity)
{
    if ((identity == NULL) || !OS_DataLock())
    {
        return false;
    }
    identity->sample_sequence = BMS_DATA_STORE.sample_sequence;
    identity->afe_generation = BMS_DATA_STORE.afe_generation;
    OS_DataUnlock();
    return true;
}

/* 把 State owner 的运行状态与故障摘要写入只读诊断投影。 */
bool FML_Data_PublishStateDiagnostic(BMS_State_t state,
                                     const BMS_FaultSummary_t *faults)
{
    if ((faults == NULL) || ((uint32_t)state >= (uint32_t)BMS_STATE_COUNT) ||
        !OS_DataLock())
    {
        return false;
    }
    BMS_DATA_STORE.state = state;
    BMS_DATA_STORE.faults = *faults;
    OS_DataUnlock();
    return true;
}

/* 把 SOC owner 的容量和千分比写入只读诊断投影。 */
bool FML_Data_PublishSocDiagnostic(BMS_CapacityMah_t capacity_mah,
                                   BMS_SocPermille_t soc_permille,
                                   BMS_TimestampMs_t now_ms,
                                   bool valid)
{
    if ((valid && (soc_permille > BMS_SOC_PERMILLE_MAX)) ||
        !OS_DataLock())
    {
        return false;
    }
    BMS_DATA_STORE.remaining_capacity_mah = capacity_mah;
    BMS_DATA_STORE.soc_permille = valid ? soc_permille :
        BMS_SOC_UNKNOWN_PERMILLE;
    FML_Data_PublishMetadata(&BMS_DATA_STORE.soc_metadata,
                             now_ms,
                             valid,
                             valid);
    OS_DataUnlock();
    return true;
}

/* 同时检查有效位、过期锁存和年龄门限。 */
bool FML_Data_IsFresh(bool valid,
                      bool stale_latched,
                      uint32_t age_ms,
                      uint32_t max_age_ms)
{
    return valid && !stale_latched &&
           (age_ms != BMS_DATA_AGE_UNKNOWN_MS) &&
           (max_age_ms != BMS_DATA_AGE_UNKNOWN_MS) &&
           (age_ms <= max_age_ms);
}
