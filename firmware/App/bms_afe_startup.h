#ifndef BMS_AFE_STARTUP_H
#define BMS_AFE_STARTUP_H

#include <stdbool.h>
#include <stdint.h>

#include "bq76940.h"

#define BMS_AFE_STARTUP_MAX_PROBE_ATTEMPTS       (3U)
#define BMS_AFE_STARTUP_REGISTER_COUNT            (12U)
#define BMS_AFE_STARTUP_SYS_CTRL1_REQUIRED        ((uint8_t)0x18U)
#define BMS_AFE_STARTUP_SYS_CTRL2_FET_OFF         ((uint8_t)0x00U)
#define BMS_AFE_STARTUP_SYS_CTRL2_CC_FET_OFF      ((uint8_t)0x40U)

/*
 * startup 对 SYS_STAT 的 ownership 刻意收窄：CC_READY 留给 ProtectTask；保护类
 * event 绝不在此 W1C，而是在 FET-off 与三组 CELLBAL-off 都回读确认后终止启动。
 * XREADY 是启动期唯一允许清除的位，而且必须先完成一遍配置与 settle；W1C 后
 * 旧 register/calibration evidence 全部失效，必须重新执行完整配置，不能复用。
 */
#define BMS_AFE_STARTUP_STAT_CC_READY              ((uint8_t)0x80U)
#define BMS_AFE_STARTUP_STAT_DEVICE_XREADY         ((uint8_t)0x20U)
#define BMS_AFE_STARTUP_STAT_BLOCKING_MASK         ((uint8_t)0x1FU)

/*
 * callback 只产生一次 board-specific PA8→TS1 rising edge，必须是有界 GPIO
 * 动作：无 delay、I2C transaction、retry loop 或 RTOS wait。
 */
typedef bool (*BMS_AfeStartupWakeFn_t)(void *context);

typedef struct
{
    bool ov_uv_trip_present;
    uint16_t ov_trip_mv;
    uint16_t uv_trip_mv;

    bool protect1_present;
    bool protect1_rsns;
    uint8_t protect1_scd_delay_code;
    uint8_t protect1_scd_threshold_code;

    bool protect2_present;
    uint8_t protect2_ocd_delay_code;
    uint8_t protect2_ocd_threshold_code;

    /* 即使 exact code 为 0 也必须显式 present，防止缺失 PROTECT3 被零初始化伪装。 */
    bool protect3_present;
    uint8_t protect3_uv_delay_code;
    uint8_t protect3_ov_delay_code;
} BMS_AfeStartupConfig_t;

typedef enum
{
    BMS_AFE_STARTUP_STATE_UNINITIALIZED = 0,
    BMS_AFE_STARTUP_STATE_WAKE,
    BMS_AFE_STARTUP_STATE_WAIT_WAKE_SETTLE,
    BMS_AFE_STARTUP_STATE_PROBE,
    BMS_AFE_STARTUP_STATE_WRITE_REGISTER,
    BMS_AFE_STARTUP_STATE_VERIFY_REGISTER,
    BMS_AFE_STARTUP_STATE_READ_ADCGAIN1,
    BMS_AFE_STARTUP_STATE_READ_ADCOFFSET,
    BMS_AFE_STARTUP_STATE_READ_ADCGAIN2,
    BMS_AFE_STARTUP_STATE_PREPARE_PROTECTION,
    BMS_AFE_STARTUP_STATE_WAIT_INITIAL_DATA,
    BMS_AFE_STARTUP_STATE_READ_FINAL_STATUS,
    BMS_AFE_STARTUP_STATE_CLEAR_XREADY,
    BMS_AFE_STARTUP_STATE_SAFE_OFF_WRITE,
    BMS_AFE_STARTUP_STATE_SAFE_OFF_VERIFY,
    BMS_AFE_STARTUP_STATE_COMPLETE,
    BMS_AFE_STARTUP_STATE_FAILED
} BMS_AfeStartupState_t;

typedef enum
{
    BMS_AFE_STARTUP_FAILURE_NONE = 0,
    BMS_AFE_STARTUP_FAILURE_INVALID_ARGUMENT,
    BMS_AFE_STARTUP_FAILURE_INVALID_CONFIG,
    BMS_AFE_STARTUP_FAILURE_DEVICE_NOT_READY,
    BMS_AFE_STARTUP_FAILURE_WAKE,
    BMS_AFE_STARTUP_FAILURE_PROBE,
    BMS_AFE_STARTUP_FAILURE_TRANSPORT,
    BMS_AFE_STARTUP_FAILURE_WRITE_FINALIZATION_AMBIGUOUS,
    BMS_AFE_STARTUP_FAILURE_READBACK_MISMATCH,
    BMS_AFE_STARTUP_FAILURE_CALIBRATION,
    BMS_AFE_STARTUP_FAILURE_UNSAFE_STATUS,
    BMS_AFE_STARTUP_FAILURE_SAFE_OFF_UNCONFIRMED
} BMS_AfeStartupFailure_t;

typedef enum
{
    BMS_AFE_STARTUP_RESULT_PENDING = 0,
    BMS_AFE_STARTUP_RESULT_COMPLETE,
    BMS_AFE_STARTUP_RESULT_FAILED
} BMS_AfeStartupResult_t;

/*
 * state 由 caller 持有，不用 RTOS object、dynamic allocation 或 module global，
 * 因而可在 scheduler 前运行，也可被 production-C test 独立实例化。
 */
typedef struct
{
    BQ76940_t *device;
    BMS_AfeStartupWakeFn_t wake;
    void *wake_context;
    BMS_AfeStartupConfig_t config;
    BQ76940_Calibration_t calibration;
    BMS_AfeStartupState_t state;
    BMS_AfeStartupFailure_t failure;
    BQ76940_Status_t last_transport_status;
    uint32_t wait_started_ms;
    uint8_t attempt_count;
    uint8_t register_index;
    uint8_t register_count;
    uint8_t register_addresses[BMS_AFE_STARTUP_REGISTER_COUNT];
    uint8_t register_values[BMS_AFE_STARTUP_REGISTER_COUNT];
    uint8_t adc_gain1;
    uint8_t adc_offset;
    uint8_t adc_gain2;
    uint8_t initial_sys_stat;
    uint8_t final_sys_stat;
    uint8_t unsafe_sys_stat;
    uint8_t failed_register_address;
    uint8_t failed_readback_value;
    uint8_t safe_off_readback_value;
    bool fet_off_confirmed;
    bool safe_outputs_confirmed;
    bool abort_after_safe_outputs;
    bool safe_off_recovery_attempted;
    bool xready_clear_required;
    bool xready_clear_attempted;
    bool xready_clear_completed;
} BMS_AfeStartup_t;

/*
 * 只验证并 staging startup plan，不执行 GPIO/I2C。每组 protection 都要求
 * explicit present flag，缺失 threshold 或 PROTECT3 delay 时整体拒绝，不猜默认值。
 */
bool BMS_AfeStartup_Init(BMS_AfeStartup_t *startup,
                         BQ76940_t *device,
                         const BMS_AfeStartupConfig_t *config,
                         BMS_AfeStartupWakeFn_t wake,
                         void *wake_context);

/*
 * 每次只推进一个有界 state，最多一次 wake callback 或一次 BQ transaction。
 * 10 ms WAKE settle 与 800 ms initial-data settle 都按 elapsed time 等待，绝不
 * 持有 I2C transaction。只有当前 final-status read 看到 XREADY high 才允许一次
 * W1C；早期历史观察不能授权 blind clear。clear 成功后所有旧配置证据失效，
 * 必须重跑 safe-register/calibration/protection plan 与 settle。第二次 XREADY、
 * 或 ambiguous W1C finalization 都 fail closed，且绝不 replay。
 *
 * SYS_STAT 不提供 event identity；final-read authorization + no-replay 用于缩小
 * “读后新事件”竞态窗口，并确保软件不会自行选择一个无法证明的事件身份。
 */
BMS_AfeStartupResult_t BMS_AfeStartup_Step(BMS_AfeStartup_t *startup,
                                           uint32_t now_ms);

BMS_AfeStartupState_t BMS_AfeStartup_GetState(
    const BMS_AfeStartup_t *startup);
BMS_AfeStartupFailure_t BMS_AfeStartup_GetFailure(
    const BMS_AfeStartup_t *startup);
bool BMS_AfeStartup_GetCalibration(
    const BMS_AfeStartup_t *startup,
    BQ76940_Calibration_t *calibration);
bool BMS_AfeStartup_IsFetOffConfirmed(
    const BMS_AfeStartup_t *startup);

#endif /* BMS_AFE_STARTUP_H：include guard */
