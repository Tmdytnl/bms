#ifndef BMS_TYPES_H
#define BMS_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_build_assert.h"

/*
 * Public in-memory scalar types. These are not wire or persistent layouts.
 * Protocol and persistence layers must serialize fields explicitly.
 */
typedef uint16_t BMS_CellVoltageMv_t;       /* millivolts */
typedef uint32_t BMS_PackVoltageMv_t;       /* millivolts */
typedef int32_t  BMS_CurrentMa_t;           /* milliamps: +charge, -discharge */
typedef int16_t  BMS_TemperatureDeciC_t;    /* 0.1 degree Celsius */
typedef uint32_t BMS_CapacityMah_t;         /* milliamp-hours */
typedef uint16_t BMS_SocPermille_t;         /* valid range: 0..1000 */
typedef uint32_t BMS_TimestampMs_t;         /* milliseconds */
typedef uint32_t BMS_DataAgeMs_t;           /* milliseconds */

#define BMS_PUBLIC_MODEL_VERSION             (1U)
#define BMS_SOC_PERMILLE_MIN                 (0U)
#define BMS_SOC_PERMILLE_MAX                 (1000U)
#define BMS_SOC_UNKNOWN_PERMILLE             ((BMS_SocPermille_t)0xFFFFU)
#define BMS_DATA_AGE_UNKNOWN_MS              ((BMS_DataAgeMs_t)0xFFFFFFFFUL)

BMS_BUILD_ASSERT(BMS_PUBLIC_MODEL_VERSION == 1U,
                 public_model_version_is_one);
BMS_BUILD_ASSERT(BMS_SOC_PERMILLE_MIN == 0U,
                 soc_min_is_zero);
BMS_BUILD_ASSERT(BMS_SOC_PERMILLE_MAX == 1000U,
                 soc_max_is_one_thousand);
BMS_BUILD_ASSERT(BMS_SOC_UNKNOWN_PERMILLE > BMS_SOC_PERMILLE_MAX,
                 soc_unknown_is_outside_valid_range);
BMS_BUILD_ASSERT(sizeof(BMS_CellVoltageMv_t) == 2U,
                 cell_voltage_type_is_two_bytes);
BMS_BUILD_ASSERT(sizeof(BMS_PackVoltageMv_t) == 4U,
                 pack_voltage_type_is_four_bytes);
BMS_BUILD_ASSERT(sizeof(BMS_CurrentMa_t) == 4U,
                 current_type_is_four_bytes);
BMS_BUILD_ASSERT(sizeof(BMS_TemperatureDeciC_t) == 2U,
                 temperature_type_is_two_bytes);
BMS_BUILD_ASSERT(sizeof(BMS_CapacityMah_t) == 4U,
                 capacity_type_is_four_bytes);
BMS_BUILD_ASSERT(sizeof(BMS_SocPermille_t) == 2U,
                 soc_type_is_two_bytes);
BMS_BUILD_ASSERT(sizeof(BMS_TimestampMs_t) == 4U,
                 timestamp_type_is_four_bytes);
BMS_BUILD_ASSERT(sizeof(BMS_DataAgeMs_t) == 4U,
                 data_age_type_is_four_bytes);

#endif /* BMS_TYPES_H */
