#include "apl_system.h"

#include "apl_can.h"
#include "apl_rtos.h"
#include "fml_afe_startup.h"
#include "fml_balance.h"
#include "fml_can.h"
#include "fml_config.h"
#include "fml_data.h"
#include "fml_debug.h"
#include "fml_fet_manager.h"
#include "fml_health.h"
#include "fml_persistence.h"
#include "fml_policy.h"
#include "fml_protect.h"
#include "fml_recovery.h"
#include "fml_sample.h"
#include "fml_soc.h"
#include "fml_state.h"
#include "bsp_clock.h"
#include "bsp_board_config.h"
#include "bsp_exti.h"
#include "bsp_flash.h"
#include "bsp_gpio.h"
#include "bsp_timer.h"
#include "bsp_uart.h"
#include "bsp_soft_i2c.h"

#define APL_AFE_STARTUP_LIMIT_MS                 (5000UL)

/* 启动期创建且覆盖任务生命周期的软件 I2C 总线实例。 */
static SoftI2C_t s_afe_bus;
/* 在同一总线上绑定的 BQ76940 句柄，由 APL 注入各 owner。 */
static BQ76940_t s_afe_device;

/* 把 AFE 启动状态机的唤醒请求转接到板级脉冲原语。 */
static bool APL_AfeWake(void *context)
{
    (void)context;
    return BSP_AFE_WakePulse();
}

/* 在有限时窗内逐步完成 AFE 启动并取得校准，失败则不启动任务。 */
static bool APL_RunAfeStartup(BQ76940_t *device,
                              const BMS_Policy_t *policy,
                              BQ76940_Calibration_t *calibration)
{
    /* 本次 AFE 启动流程的配置与结果。 */
    BMS_AfeStartup_t startup;
    /* AFE 启动流程结果，用于决定是否继续系统初始化。 */
    BMS_AfeStartupResult_t result;
    /* 本次处理的单调毫秒时刻。 */
    uint32_t now_ms;

    if ((device == NULL) || (policy == NULL) || (calibration == NULL) ||
        !FML_AfeStartup_Init(&startup, device, &policy->afe_startup,
                             APL_AfeWake, NULL))
    {
        return false;
    }
    for (now_ms = 0UL; now_ms < APL_AFE_STARTUP_LIMIT_MS; ++now_ms)
    {
        result = FML_AfeStartup_Step(&startup, now_ms);
        if (result == BMS_AFE_STARTUP_RESULT_COMPLETE)
        {
            return FML_AfeStartup_GetCalibration(&startup, calibration);
        }
        if ((result == BMS_AFE_STARTUP_RESULT_FAILED) ||
            !BSP_DelayUs(1000UL))
        {
            return false;
        }
    }
    return false;
}

/* 把持久化算法的读取请求转接到受限 Flash BSP。 */
static bool APL_FlashRead(void *context, uint32_t address,
                          uint8_t *destination, uint16_t length)
{
    (void)context;
    return BSP_Flash_Read(address, destination, length);
}

/* 把持久化算法的擦页请求转接到受限 Flash BSP。 */
static bool APL_FlashErase(void *context, uint32_t page_address)
{
    (void)context;
    return BSP_Flash_ErasePersistencePage(page_address);
}

/* 把持久化算法的 halfword 写请求转接到受限 Flash BSP。 */
static bool APL_FlashProgram(void *context, uint32_t address, uint16_t value)
{
    (void)context;
    return BSP_Flash_ProgramPersistenceHalfWord(address, value);
}

/* 按 GPIO、Timer、UART 的依赖顺序建立板级基础能力。 */
static bool APL_InitBoardPrimitives(void)
{
    /* GPIO/TIM3 先建立引脚与微秒时基，UART readback 成功后才声明板级初始化完成。 */
    BSP_GPIO_Init();
    BSP_Timer_Init();
    return BSP_UART1_Init115200();
}

/* 建立软件 I2C 回调与 BQ76940 句柄，供启动和运行期 owner 使用。 */
static bool APL_InitAfeTransport(void)
{
    /* 软件 I²C 使用的板级线操作回调。 */
    SoftI2C_LineOps_t line_ops;
    /* 软件 I²C 总线时序和超时配置。 */
    SoftI2C_Config_t i2c_config;
    /* 当前 I²C 事务返回的状态码。 */
    SoftI2C_Status_t i2c_status;

    line_ops.scl_drive_low = BSP_I2C_SCL_DriveLow;
    line_ops.scl_release = BSP_I2C_SCL_Release;
    line_ops.scl_read = BSP_I2C_SCL_Read;
    line_ops.sda_drive_low = BSP_I2C_SDA_DriveLow;
    line_ops.sda_release = BSP_I2C_SDA_Release;
    line_ops.sda_read = BSP_I2C_SDA_Read;
    line_ops.time_us16 = BSP_TimeUs16;
    line_ops.delay_us = BSP_DelayUs;
    i2c_config.half_cycle_us = BSP_BOARD_SOFT_I2C_HALF_CYCLE_US;
    i2c_config.scl_high_timeout_us = BSP_BOARD_SOFT_I2C_SCL_HIGH_TIMEOUT_US;
    i2c_config.bus_free_timeout_us = BSP_BOARD_SOFT_I2C_BUS_FREE_TIMEOUT_US;
    i2c_status = BSP_SoftI2C_Init(&s_afe_bus, &line_ops, &i2c_config);
    if (i2c_status == SOFT_I2C_STATUS_SDA_STUCK_LOW)
    {
        /* 只对明确的 SDA stuck-low 执行一次标准 9-clock recovery，不掩盖其他错误。 */
        i2c_status = BSP_SoftI2C_RecoverBus(&s_afe_bus);
    }
    return (i2c_status == SOFT_I2C_STATUS_OK) &&
           (BSP_BQ76940_Init(&s_afe_device, &s_afe_bus) ==
            BQ76940_STATUS_OK);
}

/* 完成 AFE 启动校准后再初始化保护、状态和 FET owner。 */
static bool APL_InitSafetyAndControl(const BMS_Policy_t *policy)
{
    /* 当前 AFE 校准参数。 */
    BQ76940_Calibration_t calibration;

    /* 先绑定 owner，再运行 pre-scheduler AFE startup；失败时不创建任何任务。 */
    FML_Protect_Init();
    FML_Protect_SetDevice(&s_afe_device);
    FML_Protect_SetPolicy(policy, 0UL);
    FML_Sample_SetDevice(&s_afe_device);
    if (!FML_Sample_SetNtcTable(policy->ntc_points,
                                policy->ntc_point_count) ||
        !APL_RunAfeStartup(&s_afe_device, policy, &calibration) ||
        !FML_Sample_SetCalibration(&calibration))
    {
        return false;
    }
    FML_Health_Init();
    FML_State_Init(policy, 0UL);
    FML_Recovery_Init(&s_afe_device, policy);
    FML_FetManager_Init(&s_afe_device);
    FML_Soc_Init(policy);
    return true;
}

/* 若 A/B 页存在有效记录则恢复 SOC，否则沿用安全初始化值。 */
static void APL_RestorePersistedSoc(const BMS_Policy_t *policy)
{
    /* 从 Flash 恢复的持久化数据。 */
    BMS_PersistencePayload_t persisted;
    /* Flash 持久化使用的存储回调。 */
    BMS_PersistenceStorageOps_t storage;

    storage.read = APL_FlashRead;
    storage.erase_page = APL_FlashErase;
    storage.program_halfword = APL_FlashProgram;
    storage.context = NULL;
    if (FML_Persistence_TargetInit(&policy->flash, &storage) &&
        FML_Persistence_TargetGetLatest(&persisted))
    {
        (void)FML_Soc_Restore(persisted.soc_permille,
                              persisted.remaining_capacity_mah);
    }
}

/* 初始化均衡、CAN 协议及调试快照功能。 */
static void APL_InitRuntimeFeatures(const BMS_Policy_t *policy)
{
    FML_Balance_Init(&s_afe_device, policy, true);
    FML_Can_Init(policy);
    FML_Debug_Init();
}

/* 配置 RTOS 队列与七任务，全部就绪后才启动调度器。 */
static bool APL_CreateRuntime(const BMS_Policy_t *policy)
{
    /* BSP 先固定 IRQ priority grouping；对象/任务失败都阻止 scheduler。 */
    BSP_InterruptPriorityInit();
    if (APL_Rtos_CreateObjects() != OS_PASS)
    {
        return false;
    }
    (void)APL_Can_BindTarget(policy);
    return APL_Rtos_CreateTasks(&s_afe_device) == OS_PASS;
}

/* 按 fail-safe 顺序初始化板级原语、AFE、功能模块和 RTOS 对象。 */
bool APL_SystemInit(void)
{
    /* 本轮处理使用的只读 BMS 策略。 */
    const BMS_Policy_t *policy;

    /*
     * 启动顺序本身就是 fail-safe 契约：先建立 invalid 数据初值，验证 immutable
     * policy/clock，再初始化板级原语与 AFE；只有全部 owner 就绪后才创建 RTOS。
     */
    FML_Data_Init();
    FML_Sample_Init();
    policy = FML_Policy_Get();
    if (!FML_Policy_Validate(policy) ||
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

/* 在全部必需模块创建成功后启动 FreeRTOS 调度器。 */
void APL_SystemStart(void)
{
    OS_StartScheduler();
}

/* 关闭中断并停留在不可运行状态，防止初始化失败后启动任务。 */
void APL_SafeIdle(void)
{
    __disable_irq();
    for (;;)
    {

    }
}
