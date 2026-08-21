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

/* SYS_STAT ownership during startup is deliberately narrow. CC_READY is
 * preserved for ProtectTask. Protection-class events are never W1C here;
 * they abort startup after FET-off and all three balancing-off writes are
 * read back. XREADY is the sole startup-owned W1C; it is serviced only after
 * a settled full pass and is followed by a second complete configuration
 * pass rather than by reuse of pre-clear evidence. */
#define BMS_AFE_STARTUP_STAT_CC_READY              ((uint8_t)0x80U)
#define BMS_AFE_STARTUP_STAT_DEVICE_XREADY         ((uint8_t)0x20U)
#define BMS_AFE_STARTUP_STAT_BLOCKING_MASK         ((uint8_t)0x1FU)

/* The callback emits one board-specific PA8 -> TS1 rising edge. It must be a
 * bounded GPIO action: no delay, I2C transaction, retry loop or RTOS wait.
 * Electrical polarity and waveform remain board-validation responsibilities. */
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

    /* Required even when either exact code is zero. This prevents a
     * zero-initialized/missing PROTECT3 delay from becoming code zero. */
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

/* Caller-owned state. No RTOS object, dynamic allocation or module global is
 * used, so startup can run before the scheduler and can be independently
 * instantiated by production-C tests. */
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

/* Validate and stage startup. No GPIO or I2C action occurs here. Every
 * protection group needs an explicit present flag; no default threshold or
 * PROTECT3 delay is guessed. */
bool BMS_AfeStartup_Init(BMS_AfeStartup_t *startup,
                         BQ76940_t *device,
                         const BMS_AfeStartupConfig_t *config,
                         BMS_AfeStartupWakeFn_t wake,
                         void *wake_context);

/* Advance one bounded state. A call performs at most one wake callback or one
 * BQ register transaction. The 10 ms wake settle and 800 ms initial-data
 * settle are elapsed-time states and never hold an I2C transaction open.
 * After settling, XREADY is W1C once only when the current final-status read
 * observes it high; an initial historical observation never authorizes a
 * blind clear. A successful clear invalidates all configuration evidence, so
 * the complete safe-register/calibration/protection plan and 800 ms settle
 * run again before a final status read may complete startup. A second XREADY
 * fails closed and is never W1C again. Ambiguous W1C finalization is terminal.
 *
 * SYS_STAT provides no event identity. Therefore hardware cannot distinguish
 * a new XREADY that asserts between the authorizing read and its one W1C; the
 * final-only authorization and no-replay policy minimize but cannot remove
 * that physical interface limitation. */
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

#endif /* BMS_AFE_STARTUP_H */
