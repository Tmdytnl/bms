#include "bms_data.h"
#include "bms_config.h"
#include "bms_memory_map.h"
#include "bsp_clock.h"
#include "bsp_gpio.h"
#include "bsp_timer.h"
#include "bq76940.h"
#include "soft_i2c.h"

static SoftI2C_t s_afe_bus;
static BQ76940_t s_afe_device;

static void BMS_SafeIdle(void)
{
    while (1)
    {
    }
}

int main(void)
{
    SoftI2C_LineOps_t line_ops;
    SoftI2C_Config_t i2c_config;
    SoftI2C_Status_t i2c_status;

    /* Reset_Handler has already called the CMSIS SystemInit function. */
    BMS_Data_Init();

    /* A clock mismatch blocks all later hardware initialization. */
    if (BSP_Clock_Verify() != BSP_CLOCK_STATUS_OK)
    {
        BMS_SafeIdle();
    }

    BSP_GPIO_Init();
    BSP_Timer_Init();

    line_ops.scl_drive_low = BSP_I2C_SCL_DriveLow;
    line_ops.scl_release = BSP_I2C_SCL_Release;
    line_ops.scl_read = BSP_I2C_SCL_Read;
    line_ops.sda_drive_low = BSP_I2C_SDA_DriveLow;
    line_ops.sda_release = BSP_I2C_SDA_Release;
    line_ops.sda_read = BSP_I2C_SDA_Read;
    line_ops.time_us16 = BSP_TimeUs16;
    line_ops.delay_us = BSP_DelayUs;

    i2c_config.half_cycle_us = BMS_SOFT_I2C_HALF_CYCLE_US;
    i2c_config.scl_high_timeout_us = BMS_SOFT_I2C_SCL_HIGH_TIMEOUT_US;
    i2c_config.bus_free_timeout_us = BMS_SOFT_I2C_BUS_FREE_TIMEOUT_US;
    i2c_status = SoftI2C_Init(&s_afe_bus, &line_ops, &i2c_config);
    if (i2c_status == SOFT_I2C_STATUS_SDA_STUCK_LOW)
    {
        i2c_status = SoftI2C_RecoverBus(&s_afe_bus);
    }
    if (i2c_status != SOFT_I2C_STATUS_OK)
    {
        BMS_SafeIdle();
    }

    if (BQ76940_Init(&s_afe_device, &s_afe_bus) != BQ76940_STATUS_OK)
    {
        BMS_SafeIdle();
    }

    /* Phase 3 stops here: no BQ transaction, probe, or business logic starts. */
    BMS_SafeIdle();
}
