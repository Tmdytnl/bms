#include "app_rtos.h"
#include "bms_afe_startup.h"
#include "bms_balance.h"
#include "bms_can.h"
#include "bms_data.h"
#include "bms_debug.h"
#include "bms_config.h"
#include "bms_fet_manager.h"
#include "bms_health.h"
#include "bms_memory_map.h"
#include "bms_policy.h"
#include "bms_persistence.h"
#include "bms_protect.h"
#include "bms_sample.h"
#include "bms_recovery.h"
#include "bms_soc.h"
#include "bms_state.h"
#include "bsp_clock.h"
#include "bsp_gpio.h"
#include "bsp_timer.h"
#include "bsp_uart.h"
#include "bq76940.h"
#include "soft_i2c.h"

#include "misc.h"   /* SPL 的 NVIC_PriorityGroupConfig */

static SoftI2C_t s_afe_bus;
static BQ76940_t s_afe_device;

/*
 * main 只负责 pre-scheduler wiring：验证 clock、初始化 BSP/transport、运行有界
 * AFE startup、把 calibration 与 immutable policy 交给各 owner、恢复 SOC，最后
 * 一次创建 IPC/tasks。任一 mandatory step 失败都进入关中断 safe idle，绝不以
 * 半初始化系统启动 scheduler。
 */

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
    BMS_PersistencePayload_t persisted;

    /* Reset_Handler 已调用 CMSIS SystemInit，此处从应用级安全初值开始。 */
    BMS_Data_Init();
    BMS_Sample_Init();
    policy = BMS_Policy_Get();
    if (!BMS_Policy_Validate(policy))
    {
        BMS_SafeIdle();
    }

    /* clock 不匹配会使所有 timing 假设失效，因此阻止后续硬件初始化。 */
    if (BSP_Clock_Verify() != BSP_CLOCK_STATUS_OK)
    {
        BMS_SafeIdle();
    }

    BSP_GPIO_Init();
    BSP_Timer_Init();
    if (!BSP_UART1_Init115200())
    {
        BMS_SafeIdle();
    }

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
    BMS_Soc_Init(policy);
    if (BMS_Persistence_TargetInit(&policy->flash) &&
        BMS_Persistence_TargetGetLatest(&persisted))
    {
        (void)BMS_Soc_Restore(persisted.soc_permille,
                              persisted.remaining_capacity_mah);
    }
    /* AFE startup 已回读确认 CELLBAL1..3 全零，将该证据交给 Balance owner。 */
    BMS_Balance_Init(&s_afe_device, policy, true);
    BMS_Can_Init(policy);
    BMS_Debug_Init();

    /*
     * scheduler 前创建完整 IPC/task 集合。ALERT EXTI 由首个 ProtectTask context
     * 启用，确保 FreeRTOS ISR-priority validator 已初始化；在任何 IRQ enable 前
     * 固定 NVIC PriorityGroup_4。
     */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
    if (App_Rtos_CreateObjects() != pdTRUE)
    {
        BMS_SafeIdle();
    }
    /*
     * CAN peripheral init 与本地 protection ownership 解耦；RX IRQ 稍后由
     * CANRxTask 启用，此时 FreeRTOS ISR validation 已生效。
     */
    (void)BMS_Can_BindTarget(policy);
    /* calibration 来自当前 device startup readback，不使用固定 gain/offset。 */
    if (App_Rtos_CreateTasks() != pdTRUE)
    {
        BMS_SafeIdle();
    }
    vTaskStartScheduler();

    /* vTaskStartScheduler 正常不返回；返回即按 fatal configuration error 停机。 */
    BMS_SafeIdle();
}
