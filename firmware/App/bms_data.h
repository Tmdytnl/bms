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
} BMS_MeasurementMetadata_t;

/* Cell groups are not strictly simultaneous, so timing is kept per cell. */
typedef struct
{
    BMS_TimestampMs_t timestamp_ms[BMS_CELL_COUNT];
    BMS_DataAgeMs_t age_ms[BMS_CELL_COUNT];
    uint16_t valid_bitmap;
    uint16_t in_range_bitmap;
} BMS_CellMetadata_t;

/*
 * In-memory shared snapshot model only. It is not a CAN or Flash record and
 * must never be serialized by copying its raw C representation.
 */
typedef struct
{
    BMS_CellVoltageMv_t cell_voltage_mv[BMS_CELL_COUNT];
    BMS_PackVoltageMv_t pack_voltage_mv;
    BMS_CurrentMa_t current_ma;
    BMS_TemperatureDeciC_t temperature_decic;
    BMS_CapacityMah_t remaining_capacity_mah;
    BMS_SocPermille_t soc_permille;

    BMS_State_t state;
    BMS_FaultSummary_t faults;

    BMS_CellMetadata_t cell_metadata;
    BMS_MeasurementMetadata_t pack_metadata;
    BMS_MeasurementMetadata_t current_metadata;
    BMS_MeasurementMetadata_t temperature_metadata;
    BMS_MeasurementMetadata_t soc_metadata;

    BMS_TimestampMs_t snapshot_timestamp_ms;
    uint32_t sample_sequence;
} BMS_DataSnapshot_t;

#define BMS_DATA_MODEL_VERSION                    (1U)
#define BMS_CELL_BITMAP_WIDTH_BITS               (16U)
#define BMS_CELL_DEFINED_MASK                    ((uint16_t)0x1FFFU)

BMS_BUILD_ASSERT(BMS_DATA_MODEL_VERSION == 1U,
                 data_model_version_is_one);
BMS_BUILD_ASSERT(BMS_CELL_COUNT <= BMS_CELL_BITMAP_WIDTH_BITS,
                 cell_count_fits_validity_bitmap);
BMS_BUILD_ASSERT(BMS_CELL_DEFINED_MASK ==
                     (uint16_t)(((uint16_t)1U << BMS_CELL_COUNT) -
                                (uint16_t)1U),
                 cell_defined_mask_matches_count);

extern BMS_DataSnapshot_t g_bms_data;

void BMS_Data_Init(void);

#endif /* BMS_DATA_H */
