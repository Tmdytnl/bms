#ifndef BMS_CONFIG_H
#define BMS_CONFIG_H

#include <stdint.h>

#include "bms_build_assert.h"

/*
 * BMS V1 compile-time reference configuration only.
 * Final hardware requires calibration and validation before deployment.
 */
#define BMS_PROJECT_NAME                         "BMS V1"
#define BMS_REFERENCE_CHEMISTRY                  "NMC"
#define BMS_MCU_PART_NAME                        "STM32F103C8T6"
#define BMS_AFE_PART_NAME                        "BQ7694003"

#define BMS_CONFIG_MODEL_VERSION                 (1U)
#define BMS_CELL_COUNT                           (13U)
#define BMS_REFERENCE_CAPACITY_MAH               (20000U)
#define BMS_RSENSE_REFERENCE_UOHM                (4000U)
#define BMS_NTC_REFERENCE_OHM                    (10000U)

/* Phase 8 measurement policy. These timing and range limits come from the
 * unified software specification; they are not board-calibration claims. */
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

#define BMS_HSE_CLOCK_HZ                         (8000000UL)
#define BMS_SYSCLK_HZ                            (72000000UL)
#define BMS_HCLK_HZ                              (72000000UL)
#define BMS_PCLK1_HZ                             (36000000UL)
#define BMS_PCLK2_HZ                             (72000000UL)

#define BMS_TIM3_INPUT_CLOCK_HZ                  (72000000UL)
#define BMS_TIM3_PRESCALER                       (71U)
#define BMS_TIM3_AUTORELOAD                      (0xFFFFU)
#define BMS_TIM3_COUNTER_HZ                      (1000000UL)
#define BMS_TIM3_DELAY_SPIN_GUARD_PER_US         (256UL)

/* Nominal Standard-mode software-I2C timing; board waveforms remain unverified. */
#define BMS_SOFT_I2C_HALF_CYCLE_US               (5U)
#define BMS_SOFT_I2C_SCL_HIGH_TIMEOUT_US         (1000U)
#define BMS_SOFT_I2C_BUS_FREE_TIMEOUT_US         (1000U)

#define BMS_CAN_BITRATE                          (500000UL)

/* Port identifiers are project labels, not STM32 SPL GPIO pointer values. */
#define BMS_GPIO_PORT_A_ID                       (0U)
#define BMS_GPIO_PORT_B_ID                       (1U)

#define BMS_SOFT_I2C_SCL_PORT_ID                 BMS_GPIO_PORT_B_ID
#define BMS_SOFT_I2C_SCL_PIN                     (8U)   /* PB8 */
#define BMS_SOFT_I2C_SDA_PORT_ID                 BMS_GPIO_PORT_B_ID
#define BMS_SOFT_I2C_SDA_PIN                     (9U)   /* PB9 */
#define BMS_AFE_ALERT_PORT_ID                     BMS_GPIO_PORT_B_ID
#define BMS_AFE_ALERT_PIN                        (1U)   /* PB1 */
#define BMS_AFE_WAKE_PORT_ID                      BMS_GPIO_PORT_A_ID
#define BMS_AFE_WAKE_PIN                         (8U)   /* PA8 */
#define BMS_CAN_RX_PORT_ID                        BMS_GPIO_PORT_A_ID
#define BMS_CAN_RX_PIN                           (11U)  /* PA11 */
#define BMS_CAN_TX_PORT_ID                        BMS_GPIO_PORT_A_ID
#define BMS_CAN_TX_PIN                           (12U)  /* PA12 */

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
BMS_BUILD_ASSERT(BMS_HSE_CLOCK_HZ == 8000000UL,
                 hse_reference_is_eight_mhz);
BMS_BUILD_ASSERT(BMS_SYSCLK_HZ == 72000000UL,
                 sysclk_target_is_seventy_two_mhz);
BMS_BUILD_ASSERT((BMS_TIM3_INPUT_CLOCK_HZ /
                      (BMS_TIM3_PRESCALER + 1U)) ==
                     BMS_TIM3_COUNTER_HZ,
                 tim3_prescaler_produces_one_mhz);
BMS_BUILD_ASSERT(BMS_TIM3_DELAY_SPIN_GUARD_PER_US >= 64UL,
                 tim3_delay_guard_has_clock_fault_margin);
BMS_BUILD_ASSERT(BMS_SOFT_I2C_HALF_CYCLE_US >= 5U,
                 software_i2c_nominal_rate_is_not_above_one_hundred_khz);
BMS_BUILD_ASSERT(BMS_SOFT_I2C_SCL_HIGH_TIMEOUT_US <= 32767U,
                 software_i2c_scl_timeout_is_wrap_safe);
BMS_BUILD_ASSERT(BMS_SOFT_I2C_BUS_FREE_TIMEOUT_US <= 32767U,
                 software_i2c_bus_timeout_is_wrap_safe);

#endif /* BMS_CONFIG_H */
