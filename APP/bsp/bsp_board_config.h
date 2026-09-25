#ifndef BSP_BOARD_CONFIG_H
#define BSP_BOARD_CONFIG_H

/* Board/MCU facts only; no functional BMS policy belongs in this layer. */
#define BSP_BUILD_ASSERT(condition, name) \
    typedef char bsp_build_assert_##name[(condition) ? 1 : -1]

#define BSP_BOARD_SYSCLK_HZ                      (72000000UL)
#define BSP_BOARD_HCLK_HZ                        (72000000UL)
#define BSP_BOARD_PCLK1_HZ                       (36000000UL)
#define BSP_BOARD_PCLK2_HZ                       (72000000UL)

#define BSP_BOARD_TIM3_PRESCALER                 (71U)
#define BSP_BOARD_TIM3_AUTORELOAD                (0xFFFFU)
#define BSP_BOARD_TIM3_DELAY_SPIN_GUARD_PER_US   (256UL)

#define BSP_BOARD_CAN_BITRATE                    (500000UL)
#define BSP_BOARD_SOFT_I2C_HALF_CYCLE_US         (5U)
#define BSP_BOARD_SOFT_I2C_SCL_HIGH_TIMEOUT_US   (1000U)
#define BSP_BOARD_SOFT_I2C_BUS_FREE_TIMEOUT_US   (1000U)
#define BSP_BOARD_CAN_RX_PIN                     (11U)
#define BSP_BOARD_CAN_TX_PIN                     (12U)
#define BSP_BOARD_ALERT_PIN                      (1U)
#define BSP_BOARD_UART_TX_PIN                    (9U)
#define BSP_BOARD_UART_RX_PIN                    (10U)

#define BSP_BOARD_FLASH_PAGE_SIZE                (0x00000400UL)
#define BSP_BOARD_PERSISTENCE_A_ADDR             (0x0800F800UL)
#define BSP_BOARD_PERSISTENCE_B_ADDR             (0x0800FC00UL)
#define BSP_BOARD_PERSISTENCE_END_EXCLUSIVE      (0x08010000UL)

BSP_BUILD_ASSERT(BSP_BOARD_SOFT_I2C_HALF_CYCLE_US >= 5U,
                 software_i2c_nominal_rate_is_bounded);
BSP_BUILD_ASSERT(BSP_BOARD_SOFT_I2C_SCL_HIGH_TIMEOUT_US <= 32767U,
                 software_i2c_scl_timeout_is_wrap_safe);
BSP_BUILD_ASSERT(BSP_BOARD_SOFT_I2C_BUS_FREE_TIMEOUT_US <= 32767U,
                 software_i2c_bus_timeout_is_wrap_safe);

#endif /* BSP_BOARD_CONFIG_H */
