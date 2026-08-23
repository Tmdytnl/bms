#ifndef BMS_DATA_H
#define BMS_DATA_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_config.h"
#include "bms_fault.h"
#include "bms_state.h"
#include "bms_types.h"

/* Metadata for one logical measurement group. */
typedef struct
{
    BMS_TimestampMs_t timestamp_ms;
    BMS_DataAgeMs_t age_ms;
    bool valid;
    bool in_range;
    /* Once observed stale, a 32-bit timestamp wrap cannot make it fresh. */
    bool stale_latched;
} BMS_MeasurementMetadata_t;

/* Cell groups are not strictly simultaneous, so timing is kept per cell. */
typedef struct
{
    BMS_TimestampMs_t timestamp_ms[BMS_CELL_COUNT];
    BMS_DataAgeMs_t age_ms[BMS_CELL_COUNT];
    uint16_t valid_bitmap;
    uint16_t in_range_bitmap;
    uint16_t stale_bitmap;
} BMS_CellMetadata_t;

/*
 * In-memory shared snapshot model only. It is not a CAN or Flash record and
 * must never be serialized by copying its raw C representation.
 */
struct BMS_DataSnapshot
{
    BMS_CellVoltageMv_t cell_voltage_mv[BMS_CELL_COUNT];
    /* Safety decisions use the cell sum; BAT is a diagnostic cross-check. */
    BMS_PackVoltageMv_t pack_voltage_mv;
    BMS_PackVoltageMv_t bq_pack_voltage_mv;
    BMS_CurrentMa_t current_ma;
    uint16_t ts1_raw14;
    uint32_t ts1_resistance_ohm;
    BMS_TemperatureDeciC_t temperature_decic;
    BMS_CapacityMah_t remaining_capacity_mah;
    BMS_SocPermille_t soc_permille;

    BMS_State_t state;
    BMS_FaultSummary_t faults;

    BMS_CellMetadata_t cell_metadata;
    BMS_MeasurementMetadata_t pack_metadata;
    BMS_MeasurementMetadata_t bq_pack_metadata;
    BMS_MeasurementMetadata_t current_metadata;
    /* Raw/resistance validity is separate from calibrated Celsius validity. */
    BMS_MeasurementMetadata_t ts1_metadata;
    BMS_MeasurementMetadata_t temperature_metadata;
    BMS_MeasurementMetadata_t soc_metadata;

    BMS_TimestampMs_t snapshot_timestamp_ms;
    uint32_t sample_sequence;
    uint32_t afe_generation;
};

/*
 * Bounded-stack projection for consumers that only decide freshness. All
 * fields are copied while xDataMutex is held, so the three groups and the
 * sequence always describe one published generation. It deliberately omits
 * the cell arrays and values from BMS_DataSnapshot_t.
 */
typedef struct
{
    BMS_MeasurementMetadata_t pack_metadata;
    BMS_MeasurementMetadata_t current_metadata;
    BMS_MeasurementMetadata_t temperature_metadata;
    uint32_t sample_sequence;
    uint32_t afe_generation;
} BMS_DataFreshnessSnapshot_t;

typedef struct
{
    uint32_t sample_sequence;
    uint32_t afe_generation;
} BMS_DataIdentity_t;

/*
 * One fully staged SampleTask publication. The 13-cell/BQ-pack core is
 * mandatory; current and TS1 are independently timed optional groups. The
 * caller must not publish a failed or partial core transaction.
 *
 * bq_pack_voltage_mv is diagnostic only. pack_voltage_mv is deliberately
 * absent: BMS_Data_PublishMeasurement() computes it from all 13 cells so a
 * caller cannot publish a mismatched cell/pack pair.
 */
typedef struct
{
    BMS_TimestampMs_t timestamp_ms;
    uint32_t afe_generation;
    BMS_CellVoltageMv_t cell_voltage_mv[BMS_CELL_COUNT];
    uint16_t cell_valid_bitmap;
    uint16_t cell_in_range_bitmap;

    BMS_PackVoltageMv_t bq_pack_voltage_mv;
    bool bq_pack_valid;
    bool bq_pack_in_range;

    bool update_current;
    BMS_CurrentMa_t current_ma;
    BMS_TimestampMs_t current_timestamp_ms;
    bool current_valid;
    bool current_in_range;

    bool update_temperature;
    uint16_t ts1_raw14;
    uint32_t ts1_resistance_ohm;
    BMS_TimestampMs_t temperature_timestamp_ms;
    bool ts1_valid;
    BMS_TemperatureDeciC_t temperature_decic;
    bool temperature_valid;
    bool temperature_in_range;
} BMS_MeasurementFrame_t;

#define BMS_DATA_MODEL_VERSION                    (1U)
#define BMS_CELL_BITMAP_WIDTH_BITS               (16U)
#define BMS_CELL_DEFINED_MASK                    ((uint16_t)0x1FFFU)
#define BMS_DATA_VOLTAGE_FRESH_MAX_MS            \
    (BMS_VOLTAGE_FRESH_LIMIT_MS)
#define BMS_DATA_CURRENT_FRESH_MAX_MS            \
    (BMS_CURRENT_FRESH_LIMIT_MS)
#define BMS_DATA_TEMPERATURE_FRESH_MAX_MS        \
    (BMS_TEMPERATURE_FRESH_LIMIT_MS)
#define BMS_DATA_FRESHNESS_SNAPSHOT_MAX_BYTES    (44U)

BMS_BUILD_ASSERT(BMS_DATA_MODEL_VERSION == 1U,
                 data_model_version_is_one);
BMS_BUILD_ASSERT(BMS_CELL_COUNT <= BMS_CELL_BITMAP_WIDTH_BITS,
                 cell_count_fits_validity_bitmap);
BMS_BUILD_ASSERT(BMS_CELL_DEFINED_MASK ==
                     (uint16_t)(((uint16_t)1U << BMS_CELL_COUNT) -
                                (uint16_t)1U),
                 cell_defined_mask_matches_count);
BMS_BUILD_ASSERT(sizeof(BMS_DataFreshnessSnapshot_t) <=
                     BMS_DATA_FRESHNESS_SNAPSHOT_MAX_BYTES,
                 freshness_snapshot_stack_bound);

/*
 * Backing storage retained for startup and phased integration compatibility.
 * Task consumers must use BMS_Data_GetSnapshot(); task writers must use a
 * narrow owner API while holding xDataMutex rather than editing fields.
 */
extern BMS_DataSnapshot_t g_bms_data;

/* Startup-only initialization, called before RTOS objects/tasks exist. */
void BMS_Data_Init(void);

/*
 * Publish a complete staged measurement with a zero-wait xDataMutex take.
 * Returns false for NULL/invalid frames or mutex unavailability, leaving the
 * previous snapshot unchanged. Only measurement-owned fields are changed;
 * state, fault, SOC and capacity fields are preserved.
 */
bool BMS_Data_PublishMeasurement(const BMS_MeasurementFrame_t *frame);

/*
 * Copy one coherent generation directly to the caller output while holding
 * xDataMutex, then derive wrap-safe ages after releasing the mutex. The
 * function has no second full-snapshot local object. Validity is never
 * cleared merely because data is stale. On failure, output is unchanged.
 * Unsigned age subtraction is unambiguous for the configured sub-second/
 * second freshness windows (all far below one uint32_t timestamp wrap).
 * Once a periodic reader observes a threshold crossing it is latched until
 * that group is republished. A reader stalled for a whole timestamp wrap is
 * not distinguishable using this 32-bit clock and requires watchdog coverage.
 */
bool BMS_Data_GetSnapshot(BMS_DataSnapshot_t *snapshot,
                          BMS_TimestampMs_t now_ms);

/*
 * Copy only the pack/current/temperature metadata required for stale
 * decisions. The projection is one coherent generation and ages are derived
 * after mutex release. This is the bounded-stack reader for SampleTask; on
 * failure, output is unchanged.
 */
bool BMS_Data_GetFreshnessSnapshot(
    BMS_DataFreshnessSnapshot_t *snapshot,
    BMS_TimestampMs_t now_ms);

/* Copy only the generation identity under xDataMutex. */
bool BMS_Data_GetIdentity(BMS_DataIdentity_t *identity);

/* Diagnostic projections only; neither API creates FET authority. */
bool BMS_Data_PublishStateDiagnostic(BMS_State_t state,
                                     const BMS_FaultSummary_t *faults);
bool BMS_Data_PublishSocDiagnostic(BMS_CapacityMah_t capacity_mah,
                                   BMS_SocPermille_t soc_permille,
                                   BMS_TimestampMs_t now_ms,
                                   bool valid);

/* Valid and fresh are deliberately separate concepts. A stale latch can be
 * cleared only by publication of that measurement group. */
bool BMS_Data_IsFresh(bool valid,
                      bool stale_latched,
                      uint32_t age_ms,
                      uint32_t max_age_ms);

#endif /* BMS_DATA_H */
