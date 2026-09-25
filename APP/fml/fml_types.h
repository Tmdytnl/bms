#ifndef FML_TYPES_H
#define FML_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#include "fml_build_assert.h"

/* 公共内存标量类型，不是 wire/Flash layout；协议层必须逐字段显式序列化。 */
typedef uint16_t BMS_CellVoltageMv_t;       /* 单节电压，mV */
typedef uint32_t BMS_PackVoltageMv_t;       /* 包电压，mV */
typedef int32_t  BMS_CurrentMa_t;           /* 电流，mA：正充电、负放电 */
typedef int16_t  BMS_TemperatureDeciC_t;    /* 温度，0.1 °C */
typedef uint32_t BMS_CapacityMah_t;         /* 容量，mAh */
typedef uint16_t BMS_SocPermille_t;         /* SOC 千分比，有效域 0..1000 */
typedef uint32_t BMS_TimestampMs_t;         /* 单调时间戳，ms */
typedef uint32_t BMS_DataAgeMs_t;           /* 数据年龄，ms */

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

#endif /* FML_TYPES_H：include guard */
