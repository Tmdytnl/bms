#include "app_rtos.h"
#include "bms_afe_startup.h"
#include "bms_data.h"
#include "bms_config.h"
#include "bms_fet_manager.h"
#include "bms_health.h"
#include "bms_memory_map.h"
#include "bms_policy.h"
#include "bms_protect.h"
#include "bms_sample.h"
#include "bms_recovery.h"
#include "bms_state.h"
#include "bsp_clock.h"
#include "bsp_gpio.h"
#include "bsp_timer.h"
#include "bq76940.h"
#include "soft_i2c.h"

#include "misc.h"   /* NVIC_PriorityGroupConfig (SPL, Phase 6/7 target) */

static SoftI2C_t s_afe_bus;
static BQ76940_t s_afe_device;

#define BMS_MAIN_AFE_STARTUP_LIMIT_MS            (5000UL)

static void BMS_SafeIdle(void)
{
    __disable_irq();
    while (1)
    {
    }
}

static bool BMS_Main_AfeWake(void *context)
{
    (void)context;
    return BSP_AFE_WakePulse();
}

static bool BMS_Main_RunAfeStartup(
    BQ76940_t *device,
    const BMS_Policy_t *policy,
    BQ76940_Calibration_t *calibration)
{
    BMS_AfeStartup_t startup;
    BMS_AfeStartupResult_t result;
    uint32_t now_ms;

    if ((device == NULL) || (policy == NULL) || (calibration == NULL) ||
        !BMS_AfeStartup_Init(&startup,
                             device,
                             &policy->afe_startup,
                             BMS_Main_AfeWake,
                             NULL))
    {
        return false;
    }

    now_ms = 0UL;
    while (now_ms < BMS_MAIN_AFE_STARTUP_LIMIT_MS)
    {
        result = BMS_AfeStartup_Step(&startup, now_ms);
        if (result == BMS_AFE_STARTUP_RESULT_COMPLETE)
        {
            return BMS_AfeStartup_GetCalibration(&startup, calibration);
        }
        if (result == BMS_AFE_STARTUP_RESULT_FAILED)
        {
            return false;
        }
        if (!BSP_DelayUs(1000UL))
        {
            return false;
        }
        ++now_ms;
    }
    return false;
}

int main(void)
{
    SoftI2C_LineOps_t line_ops;
    SoftI2C_Config_t i2c_config;
    SoftI2C_Status_t i2c_status;
    const BMS_Policy_t *policy;
    BQ76940_Calibration_t calibration;

    /* Reset_Handler has already called the CMSIS SystemInit function. */
    BMS_Data_Init();
    BMS_Sample_Init();
    policy = BMS_Policy_Get();
    if (!BMS_Policy_Validate(policy))
    {
        BMS_SafeIdle();
    }

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
    BMS_Protect_Init();
    BMS_Protect_SetDevice(&s_afe_device);
    BMS_Protect_SetPolicy(policy);
    BMS_Sample_SetDevice(&s_afe_device);
    if (!BMS_Sample_SetNtcTable(policy->ntc_points,
                                policy->ntc_point_count) ||
        !BMS_Main_RunAfeStartup(&s_afe_device,
                                policy,
                                &calibration) ||
        !BMS_Sample_SetCalibration(&calibration))
    {
        BMS_SafeIdle();
    }
    BMS_Health_Init();
    BMS_State_Init(policy, 0UL);
    BMS_Recovery_Init(&s_afe_device, policy);
    BMS_FetManager_Init(&s_afe_device);

    /* Phase 6/7: create all objects/tasks before scheduler start. ALERT EXTI
     * is intentionally enabled by the first ProtectTask context only after
     * the FreeRTOS port has initialized its ISR-priority validation state.
     * NVIC PriorityGroup_4 is locked before scheduler/interrupt activation. */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
    if (App_Rtos_CreateObjects() != pdTRUE)
    {
        BMS_SafeIdle();
    }
    /* SIM_POLICY_V1 is an explicit learning/simulation input. Calibration
     * still comes from this device instance; no fixed gain/offset is used. */
    if (App_Rtos_CreateTasks() != pdTRUE)
    {
        BMS_SafeIdle();
    }
    vTaskStartScheduler();

    /* vTaskStartScheduler only returns on fatal configuration error. */
    BMS_SafeIdle();
}
