#include "apl_system.h"

#include "apl_can.h"
#include "apl_rtos.h"
#include "bms_afe_startup.h"
#include "bms_balance.h"
#include "bms_can.h"
#include "bms_config.h"
#include "bms_data.h"
#include "bms_debug.h"
#include "bms_fet_manager.h"
#include "bms_health.h"
#include "bms_persistence.h"
#include "bms_policy.h"
#include "bms_protect.h"
#include "bms_recovery.h"
#include "bms_sample.h"
#include "bms_soc.h"
#include "bms_state.h"
#include "bsp_clock.h"
#include "bsp_flash.h"
#include "bsp_gpio.h"
#include "bsp_timer.h"
#include "bsp_uart.h"
#include "misc.h"
#include "soft_i2c.h"

#define APL_AFE_STARTUP_LIMIT_MS                 (5000UL)

static SoftI2C_t s_afe_bus;
static BQ76940_t s_afe_device;

static bool APL_AfeWake(void *context)
{
    (void)context;
    return BSP_AFE_WakePulse();
}

static bool APL_RunAfeStartup(BQ76940_t *device,
                              const BMS_Policy_t *policy,
                              BQ76940_Calibration_t *calibration)
{
    BMS_AfeStartup_t startup;
    BMS_AfeStartupResult_t result;
    uint32_t now_ms;

    if ((device == NULL) || (policy == NULL) || (calibration == NULL) ||
        !BMS_AfeStartup_Init(&startup, device, &policy->afe_startup,
                             APL_AfeWake, NULL))
    {
        return false;
    }
    for (now_ms = 0UL; now_ms < APL_AFE_STARTUP_LIMIT_MS; ++now_ms)
    {
        result = BMS_AfeStartup_Step(&startup, now_ms);
        if (result == BMS_AFE_STARTUP_RESULT_COMPLETE)
        {
            return BMS_AfeStartup_GetCalibration(&startup, calibration);
        }
        if ((result == BMS_AFE_STARTUP_RESULT_FAILED) ||
            !BSP_DelayUs(1000UL))
        {
            return false;
        }
    }
    return false;
}

static bool APL_FlashRead(void *context, uint32_t address,
                          uint8_t *destination, uint16_t length)
{
    (void)context;
    return BSP_Flash_Read(address, destination, length);
}

static bool APL_FlashErase(void *context, uint32_t page_address)
{
    (void)context;
    return BSP_Flash_ErasePersistencePage(page_address);
}

static bool APL_FlashProgram(void *context, uint32_t address, uint16_t value)
{
    (void)context;
    return BSP_Flash_ProgramPersistenceHalfWord(address, value);
}

static bool APL_InitBoardPrimitives(void)
{
    /* GPIO/TIM3 先建立引脚与微秒时基，UART readback 成功后才声明板级初始化完成。 */
    BSP_GPIO_Init();
    BSP_Timer_Init();
    return BSP_UART1_Init115200();
}

static bool APL_InitAfeTransport(void)
{
    SoftI2C_LineOps_t line_ops;
    SoftI2C_Config_t i2c_config;
    SoftI2C_Status_t i2c_status;

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
        /* 只对明确的 SDA stuck-low 执行一次标准 9-clock recovery，不掩盖其他错误。 */
        i2c_status = SoftI2C_RecoverBus(&s_afe_bus);
    }
    return (i2c_status == SOFT_I2C_STATUS_OK) &&
           (BQ76940_Init(&s_afe_device, &s_afe_bus) ==
            BQ76940_STATUS_OK);
}

static bool APL_InitSafetyAndControl(const BMS_Policy_t *policy)
{
    BQ76940_Calibration_t calibration;

    /* 先绑定 owner，再运行 pre-scheduler AFE startup；失败时不创建任何任务。 */
    BMS_Protect_Init();
    BMS_Protect_SetDevice(&s_afe_device);
    BMS_Protect_SetPolicy(policy, 0UL);
    BMS_Sample_SetDevice(&s_afe_device);
    if (!BMS_Sample_SetNtcTable(policy->ntc_points,
                                policy->ntc_point_count) ||
        !APL_RunAfeStartup(&s_afe_device, policy, &calibration) ||
        !BMS_Sample_SetCalibration(&calibration))
    {
        return false;
    }
    BMS_Health_Init();
    BMS_State_Init(policy, 0UL);
    BMS_Recovery_Init(&s_afe_device, policy);
    BMS_FetManager_Init(&s_afe_device);
    BMS_Soc_Init(policy);
    return true;
}

static void APL_RestorePersistedSoc(const BMS_Policy_t *policy)
{
    BMS_PersistencePayload_t persisted;
    BMS_PersistenceStorageOps_t storage;

    storage.read = APL_FlashRead;
    storage.erase_page = APL_FlashErase;
    storage.program_halfword = APL_FlashProgram;
    storage.context = NULL;
    if (BMS_Persistence_TargetInit(&policy->flash, &storage) &&
        BMS_Persistence_TargetGetLatest(&persisted))
    {
        (void)BMS_Soc_Restore(persisted.soc_permille,
                              persisted.remaining_capacity_mah);
    }
}

static void APL_InitRuntimeFeatures(const BMS_Policy_t *policy)
{
    BMS_Balance_Init(&s_afe_device, policy, true);
    BMS_Can_Init(policy);
    BMS_Debug_Init();
}

static bool APL_CreateRuntime(const BMS_Policy_t *policy)
{
    /* IRQ priority grouping 属于 APL composition；对象/任务任一步失败都阻止 scheduler。 */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
    if (APL_Rtos_CreateObjects() != pdTRUE)
    {
        return false;
    }
    (void)APL_Can_BindTarget(policy);
    return APL_Rtos_CreateTasks(&s_afe_device) == pdTRUE;
}

bool APL_SystemInit(void)
{
    const BMS_Policy_t *policy;

    /*
     * 启动顺序本身就是 fail-safe 契约：先建立 invalid 数据初值，验证 immutable
     * policy/clock，再初始化板级原语与 AFE；只有全部 owner 就绪后才创建 RTOS。
     */
    BMS_Data_Init();
    BMS_Sample_Init();
    policy = BMS_Policy_Get();
    if (!BMS_Policy_Validate(policy) ||
        (BSP_Clock_Verify() != BSP_CLOCK_STATUS_OK) ||
        !APL_InitBoardPrimitives() ||
        !APL_InitAfeTransport() ||
        !APL_InitSafetyAndControl(policy))
    {
        return false;
    }

    APL_RestorePersistedSoc(policy);
    APL_InitRuntimeFeatures(policy);
    return APL_CreateRuntime(policy);
}

void APL_SystemStart(void)
{
    vTaskStartScheduler();
}

void APL_SafeIdle(void)
{
    __disable_irq();
    for (;;)
    {
    }
}
