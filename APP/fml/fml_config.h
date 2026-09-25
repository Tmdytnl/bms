#ifndef FML_CONFIG_H
#define FML_CONFIG_H

#include <stdint.h>

#include "fml_build_assert.h"

/* BMS 领域编译配置；运行策略集中在 fml_policy，禁止在业务代码散落常量。 */
#define BMS_PROJECT_NAME                         "BMS V1"
#define BMS_REFERENCE_CHEMISTRY                  "NMC"

#define BMS_CONFIG_MODEL_VERSION                 (1U)
#define BMS_CELL_COUNT                           (13U)
#define BMS_REFERENCE_CAPACITY_MAH               (20000U)
#define BMS_RSENSE_REFERENCE_UOHM                (4000U)
#define BMS_NTC_REFERENCE_OHM                    (10000U)

/* measurement period、freshness 与输入 range 的集中配置。 */
#define BMS_SAMPLE_PERIOD_MS                     (250U)
#define BMS_TEMPERATURE_SAMPLE_DIVIDER           (8U)
#define BMS_I2C_MUTEX_TIMEOUT_MS                 (20U)
#define BMS_VOLTAGE_FRESH_LIMIT_MS               (1000UL)
#define BMS_CURRENT_FRESH_LIMIT_MS               (1000UL)
#define BMS_TEMPERATURE_FRESH_LIMIT_MS           (5000UL)
#define BMS_CELL_VALID_MIN_MV                    (2000U)
#define BMS_CELL_VALID_MAX_MV                    (5000U)
#define BMS_STARTUP_CELL_MIN_MV                  (2500U)
#define BMS_STARTUP_CELL_MAX_MV                  (4300U)
#define BMS_AFE_WAKE_SETTLE_MS                   (10U)
#define BMS_AFE_INITIAL_DATA_SETTLE_MS           (800U)
#define BMS_CURRENT_POLARITY                     (1)
/* BMS V1 CAN 协议要求的线速；板级 CAN 时钟与分频另见 BSP 配置。 */
#define BMS_CAN_BITRATE                          (500000UL)

BMS_BUILD_ASSERT(BMS_CONFIG_MODEL_VERSION == 1U,
                 config_model_version_is_one);
BMS_BUILD_ASSERT(BMS_CELL_COUNT == 13U,
                 configured_cell_count_is_thirteen);
BMS_BUILD_ASSERT(BMS_REFERENCE_CAPACITY_MAH == 20000U,
                 reference_capacity_is_twenty_ah);
BMS_BUILD_ASSERT(BMS_SAMPLE_PERIOD_MS == 250U,
                 sample_period_is_two_hundred_fifty_ms);
BMS_BUILD_ASSERT(BMS_TEMPERATURE_SAMPLE_DIVIDER == 8U,
                 temperature_period_is_eight_sample_cycles);
BMS_BUILD_ASSERT(BMS_I2C_MUTEX_TIMEOUT_MS < BMS_SAMPLE_PERIOD_MS,
                 sample_i2c_timeout_is_bounded_by_period);
BMS_BUILD_ASSERT(BMS_CELL_VALID_MIN_MV < BMS_CELL_VALID_MAX_MV,
                 cell_measurement_range_is_ordered);
BMS_BUILD_ASSERT(BMS_STARTUP_CELL_MIN_MV >= BMS_CELL_VALID_MIN_MV,
                 startup_cell_min_is_inside_measurement_range);
BMS_BUILD_ASSERT(BMS_STARTUP_CELL_MAX_MV <= BMS_CELL_VALID_MAX_MV,
                 startup_cell_max_is_inside_measurement_range);
BMS_BUILD_ASSERT((BMS_CURRENT_POLARITY == 1) ||
                     (BMS_CURRENT_POLARITY == -1),
                 current_polarity_is_signed_unit);
#endif /* FML_CONFIG_H */
