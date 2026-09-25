#include "fml_soc.h"
#include "os_runtime.h"

/*
 * SOC 采用整数库仑计数：ProtectTask 每次 CC_READY 只产生一条带 generation 的
 * sample，SOCTask 是 queue 与 estimate sole owner。Flash restore 只提供启动值；
 * 后续积分、OCV full/empty correction 与 capacity clamp 都在同一 engine 内完成。
 */

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "fml_protect.h"
#include "bsp_bq76940_measurement.h"

#define BMS_SOC_MAMS_PER_MAH                    (3600000LL)
#define BMS_SOC_OCV_POINT_COUNT                 (10U)
typedef struct
{
    uint16_t cell_mv; /* OCV 插值节点的单体电压，单位 mV。 */
    uint16_t soc_permille; /* 对应的初始 SOC，单位千分比。 */
} BMS_SocOcvPoint_t;

/* 启动 OCV 估算使用的固定单体电压与 SOC 对照表。 */
static const BMS_SocOcvPoint_t
    s_ocv_table[BMS_SOC_OCV_POINT_COUNT] =
{
    {3000U, 0U}, {3300U, 100U}, {3500U, 200U},
    {3600U, 300U}, {3700U, 500U}, {3800U, 650U},
    {3900U, 800U}, {4000U, 900U}, {4100U, 970U},
    {4200U, 1000U}
};

/* SOC owner 独占的高分辨率积分与端点资格状态。 */
static BMS_SocEngine_t s_engine;
/* 对其它模块发布的 SOC 只读估计快照。 */
static BMS_SocSnapshot_t s_snapshot;
/* 启动期绑定的不可变 SOC 策略。 */
static const BMS_Policy_t *s_policy;

/* 根据容量策略计算内部 mA·ms 积分上限。 */
static int64_t FML_Soc_MaxMams(const BMS_SocPolicy_t *policy)
{
    return (int64_t)policy->capacity_mah * BMS_SOC_MAMS_PER_MAH;
}

/* 在相邻 OCV 节点间插值初始 SOC，范围外取端点值。 */
static uint16_t FML_Soc_InterpolateOcv(uint16_t cell_mv)
{
    /* 当前 OCV 节点或 CC 样本的索引。 */
    uint8_t index;
    /* 当前比例计算的分子。 */
    uint32_t numerator;
    /* 当前比例计算的分母。 */
    uint32_t denominator;
    /* 本轮 SOC 修正量。 */
    uint32_t delta_soc;

    if (cell_mv <= s_ocv_table[0].cell_mv)
    {
        return s_ocv_table[0].soc_permille;
    }
    for (index = 1U; index < BMS_SOC_OCV_POINT_COUNT; ++index)
    {
        if (cell_mv <= s_ocv_table[index].cell_mv)
        {
            numerator = (uint32_t)(cell_mv -
                s_ocv_table[index - 1U].cell_mv) *
                (uint32_t)(s_ocv_table[index].soc_permille -
                s_ocv_table[index - 1U].soc_permille);
            denominator = (uint32_t)(s_ocv_table[index].cell_mv -
                s_ocv_table[index - 1U].cell_mv);
            delta_soc = numerator / denominator;
            return (uint16_t)(s_ocv_table[index - 1U].soc_permille +
                delta_soc);
        }
    }
    return s_ocv_table[BMS_SOC_OCV_POINT_COUNT - 1U].soc_permille;
}

/* 确认 OCV 与端点修正所需的电芯测量仍新鲜。 */
static bool FML_Soc_HaveFreshCells(
    const BMS_DataSnapshot_t *measurement)
{
    return (measurement != NULL) &&
        (measurement->cell_metadata.valid_bitmap == BMS_CELL_DEFINED_MASK) &&
        (measurement->cell_metadata.stale_bitmap == 0U);
}

/* 从有效电芯测量计算平均单体电压，供 OCV 初始化。 */
static uint16_t FML_Soc_AverageCellMv(
    const BMS_DataSnapshot_t *measurement)
{
    /* 当前累加结果。 */
    uint32_t sum;
    /* 当前 OCV 节点或 CC 样本的索引。 */
    uint8_t index;

    sum = 0UL;
    for (index = 0U; index < BMS_CELL_COUNT; ++index)
    {
        sum += measurement->cell_voltage_mv[index];
    }
    return (uint16_t)(sum / BMS_CELL_COUNT);
}

/* 从有效电芯测量取得最低单体电压，供端点修正。 */
static uint16_t FML_Soc_MinCellMv(
    const BMS_DataSnapshot_t *measurement)
{
    /* 当前比较得到的最小值。 */
    uint16_t minimum;
    /* 当前 OCV 节点或 CC 样本的索引。 */
    uint8_t index;

    minimum = measurement->cell_voltage_mv[0];
    for (index = 1U; index < BMS_CELL_COUNT; ++index)
    {
        if (measurement->cell_voltage_mv[index] < minimum)
        {
            minimum = measurement->cell_voltage_mv[index];
        }
    }
    return minimum;
}

/* 按回绕安全的毫秒差判断 SOC 时间窗口结束。 */
static bool FML_Soc_TimeElapsed(uint32_t now_ms,
                                uint32_t started_ms,
                                uint32_t duration_ms)
{
    return ((uint32_t)(now_ms - started_ms) >= duration_ms);
}

/* 把容量积分结果限制到千分比范围后发布。 */
static void FML_Soc_SetPermille(BMS_SocEngine_t *engine,
                                const BMS_SocPolicy_t *policy,
                                uint16_t soc_permille)
{
    engine->remaining_mams =
        (FML_Soc_MaxMams(policy) * (int64_t)soc_permille) / 1000LL;
}

/* 用有效 Flash/OCV 证据建立 SOC 积分器初始容量。 */
bool FML_Soc_EngineInit(BMS_SocEngine_t *engine,
                        const BMS_SocPolicy_t *policy,
                        const BMS_DataSnapshot_t *measurement,
                        uint32_t now_ms)
{
    /* 重启后恢复或安全初始化的 SOC 值。 */
    uint16_t initial_soc;

    if ((engine == NULL) || (policy == NULL) ||
        (policy->capacity_mah == 0UL) ||
        (policy->initial_soc_permille > BMS_SOC_PERMILLE_MAX))
    {
        return false;
    }
    (void)memset(engine, 0, sizeof(*engine));
    initial_soc = policy->initial_soc_permille;
    if (FML_Soc_HaveFreshCells(measurement))
    {
        initial_soc = FML_Soc_InterpolateOcv(
            FML_Soc_AverageCellMv(measurement));
        engine->afe_generation = measurement->afe_generation;
    }
    FML_Soc_SetPermille(engine, policy, initial_soc);
    engine->last_sample_ms = now_ms;
    engine->initialized = true;
    engine->valid = true;
    return true;
}

/* 标记 CC 样本序列断档，避免跨缺口积分。 */
void FML_Soc_MarkQueueGap(BMS_SocEngine_t *engine)
{
    if (engine != NULL)
    {
        if (engine->queue_gap_count < UINT32_MAX)
        {
            ++engine->queue_gap_count;
        }
        engine->queue_gap_latched = true;
        engine->valid = false;
        engine->have_sample_time = false;
    }
}

/* 按样本时间差积分电流，并维护同代 CC 连续性。 */
bool FML_Soc_IntegrateCurrent(BMS_SocEngine_t *engine,
                              const BMS_SocPolicy_t *policy,
                              int32_t current_ma,
                              uint32_t sample_ms,
                              uint32_t afe_generation)
{
    /* 本轮 SOC 积分距上次更新的毫秒数。 */
    uint32_t elapsed_ms;
    /* 用于本轮 SOC 积分的充放电效率系数。 */
    uint16_t efficiency;
    /* 本轮库仑积分增量，单位 mA·ms。 */
    int64_t delta_mams;
    /* 当前比较得到的最大值。 */
    int64_t maximum;

    /*
     * 整数形式的库仑积分：delta_mams = current_ma × elapsed_ms × efficiency/1000。
     * 正电流按充电效率增加容量，负电流按放电效率减少容量；结果始终 clamp 在
     * [0, nominal_capacity]，避免数值误差把 SOC 推到物理域之外。
     */
    if ((engine == NULL) || (policy == NULL) || !engine->initialized)
    {
        return false;
    }
    /* generation 改变时只建立新时间基线，禁止用旧电流跨越未知 reset 间隔积分。 */
    if (engine->afe_generation != afe_generation)
    {
        engine->afe_generation = afe_generation;
        engine->have_sample_time = false;
        engine->valid = false;
        if (engine->generation_change_count < UINT32_MAX)
        {
            ++engine->generation_change_count;
        }
    }
    if (!engine->have_sample_time)
    {
        engine->last_sample_ms = sample_ms;
        engine->have_sample_time = true;
        return true;
    }

    elapsed_ms = (uint32_t)(sample_ms - engine->last_sample_ms);
    engine->last_sample_ms = sample_ms;
    if (elapsed_ms > (policy->period_ms * 4UL))
    {
        FML_Soc_MarkQueueGap(engine);
        engine->last_sample_ms = sample_ms;
        engine->have_sample_time = true;
        return false;
    }
    efficiency = current_ma >= 0 ? policy->charge_efficiency_permille :
        policy->discharge_efficiency_permille;
    delta_mams = ((int64_t)current_ma * (int64_t)elapsed_ms *
        (int64_t)efficiency) / 1000LL;
    maximum = FML_Soc_MaxMams(policy);
    if ((delta_mams > 0LL) &&
        (engine->remaining_mams > (maximum - delta_mams)))
    {
        engine->remaining_mams = maximum;
    }
    else if ((delta_mams < 0LL) &&
             (engine->remaining_mams < -delta_mams))
    {
        engine->remaining_mams = 0LL;
    }
    else
    {
        engine->remaining_mams += delta_mams;
    }
    if (engine->integrated_sample_count < UINT32_MAX)
    {
        ++engine->integrated_sample_count;
    }
    return true;
}

/* 按静置或端点资格修正积分估计，并保留证据窗口。 */
void FML_Soc_ObserveCorrection(BMS_SocEngine_t *engine,
                               const BMS_SocPolicy_t *policy,
                               const BMS_DataSnapshot_t *measurement,
                               uint32_t now_ms)
{
    /* 当前比较得到的最小值。 */
    uint16_t minimum;
    /* 本轮电流样本是否仍在有效期内。 */
    bool current_fresh;
    /* 电压和电流是否同时满足满电确认条件。 */
    bool full_condition;
    /* 当前是否满足空队列或空集合条件。 */
    bool empty_condition;

    if ((engine == NULL) || (policy == NULL) ||
        !engine->initialized || !FML_Soc_HaveFreshCells(measurement))
    {
        if (engine != NULL)
        {
            engine->full_tracking = false;
            engine->empty_tracking = false;
        }
        return;
    }
    current_fresh = FML_Data_IsFresh(
        measurement->current_metadata.valid,
        measurement->current_metadata.stale_latched,
        measurement->current_metadata.age_ms,
        BMS_DATA_CURRENT_FRESH_MAX_MS);
    if (!current_fresh ||
        (measurement->afe_generation != engine->afe_generation))
    {
        engine->full_tracking = false;
        engine->empty_tracking = false;
        return;
    }

    minimum = FML_Soc_MinCellMv(measurement);
    full_condition = (minimum >= policy->full_cell_mv) &&
        (measurement->current_ma >= policy->full_current_min_ma) &&
        (measurement->current_ma <= policy->full_current_max_ma);
    empty_condition = (minimum <= policy->empty_cell_mv) &&
        (measurement->current_ma <= 0) &&
        (measurement->current_ma >=
         -policy->empty_discharge_abs_current_max_ma);

    if (full_condition)
    {
        if (!engine->full_tracking)
        {
            engine->full_tracking = true;
            engine->full_started_ms = now_ms;
        }
        else if (FML_Soc_TimeElapsed(now_ms, engine->full_started_ms,
                                     policy->full_qualify_ms))
        {
            FML_Soc_SetPermille(engine, policy, 1000U);
            engine->valid = true;
            engine->queue_gap_latched = false;
            engine->full_tracking = false;
            if (engine->full_correction_count < UINT32_MAX)
            {
                ++engine->full_correction_count;
            }
        }
    }
    else
    {
        engine->full_tracking = false;
    }

    if (empty_condition)
    {
        if (!engine->empty_tracking)
        {
            engine->empty_tracking = true;
            engine->empty_started_ms = now_ms;
        }
        else if (FML_Soc_TimeElapsed(now_ms, engine->empty_started_ms,
                                     policy->empty_qualify_ms))
        {
            FML_Soc_SetPermille(engine, policy, 0U);
            engine->valid = true;
            engine->queue_gap_latched = false;
            engine->empty_tracking = false;
            if (engine->empty_correction_count < UINT32_MAX)
            {
                ++engine->empty_correction_count;
            }
        }
    }
    else
    {
        engine->empty_tracking = false;
    }
}

/* 复制积分器内部证据供测试与诊断，不修改估计。 */
BMS_SocSnapshot_t FML_Soc_GetEngineSnapshot(
    const BMS_SocEngine_t *engine,
    const BMS_SocPolicy_t *policy)
{
    /* 本次读取的一致状态快照。 */
    BMS_SocSnapshot_t snapshot;
    /* 当前比较得到的最大值。 */
    int64_t maximum;
    /* 本轮计算出的 SOC 千分比中间值。 */
    int64_t permille;

    (void)memset(&snapshot, 0, sizeof(snapshot));
    if ((engine == NULL) || (policy == NULL) || !engine->initialized)
    {
        snapshot.soc_permille = BMS_SOC_UNKNOWN_PERMILLE;
        return snapshot;
    }
    maximum = FML_Soc_MaxMams(policy);
    permille = maximum == 0LL ? 0LL :
        ((engine->remaining_mams * 1000LL) / maximum);
    if (permille > 1000LL)
    {
        permille = 1000LL;
    }
    snapshot.remaining_capacity_mah = (BMS_CapacityMah_t)(
        engine->remaining_mams / BMS_SOC_MAMS_PER_MAH);
    snapshot.soc_permille = (BMS_SocPermille_t)permille;
    snapshot.integrated_sample_count = engine->integrated_sample_count;
    snapshot.queue_gap_count = engine->queue_gap_count;
    snapshot.generation_change_count = engine->generation_change_count;
    snapshot.full_correction_count = engine->full_correction_count;
    snapshot.empty_correction_count = engine->empty_correction_count;
    snapshot.valid = engine->valid;
    snapshot.queue_gap_latched = engine->queue_gap_latched;
    return snapshot;
}

/* 绑定 SOC 策略并建立尚未取得有效容量证据的初始估计。 */
void FML_Soc_Init(const BMS_Policy_t *policy)
{
    /* 本轮计算使用的测量快照。 */
    BMS_DataSnapshot_t measurement;

    (void)memset(&s_engine, 0, sizeof(s_engine));
    (void)memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.soc_permille = BMS_SOC_UNKNOWN_PERMILLE;
    s_policy = FML_Policy_Validate(policy) ? policy : NULL;
    if ((s_policy != NULL) && FML_Data_GetSnapshot(&measurement, 0UL))
    {
        (void)FML_Soc_EngineInit(&s_engine, &s_policy->soc,
                                 &measurement, 0UL);
        s_snapshot = FML_Soc_GetEngineSnapshot(&s_engine, &s_policy->soc);
    }
}

/* 用合法持久化值恢复 SOC 估计，非法记录不覆盖现有状态。 */
bool FML_Soc_Restore(uint16_t soc_permille,
                     uint32_t remaining_capacity_mah)
{
    /* 由测量数据推导的 SOC 千分比。 */
    uint32_t derived_permille;

    if ((s_policy == NULL) || (soc_permille > 1000U) ||
        (remaining_capacity_mah > s_policy->soc.capacity_mah))
    {
        return false;
    }
    derived_permille = s_policy->soc.capacity_mah == 0UL ? 0UL :
        (remaining_capacity_mah * 1000UL) /
        s_policy->soc.capacity_mah;
    if ((derived_permille > (uint32_t)soc_permille + 1UL) ||
        ((uint32_t)soc_permille > derived_permille + 1UL))
    {
        return false;
    }
    (void)memset(&s_engine, 0, sizeof(s_engine));
    s_engine.remaining_mams =
        (int64_t)remaining_capacity_mah * BMS_SOC_MAMS_PER_MAH;
    s_engine.initialized = true;
    s_engine.valid = true;
    s_snapshot = FML_Soc_GetEngineSnapshot(&s_engine, &s_policy->soc);
    return true;
}

/* 消费本轮 CC 样本、更新 SOC 估计并发布只读诊断。 */
void FML_Soc_RunOnce(uint32_t now_ms,
                     const BMS_CcSample_t *samples,
                     uint8_t sample_count,
                     bool queue_gap)
{
    /* 本轮计算使用的测量快照。 */
    BMS_DataSnapshot_t measurement;
    /* 本轮处理的 CC 电流或 AFE 测量样本。 */
    const BMS_CcSample_t *sample;
    /* 当前 OCV 节点或 CC 样本的索引。 */
    uint8_t index;
    /* 用于本轮安全或 SOC 判定的电流，单位 mA。 */
    int32_t current_ma;

    if ((s_policy == NULL) ||
        !FML_Data_GetSnapshot(&measurement, now_ms))
    {
        return;
    }
    if (!s_engine.initialized)
    {
        (void)FML_Soc_EngineInit(&s_engine, &s_policy->soc,
                                 &measurement, now_ms);
    }
    if (queue_gap ||
        ((sample_count > 0U) && (samples == NULL)) ||
        (sample_count > BMS_SOC_MAX_CC_SAMPLES_PER_RUN))
    {
        FML_Soc_MarkQueueGap(&s_engine);
    }
    if (sample_count > BMS_SOC_MAX_CC_SAMPLES_PER_RUN)
    {
        sample_count = BMS_SOC_MAX_CC_SAMPLES_PER_RUN;
    }
    if (samples == NULL)
    {
        /* 非零 count 配 NULL 已在上方锁存 queue gap；清零防止无效输入被解引用。 */
        sample_count = 0U;
    }
    for (index = 0U; index < sample_count; ++index)
    {
        sample = &samples[index];
        if ((sample->xready_generation != measurement.afe_generation) ||
            (BSP_BQ76940_ConvertCcRawToCurrentMa(
                sample->raw, s_policy->rsense_uohm,
                s_policy->current_polarity, &current_ma) !=
             BQ76940_STATUS_OK))
        {
            FML_Soc_MarkQueueGap(&s_engine);
            continue;
        }
        (void)FML_Soc_IntegrateCurrent(
            &s_engine, &s_policy->soc, current_ma,
            sample->sample_ms,
            sample->xready_generation);
    }
    FML_Soc_ObserveCorrection(&s_engine, &s_policy->soc,
                              &measurement, now_ms);
    s_snapshot = FML_Soc_GetEngineSnapshot(&s_engine, &s_policy->soc);
    (void)FML_Data_PublishSocDiagnostic(
        s_snapshot.remaining_capacity_mah,
        s_snapshot.soc_permille, now_ms, s_snapshot.valid);
}

/* 复制 SOC owner 已发布的容量和证据状态。 */
BMS_SocSnapshot_t FML_Soc_GetSnapshot(void)
{
    /* 本次读取的一致状态快照。 */
    BMS_SocSnapshot_t snapshot;

    OS_CriticalEnter();
    snapshot = s_snapshot;
    OS_CriticalExit();
    return snapshot;
}
