#include "bms_soc.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "app_rtos.h"
#include "bms_protect.h"
#include "bq76940_measurement.h"

#define BMS_SOC_MAMS_PER_MAH                    (3600000LL)
#define BMS_SOC_OCV_POINT_COUNT                 (10U)
#define BMS_SOC_MAX_DRAIN_PER_RUN               \
    (APP_RTOS_CC_SAMPLE_QUEUE_DEPTH)

typedef struct
{
    uint16_t cell_mv;
    uint16_t soc_permille;
} BMS_SocOcvPoint_t;

static const BMS_SocOcvPoint_t
    s_ocv_table[BMS_SOC_OCV_POINT_COUNT] =
{
    {3000U, 0U}, {3300U, 100U}, {3500U, 200U},
    {3600U, 300U}, {3700U, 500U}, {3800U, 650U},
    {3900U, 800U}, {4000U, 900U}, {4100U, 970U},
    {4200U, 1000U}
};

static BMS_SocEngine_t s_engine;
static BMS_SocSnapshot_t s_snapshot;
static const BMS_Policy_t *s_policy;

static int64_t BMS_Soc_MaxMams(const BMS_SocPolicy_t *policy)
{
    return (int64_t)policy->capacity_mah * BMS_SOC_MAMS_PER_MAH;
}

static uint16_t BMS_Soc_InterpolateOcv(uint16_t cell_mv)
{
    uint8_t index;
    uint32_t numerator;
    uint32_t denominator;
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

static bool BMS_Soc_HaveFreshCells(
    const BMS_DataSnapshot_t *measurement)
{
    return (measurement != NULL) &&
        (measurement->cell_metadata.valid_bitmap == BMS_CELL_DEFINED_MASK) &&
        (measurement->cell_metadata.stale_bitmap == 0U);
}

static uint16_t BMS_Soc_AverageCellMv(
    const BMS_DataSnapshot_t *measurement)
{
    uint32_t sum;
    uint8_t index;

    sum = 0UL;
    for (index = 0U; index < BMS_CELL_COUNT; ++index)
    {
        sum += measurement->cell_voltage_mv[index];
    }
    return (uint16_t)(sum / BMS_CELL_COUNT);
}

static uint16_t BMS_Soc_MinCellMv(
    const BMS_DataSnapshot_t *measurement)
{
    uint16_t minimum;
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

static bool BMS_Soc_TimeElapsed(uint32_t now_ms,
                                uint32_t started_ms,
                                uint32_t duration_ms)
{
    return ((uint32_t)(now_ms - started_ms) >= duration_ms);
}

static void BMS_Soc_SetPermille(BMS_SocEngine_t *engine,
                                const BMS_SocPolicy_t *policy,
                                uint16_t soc_permille)
{
    engine->remaining_mams =
        (BMS_Soc_MaxMams(policy) * (int64_t)soc_permille) / 1000LL;
}

bool BMS_Soc_EngineInit(BMS_SocEngine_t *engine,
                        const BMS_SocPolicy_t *policy,
                        const BMS_DataSnapshot_t *measurement,
                        uint32_t now_ms)
{
    uint16_t initial_soc;

    if ((engine == NULL) || (policy == NULL) ||
        (policy->capacity_mah == 0UL) ||
        (policy->initial_soc_permille > BMS_SOC_PERMILLE_MAX))
    {
        return false;
    }
    (void)memset(engine, 0, sizeof(*engine));
    initial_soc = policy->initial_soc_permille;
    if (BMS_Soc_HaveFreshCells(measurement))
    {
        initial_soc = BMS_Soc_InterpolateOcv(
            BMS_Soc_AverageCellMv(measurement));
        engine->afe_generation = measurement->afe_generation;
    }
    BMS_Soc_SetPermille(engine, policy, initial_soc);
    engine->last_sample_ms = now_ms;
    engine->initialized = true;
    engine->valid = true;
    return true;
}

void BMS_Soc_MarkQueueGap(BMS_SocEngine_t *engine)
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

bool BMS_Soc_IntegrateCurrent(BMS_SocEngine_t *engine,
                              const BMS_SocPolicy_t *policy,
                              int32_t current_ma,
                              uint32_t sample_ms,
                              uint32_t afe_generation)
{
    uint32_t elapsed_ms;
    uint16_t efficiency;
    int64_t delta_mams;
    int64_t maximum;

    if ((engine == NULL) || (policy == NULL) || !engine->initialized)
    {
        return false;
    }
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
        BMS_Soc_MarkQueueGap(engine);
        engine->last_sample_ms = sample_ms;
        engine->have_sample_time = true;
        return false;
    }
    efficiency = current_ma >= 0 ? policy->charge_efficiency_permille :
        policy->discharge_efficiency_permille;
    delta_mams = ((int64_t)current_ma * (int64_t)elapsed_ms *
        (int64_t)efficiency) / 1000LL;
    maximum = BMS_Soc_MaxMams(policy);
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

void BMS_Soc_ObserveCorrection(BMS_SocEngine_t *engine,
                               const BMS_SocPolicy_t *policy,
                               const BMS_DataSnapshot_t *measurement,
                               uint32_t now_ms)
{
    uint16_t minimum;
    bool current_fresh;
    bool full_condition;
    bool empty_condition;

    if ((engine == NULL) || (policy == NULL) ||
        !engine->initialized || !BMS_Soc_HaveFreshCells(measurement))
    {
        if (engine != NULL)
        {
            engine->full_tracking = false;
            engine->empty_tracking = false;
        }
        return;
    }
    current_fresh = BMS_Data_IsFresh(
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

    minimum = BMS_Soc_MinCellMv(measurement);
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
        else if (BMS_Soc_TimeElapsed(now_ms, engine->full_started_ms,
                                     policy->full_qualify_ms))
        {
            BMS_Soc_SetPermille(engine, policy, 1000U);
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
        else if (BMS_Soc_TimeElapsed(now_ms, engine->empty_started_ms,
                                     policy->empty_qualify_ms))
        {
            BMS_Soc_SetPermille(engine, policy, 0U);
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

BMS_SocSnapshot_t BMS_Soc_GetEngineSnapshot(
    const BMS_SocEngine_t *engine,
    const BMS_SocPolicy_t *policy)
{
    BMS_SocSnapshot_t snapshot;
    int64_t maximum;
    int64_t permille;

    (void)memset(&snapshot, 0, sizeof(snapshot));
    if ((engine == NULL) || (policy == NULL) || !engine->initialized)
    {
        snapshot.soc_permille = BMS_SOC_UNKNOWN_PERMILLE;
        return snapshot;
    }
    maximum = BMS_Soc_MaxMams(policy);
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

void BMS_Soc_Init(const BMS_Policy_t *policy)
{
    BMS_DataSnapshot_t measurement;

    (void)memset(&s_engine, 0, sizeof(s_engine));
    (void)memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.soc_permille = BMS_SOC_UNKNOWN_PERMILLE;
    s_policy = BMS_Policy_Validate(policy) ? policy : NULL;
    if ((s_policy != NULL) && BMS_Data_GetSnapshot(&measurement, 0UL))
    {
        (void)BMS_Soc_EngineInit(&s_engine, &s_policy->soc,
                                 &measurement, 0UL);
        s_snapshot = BMS_Soc_GetEngineSnapshot(&s_engine, &s_policy->soc);
    }
}

bool BMS_Soc_Restore(uint16_t soc_permille,
                     uint32_t remaining_capacity_mah)
{
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
    s_snapshot = BMS_Soc_GetEngineSnapshot(&s_engine, &s_policy->soc);
    return true;
}

void BMS_Soc_RunOnce(uint32_t now_ms)
{
    BMS_DataSnapshot_t measurement;
    BMS_CcSample_t sample;
    EventBits_t events;
    uint8_t drained;
    int32_t current_ma;

    if ((s_policy == NULL) ||
        !BMS_Data_GetSnapshot(&measurement, now_ms))
    {
        return;
    }
    if (!s_engine.initialized)
    {
        (void)BMS_Soc_EngineInit(&s_engine, &s_policy->soc,
                                 &measurement, now_ms);
    }
    if (xSysEvents != NULL)
    {
        events = xEventGroupGetBits(xSysEvents);
        if ((events & EVT_CC_QUEUE_OVERFLOW) != 0U)
        {
            BMS_Soc_MarkQueueGap(&s_engine);
            (void)xEventGroupClearBits(xSysEvents,
                                       EVT_CC_QUEUE_OVERFLOW);
        }
    }
    drained = 0U;
    while ((xCcSampleQueue != NULL) &&
           (drained < BMS_SOC_MAX_DRAIN_PER_RUN) &&
           (xQueueReceive(xCcSampleQueue, &sample, 0U) == pdPASS))
    {
        ++drained;
        if ((sample.xready_generation != measurement.afe_generation) ||
            (BQ76940_ConvertCcRawToCurrentMa(
                sample.raw, s_policy->rsense_uohm,
                s_policy->current_polarity, &current_ma) !=
             BQ76940_STATUS_OK))
        {
            BMS_Soc_MarkQueueGap(&s_engine);
            continue;
        }
        (void)BMS_Soc_IntegrateCurrent(
            &s_engine, &s_policy->soc, current_ma,
            (uint32_t)(sample.tick * portTICK_PERIOD_MS),
            sample.xready_generation);
    }
    BMS_Soc_ObserveCorrection(&s_engine, &s_policy->soc,
                              &measurement, now_ms);
    s_snapshot = BMS_Soc_GetEngineSnapshot(&s_engine, &s_policy->soc);
    (void)BMS_Data_PublishSocDiagnostic(
        s_snapshot.remaining_capacity_mah,
        s_snapshot.soc_permille, now_ms, s_snapshot.valid);
}

BMS_SocSnapshot_t BMS_Soc_GetSnapshot(void)
{
    BMS_SocSnapshot_t snapshot;

    vTaskSuspendAll();
    snapshot = s_snapshot;
    (void)xTaskResumeAll();
    return snapshot;
}
