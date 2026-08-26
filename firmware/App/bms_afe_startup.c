#include "bms_afe_startup.h"

/*
 * AFE startup 是 pre-scheduler、caller-owned 的有界状态机：power-up/WAKE、
 * communication probe、safe FET/CELLBAL、calibration、protection configuration、
 * settle、status verify 逐步执行。每个 register write 都有 readback；任何状态
 * 失败都保持 fail-closed，且不会把上一个 register epoch 的证据带入下一 epoch。
 */

#include <stddef.h>
#include <string.h>

#include "bms_config.h"
#include "bq76940_control.h"
#include "bq76940_regs.h"

enum
{
    BMS_AFE_STARTUP_REG_FAILSAFE_CTRL2 = 0,
    BMS_AFE_STARTUP_REG_CELLBAL1,
    BMS_AFE_STARTUP_REG_CELLBAL2,
    BMS_AFE_STARTUP_REG_CELLBAL3,
    BMS_AFE_STARTUP_REG_CC_CFG,
    BMS_AFE_STARTUP_REG_OV_TRIP,
    BMS_AFE_STARTUP_REG_UV_TRIP,
    BMS_AFE_STARTUP_REG_PROTECT3,
    BMS_AFE_STARTUP_REG_PROTECT1,
    BMS_AFE_STARTUP_REG_PROTECT2,
    BMS_AFE_STARTUP_REG_SYS_CTRL1,
    BMS_AFE_STARTUP_REG_FINAL_CTRL2,
    BMS_AFE_STARTUP_EARLY_REGISTER_COUNT = 5
};

BMS_BUILD_ASSERT((BMS_AFE_STARTUP_REG_FINAL_CTRL2 + 1) ==
                     BMS_AFE_STARTUP_REGISTER_COUNT,
                 afe_startup_register_plan_matches_public_count);

static void BMS_AfeStartup_Fail(BMS_AfeStartup_t *startup,
                                BMS_AfeStartupFailure_t failure,
                                BQ76940_Status_t transport_status)
{
    startup->failure = failure;
    startup->last_transport_status = transport_status;
    startup->state = BMS_AFE_STARTUP_STATE_FAILED;
}

static bool BMS_AfeStartup_TimeElapsed(uint32_t now_ms,
                                       uint32_t start_ms,
                                       uint32_t duration_ms)
{
    return ((uint32_t)(now_ms - start_ms) >= duration_ms);
}

static bool BMS_AfeStartup_HasBlockingStatus(uint8_t sys_stat)
{
    return ((sys_stat & BMS_AFE_STARTUP_STAT_BLOCKING_MASK) != 0U);
}

static bool BMS_AfeStartup_HasFetsOff(uint8_t sys_ctrl2)
{
    return ((sys_ctrl2 & 0x03U) == 0U);
}

static bool BMS_AfeStartup_ValidateConfig(
    const BMS_AfeStartupConfig_t *config,
    uint8_t *protect1,
    uint8_t *protect2,
    uint8_t *protect3)
{
    BQ76940_Status_t status;

    if ((config == NULL) || (protect1 == NULL) ||
        (protect2 == NULL) || (protect3 == NULL))
    {
        return false;
    }
    if (!config->ov_uv_trip_present || !config->protect1_present ||
        !config->protect2_present || !config->protect3_present)
    {
        return false;
    }
    if ((config->uv_trip_mv == 0U) || (config->ov_trip_mv == 0U) ||
        (config->uv_trip_mv >= config->ov_trip_mv))
    {
        return false;
    }

    status = BQ76940_Control_ComposeProtect1(
        config->protect1_rsns,
        config->protect1_scd_delay_code,
        config->protect1_scd_threshold_code,
        protect1);
    if (status != BQ76940_STATUS_OK)
    {
        return false;
    }
    status = BQ76940_Control_ComposeProtect2(
        config->protect2_ocd_delay_code,
        config->protect2_ocd_threshold_code,
        protect2);
    if (status != BQ76940_STATUS_OK)
    {
        return false;
    }
    status = BQ76940_Control_ComposeProtect3(
        config->protect3_uv_delay_code,
        config->protect3_ov_delay_code,
        protect3);
    return (status == BQ76940_STATUS_OK);
}

static void BMS_AfeStartup_StageEarlyRegisters(BMS_AfeStartup_t *startup)
{
    startup->register_addresses[BMS_AFE_STARTUP_REG_FAILSAFE_CTRL2] =
        BQ76940_REG_SYS_CTRL2;
    startup->register_values[BMS_AFE_STARTUP_REG_FAILSAFE_CTRL2] =
        BMS_AFE_STARTUP_SYS_CTRL2_FET_OFF;

    startup->register_addresses[BMS_AFE_STARTUP_REG_CELLBAL1] =
        BQ76940_REG_CELLBAL1;
    startup->register_values[BMS_AFE_STARTUP_REG_CELLBAL1] = 0U;
    startup->register_addresses[BMS_AFE_STARTUP_REG_CELLBAL2] =
        BQ76940_REG_CELLBAL2;
    startup->register_values[BMS_AFE_STARTUP_REG_CELLBAL2] = 0U;
    startup->register_addresses[BMS_AFE_STARTUP_REG_CELLBAL3] =
        BQ76940_REG_CELLBAL3;
    startup->register_values[BMS_AFE_STARTUP_REG_CELLBAL3] = 0U;

    startup->register_addresses[BMS_AFE_STARTUP_REG_CC_CFG] =
        BQ76940_REG_CC_CFG;
    startup->register_values[BMS_AFE_STARTUP_REG_CC_CFG] =
        BQ76940_CC_CFG_REQUIRED_VALUE;
    startup->register_count = BMS_AFE_STARTUP_EARLY_REGISTER_COUNT;
}

static bool BMS_AfeStartup_StageProtectionRegisters(
    BMS_AfeStartup_t *startup)
{
    BQ76940_Status_t status;
    uint8_t ov_trip;
    uint8_t uv_trip;

    status = BQ76940_Control_EncodeOvTrip(
        startup->config.ov_trip_mv, &startup->calibration, &ov_trip);
    if (status != BQ76940_STATUS_OK)
    {
        BMS_AfeStartup_Fail(startup,
                            BMS_AFE_STARTUP_FAILURE_INVALID_CONFIG,
                            status);
        return false;
    }
    status = BQ76940_Control_EncodeUvTrip(
        startup->config.uv_trip_mv, &startup->calibration, &uv_trip);
    if (status != BQ76940_STATUS_OK)
    {
        BMS_AfeStartup_Fail(startup,
                            BMS_AFE_STARTUP_FAILURE_INVALID_CONFIG,
                            status);
        return false;
    }

    startup->register_addresses[BMS_AFE_STARTUP_REG_OV_TRIP] =
        BQ76940_REG_OV_TRIP;
    startup->register_values[BMS_AFE_STARTUP_REG_OV_TRIP] = ov_trip;
    startup->register_addresses[BMS_AFE_STARTUP_REG_UV_TRIP] =
        BQ76940_REG_UV_TRIP;
    startup->register_values[BMS_AFE_STARTUP_REG_UV_TRIP] = uv_trip;

    startup->register_addresses[BMS_AFE_STARTUP_REG_PROTECT3] =
        BQ76940_REG_PROTECT3;
    startup->register_addresses[BMS_AFE_STARTUP_REG_PROTECT1] =
        BQ76940_REG_PROTECT1;
    startup->register_addresses[BMS_AFE_STARTUP_REG_PROTECT2] =
        BQ76940_REG_PROTECT2;

    startup->register_addresses[BMS_AFE_STARTUP_REG_SYS_CTRL1] =
        BQ76940_REG_SYS_CTRL1;
    startup->register_values[BMS_AFE_STARTUP_REG_SYS_CTRL1] =
        BMS_AFE_STARTUP_SYS_CTRL1_REQUIRED;
    startup->register_addresses[BMS_AFE_STARTUP_REG_FINAL_CTRL2] =
        BQ76940_REG_SYS_CTRL2;
    startup->register_values[BMS_AFE_STARTUP_REG_FINAL_CTRL2] =
        BQ76940_Control_SysCtrl2WithFets(
            BMS_AFE_STARTUP_SYS_CTRL2_CC_FET_OFF, NULL);
    startup->register_count = BMS_AFE_STARTUP_REGISTER_COUNT;
    return true;
}

bool BMS_AfeStartup_Init(BMS_AfeStartup_t *startup,
                         BQ76940_t *device,
                         const BMS_AfeStartupConfig_t *config,
                         BMS_AfeStartupWakeFn_t wake,
                         void *wake_context)
{
    uint8_t protect1;
    uint8_t protect2;
    uint8_t protect3;

    if (startup == NULL)
    {
        return false;
    }

    (void)memset(startup, 0, sizeof(*startup));
    startup->state = BMS_AFE_STARTUP_STATE_FAILED;
    startup->failure = BMS_AFE_STARTUP_FAILURE_INVALID_ARGUMENT;
    startup->last_transport_status = BQ76940_STATUS_INVALID_ARGUMENT;
    startup->failed_register_address = 0xFFU;

    if ((device == NULL) || (config == NULL) || (wake == NULL))
    {
        return false;
    }
    if (!BQ76940_IsInitialized(device))
    {
        startup->failure = BMS_AFE_STARTUP_FAILURE_DEVICE_NOT_READY;
        startup->last_transport_status = BQ76940_STATUS_NOT_INITIALIZED;
        return false;
    }
    if (!BMS_AfeStartup_ValidateConfig(config,
                                        &protect1,
                                        &protect2,
                                        &protect3))
    {
        startup->failure = BMS_AFE_STARTUP_FAILURE_INVALID_CONFIG;
        startup->last_transport_status = BQ76940_STATUS_RANGE_ERROR;
        return false;
    }

    startup->device = device;
    startup->wake = wake;
    startup->wake_context = wake_context;
    startup->config = *config;
    startup->calibration.valid = false;
    startup->register_values[BMS_AFE_STARTUP_REG_PROTECT1] = protect1;
    startup->register_values[BMS_AFE_STARTUP_REG_PROTECT2] = protect2;
    startup->register_values[BMS_AFE_STARTUP_REG_PROTECT3] = protect3;
    BMS_AfeStartup_StageEarlyRegisters(startup);
    startup->state = BMS_AFE_STARTUP_STATE_WAKE;
    startup->failure = BMS_AFE_STARTUP_FAILURE_NONE;
    startup->last_transport_status = BQ76940_STATUS_OK;
    return true;
}

static BMS_AfeStartupResult_t BMS_AfeStartup_WriteRegister(
    BMS_AfeStartup_t *startup)
{
    BQ76940_Status_t status;
    uint8_t address;
    uint8_t value;

    address = startup->register_addresses[startup->register_index];
    value = startup->register_values[startup->register_index];
    if (address == BQ76940_REG_SYS_CTRL2)
    {
        /* 上一次 readback 只描述上一 register epoch，新 write 后必须重新证明。 */
        startup->fet_off_confirmed = false;
        if (startup->register_index ==
            BMS_AFE_STARTUP_REG_FINAL_CTRL2)
        {
            startup->safe_outputs_confirmed = false;
        }
    }
    startup->failed_register_address = address;
    status = BQ76940_WriteByte(startup->device, address, value);
    startup->last_transport_status = status;
    if (status == BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS)
    {
        BMS_AfeStartup_Fail(
            startup,
            BMS_AFE_STARTUP_FAILURE_WRITE_FINALIZATION_AMBIGUOUS,
            status);
        return BMS_AFE_STARTUP_RESULT_FAILED;
    }
    if (status != BQ76940_STATUS_OK)
    {
        BMS_AfeStartup_Fail(startup,
                            BMS_AFE_STARTUP_FAILURE_TRANSPORT,
                            status);
        return BMS_AFE_STARTUP_RESULT_FAILED;
    }
    startup->state = BMS_AFE_STARTUP_STATE_VERIFY_REGISTER;
    return BMS_AFE_STARTUP_RESULT_PENDING;
}

static BMS_AfeStartupResult_t BMS_AfeStartup_VerifyRegister(
    BMS_AfeStartup_t *startup,
    uint32_t now_ms)
{
    BQ76940_Status_t status;
    uint8_t address;
    uint8_t expected;
    uint8_t actual;
    bool is_sys_ctrl2;

    address = startup->register_addresses[startup->register_index];
    expected = startup->register_values[startup->register_index];
    is_sys_ctrl2 = (address == BQ76940_REG_SYS_CTRL2);
    actual = 0U;
    startup->failed_register_address = address;
    status = BQ76940_ReadByte(startup->device, address, &actual);
    startup->last_transport_status = status;
    if (status != BQ76940_STATUS_OK)
    {
        if (is_sys_ctrl2)
        {
            startup->fet_off_confirmed = false;
        }
        BMS_AfeStartup_Fail(startup,
                            BMS_AFE_STARTUP_FAILURE_TRANSPORT,
                            status);
        return BMS_AFE_STARTUP_RESULT_FAILED;
    }
    if (is_sys_ctrl2)
    {
        /* 该标志只跟随最新 SYS_CTRL2 实读 FET 位，不受其他 bit 是否匹配影响。 */
        startup->fet_off_confirmed = BMS_AfeStartup_HasFetsOff(actual);
        if (startup->register_index ==
            BMS_AFE_STARTUP_REG_FINAL_CTRL2)
        {
            startup->safe_outputs_confirmed =
                startup->fet_off_confirmed;
        }
    }
    if (actual != expected)
    {
        startup->failed_readback_value = actual;
        if ((startup->register_index ==
             BMS_AFE_STARTUP_REG_FINAL_CTRL2) &&
            !startup->fet_off_confirmed &&
            !startup->safe_off_recovery_attempted)
        {
            /* 只有具体 read 证明任一 FET 高后，才允许一次有界且幂等的紧急纠正。 */
            startup->safe_off_recovery_attempted = true;
            startup->state = BMS_AFE_STARTUP_STATE_SAFE_OFF_WRITE;
            return BMS_AFE_STARTUP_RESULT_PENDING;
        }
        BMS_AfeStartup_Fail(startup,
                            BMS_AFE_STARTUP_FAILURE_READBACK_MISMATCH,
                            BQ76940_STATUS_OK);
        return BMS_AFE_STARTUP_RESULT_FAILED;
    }

    if (startup->register_index == BMS_AFE_STARTUP_REG_CELLBAL3)
    {
        startup->safe_outputs_confirmed = true;
        if (startup->abort_after_safe_outputs)
        {
            BMS_AfeStartup_Fail(startup,
                                BMS_AFE_STARTUP_FAILURE_UNSAFE_STATUS,
                                BQ76940_STATUS_OK);
            return BMS_AFE_STARTUP_RESULT_FAILED;
        }
    }
    ++startup->register_index;
    if (startup->register_index < startup->register_count)
    {
        startup->state = BMS_AFE_STARTUP_STATE_WRITE_REGISTER;
    }
    else if (startup->register_count ==
             BMS_AFE_STARTUP_EARLY_REGISTER_COUNT)
    {
        startup->state = BMS_AFE_STARTUP_STATE_READ_ADCGAIN1;
    }
    else
    {
        startup->wait_started_ms = now_ms;
        startup->state = BMS_AFE_STARTUP_STATE_WAIT_INITIAL_DATA;
    }
    return BMS_AFE_STARTUP_RESULT_PENDING;
}

static BMS_AfeStartupResult_t BMS_AfeStartup_ReadCalibrationRegister(
    BMS_AfeStartup_t *startup,
    uint8_t register_address,
    uint8_t *value,
    BMS_AfeStartupState_t next_state)
{
    BQ76940_Status_t status;

    status = BQ76940_ReadByte(startup->device, register_address, value);
    startup->last_transport_status = status;
    if (status != BQ76940_STATUS_OK)
    {
        BMS_AfeStartup_Fail(startup,
                            BMS_AFE_STARTUP_FAILURE_TRANSPORT,
                            status);
        return BMS_AFE_STARTUP_RESULT_FAILED;
    }
    startup->state = next_state;
    return BMS_AFE_STARTUP_RESULT_PENDING;
}

static BMS_AfeStartupResult_t BMS_AfeStartup_ReadFinalStatus(
    BMS_AfeStartup_t *startup)
{
    BQ76940_Status_t status;

    status = BQ76940_ReadByte(startup->device,
                              BQ76940_REG_SYS_STAT,
                              &startup->final_sys_stat);
    startup->last_transport_status = status;
    if (status != BQ76940_STATUS_OK)
    {
        BMS_AfeStartup_Fail(startup,
                            BMS_AFE_STARTUP_FAILURE_TRANSPORT,
                            status);
        return BMS_AFE_STARTUP_RESULT_FAILED;
    }
    if (BMS_AfeStartup_HasBlockingStatus(startup->final_sys_stat))
    {
        startup->unsafe_sys_stat = startup->final_sys_stat;
        BMS_AfeStartup_Fail(startup,
                            BMS_AFE_STARTUP_FAILURE_UNSAFE_STATUS,
                            BQ76940_STATUS_OK);
        return BMS_AFE_STARTUP_RESULT_FAILED;
    }

    /*
     * final read 是本次 W1C 的 authorization identity。只在 probe 时出现的旧
     * XREADY 已退休；blind clear 可能误消费 final read 之后的新事件。
     */
    startup->xready_clear_required =
        ((startup->final_sys_stat &
          BMS_AFE_STARTUP_STAT_DEVICE_XREADY) != 0U);
    if (startup->xready_clear_required)
    {
        if (startup->xready_clear_attempted ||
            startup->xready_clear_completed)
        {
            startup->unsafe_sys_stat = startup->final_sys_stat;
            BMS_AfeStartup_Fail(startup,
                                BMS_AFE_STARTUP_FAILURE_UNSAFE_STATUS,
                                BQ76940_STATUS_OK);
            return BMS_AFE_STARTUP_RESULT_FAILED;
        }
        startup->state = BMS_AFE_STARTUP_STATE_CLEAR_XREADY;
        return BMS_AFE_STARTUP_RESULT_PENDING;
    }

    startup->state = BMS_AFE_STARTUP_STATE_COMPLETE;
    return BMS_AFE_STARTUP_RESULT_COMPLETE;
}

static BMS_AfeStartupResult_t BMS_AfeStartup_ClearXready(
    BMS_AfeStartup_t *startup)
{
    BQ76940_Status_t status;

    if (startup->xready_clear_attempted)
    {
        startup->unsafe_sys_stat = startup->final_sys_stat;
        BMS_AfeStartup_Fail(startup,
                            BMS_AFE_STARTUP_FAILURE_UNSAFE_STATUS,
                            BQ76940_STATUS_OK);
        return BMS_AFE_STARTUP_RESULT_FAILED;
    }
    startup->xready_clear_attempted = true;
    /*
     * STOP finalization ambiguous 时 W1C 仍可能已提交；发出前先使所有 evidence
     * 失效，确保任一 terminal path 都不会暴露 prior epoch 的 FET/CELLBAL/calibration。
     */
    startup->fet_off_confirmed = false;
    startup->safe_outputs_confirmed = false;
    startup->calibration.valid = false;
    startup->failed_register_address = BQ76940_REG_SYS_STAT;
    status = BQ76940_WriteByte(startup->device,
                               BQ76940_REG_SYS_STAT,
                               BMS_AFE_STARTUP_STAT_DEVICE_XREADY);
    startup->last_transport_status = status;
    if (status == BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS)
    {
        BMS_AfeStartup_Fail(
            startup,
            BMS_AFE_STARTUP_FAILURE_WRITE_FINALIZATION_AMBIGUOUS,
            status);
        return BMS_AFE_STARTUP_RESULT_FAILED;
    }
    if (status != BQ76940_STATUS_OK)
    {
        BMS_AfeStartup_Fail(startup,
                            BMS_AFE_STARTUP_FAILURE_TRANSPORT,
                            status);
        return BMS_AFE_STARTUP_RESULT_FAILED;
    }

    /* XREADY 划分 reset/config epoch；W1C 成功也不能授权复用 pre-clear 证据。 */
    startup->xready_clear_completed = true;
    startup->safe_off_recovery_attempted = false;
    BMS_AfeStartup_StageEarlyRegisters(startup);
    startup->register_index = 0U;
    startup->state = BMS_AFE_STARTUP_STATE_WRITE_REGISTER;
    return BMS_AFE_STARTUP_RESULT_PENDING;
}

static BMS_AfeStartupResult_t BMS_AfeStartup_SafeOffWrite(
    BMS_AfeStartup_t *startup)
{
    BQ76940_Status_t status;

    startup->fet_off_confirmed = false;
    startup->safe_outputs_confirmed = false;
    startup->failed_register_address = BQ76940_REG_SYS_CTRL2;
    status = BQ76940_WriteByte(
        startup->device,
        BQ76940_REG_SYS_CTRL2,
        BMS_AFE_STARTUP_SYS_CTRL2_CC_FET_OFF);
    startup->last_transport_status = status;
    if (status == BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS)
    {
        BMS_AfeStartup_Fail(
            startup,
            BMS_AFE_STARTUP_FAILURE_WRITE_FINALIZATION_AMBIGUOUS,
            status);
        return BMS_AFE_STARTUP_RESULT_FAILED;
    }
    if (status != BQ76940_STATUS_OK)
    {
        BMS_AfeStartup_Fail(startup,
                            BMS_AFE_STARTUP_FAILURE_TRANSPORT,
                            status);
        return BMS_AFE_STARTUP_RESULT_FAILED;
    }

    startup->state = BMS_AFE_STARTUP_STATE_SAFE_OFF_VERIFY;
    return BMS_AFE_STARTUP_RESULT_PENDING;
}

static BMS_AfeStartupResult_t BMS_AfeStartup_SafeOffVerify(
    BMS_AfeStartup_t *startup,
    uint32_t now_ms)
{
    BQ76940_Status_t status;
    uint8_t actual;

    actual = 0U;
    status = BQ76940_ReadByte(startup->device,
                              BQ76940_REG_SYS_CTRL2,
                              &actual);
    startup->last_transport_status = status;
    if (status != BQ76940_STATUS_OK)
    {
        startup->fet_off_confirmed = false;
        BMS_AfeStartup_Fail(startup,
                            BMS_AFE_STARTUP_FAILURE_TRANSPORT,
                            status);
        return BMS_AFE_STARTUP_RESULT_FAILED;
    }
    startup->safe_off_readback_value = actual;
    startup->fet_off_confirmed = BMS_AfeStartup_HasFetsOff(actual);
    startup->safe_outputs_confirmed = startup->fet_off_confirmed;
    if (actual != BMS_AFE_STARTUP_SYS_CTRL2_CC_FET_OFF)
    {
        BMS_AfeStartup_Fail(
            startup,
            BMS_AFE_STARTUP_FAILURE_SAFE_OFF_UNCONFIRMED,
            BQ76940_STATUS_OK);
        return BMS_AFE_STARTUP_RESULT_FAILED;
    }

    startup->wait_started_ms = now_ms;
    startup->state = BMS_AFE_STARTUP_STATE_WAIT_INITIAL_DATA;
    return BMS_AFE_STARTUP_RESULT_PENDING;
}

BMS_AfeStartupResult_t BMS_AfeStartup_Step(BMS_AfeStartup_t *startup,
                                           uint32_t now_ms)
{
    BQ76940_Status_t status;

    if (startup == NULL)
    {
        return BMS_AFE_STARTUP_RESULT_FAILED;
    }

    switch (startup->state)
    {
        case BMS_AFE_STARTUP_STATE_WAKE:
            if (startup->attempt_count >=
                BMS_AFE_STARTUP_MAX_PROBE_ATTEMPTS)
            {
                BMS_AfeStartup_Fail(startup,
                                    BMS_AFE_STARTUP_FAILURE_WAKE,
                                    BQ76940_STATUS_I2C_TIMEOUT);
                return BMS_AFE_STARTUP_RESULT_FAILED;
            }
            ++startup->attempt_count;
            if (!startup->wake(startup->wake_context))
            {
                if (startup->attempt_count >=
                    BMS_AFE_STARTUP_MAX_PROBE_ATTEMPTS)
                {
                    BMS_AfeStartup_Fail(startup,
                                        BMS_AFE_STARTUP_FAILURE_WAKE,
                                        BQ76940_STATUS_I2C_TIMEOUT);
                    return BMS_AFE_STARTUP_RESULT_FAILED;
                }
                return BMS_AFE_STARTUP_RESULT_PENDING;
            }
            startup->wait_started_ms = now_ms;
            startup->state = BMS_AFE_STARTUP_STATE_WAIT_WAKE_SETTLE;
            return BMS_AFE_STARTUP_RESULT_PENDING;

        case BMS_AFE_STARTUP_STATE_WAIT_WAKE_SETTLE:
            if (BMS_AfeStartup_TimeElapsed(
                    now_ms,
                    startup->wait_started_ms,
                    (uint32_t)BMS_AFE_WAKE_SETTLE_MS))
            {
                startup->state = BMS_AFE_STARTUP_STATE_PROBE;
            }
            return BMS_AFE_STARTUP_RESULT_PENDING;

        case BMS_AFE_STARTUP_STATE_PROBE:
            status = BQ76940_ReadByte(startup->device,
                                      BQ76940_REG_SYS_STAT,
                                      &startup->initial_sys_stat);
            startup->last_transport_status = status;
            if (status != BQ76940_STATUS_OK)
            {
                if (startup->attempt_count <
                    BMS_AFE_STARTUP_MAX_PROBE_ATTEMPTS)
                {
                    startup->state = BMS_AFE_STARTUP_STATE_WAKE;
                    return BMS_AFE_STARTUP_RESULT_PENDING;
                }
                BMS_AfeStartup_Fail(startup,
                                    BMS_AFE_STARTUP_FAILURE_PROBE,
                                    status);
                return BMS_AFE_STARTUP_RESULT_FAILED;
            }
            if (BMS_AfeStartup_HasBlockingStatus(
                    startup->initial_sys_stat))
            {
                /*
                 * 保留 hardware event 给 Protect/recovery owner，但在发布 terminal
                 * unsafe result 前仍回读确认两路 FET low 与 CELLBAL1..3 全零。
                 */
                startup->unsafe_sys_stat = startup->initial_sys_stat;
                startup->abort_after_safe_outputs = true;
            }
            startup->register_index = 0U;
            startup->state = BMS_AFE_STARTUP_STATE_WRITE_REGISTER;
            return BMS_AFE_STARTUP_RESULT_PENDING;

        case BMS_AFE_STARTUP_STATE_WRITE_REGISTER:
            return BMS_AfeStartup_WriteRegister(startup);

        case BMS_AFE_STARTUP_STATE_VERIFY_REGISTER:
            return BMS_AfeStartup_VerifyRegister(startup, now_ms);

        case BMS_AFE_STARTUP_STATE_READ_ADCGAIN1:
            return BMS_AfeStartup_ReadCalibrationRegister(
                startup, BQ76940_REG_ADCGAIN1, &startup->adc_gain1,
                BMS_AFE_STARTUP_STATE_READ_ADCOFFSET);

        case BMS_AFE_STARTUP_STATE_READ_ADCOFFSET:
            return BMS_AfeStartup_ReadCalibrationRegister(
                startup, BQ76940_REG_ADCOFFSET, &startup->adc_offset,
                BMS_AFE_STARTUP_STATE_READ_ADCGAIN2);

        case BMS_AFE_STARTUP_STATE_READ_ADCGAIN2:
            return BMS_AfeStartup_ReadCalibrationRegister(
                startup, BQ76940_REG_ADCGAIN2, &startup->adc_gain2,
                BMS_AFE_STARTUP_STATE_PREPARE_PROTECTION);

        case BMS_AFE_STARTUP_STATE_PREPARE_PROTECTION:
            status = BQ76940_DecodeCalibration(startup->adc_gain1,
                                                startup->adc_offset,
                                                startup->adc_gain2,
                                                &startup->calibration);
            startup->last_transport_status = status;
            if (status != BQ76940_STATUS_OK)
            {
                BMS_AfeStartup_Fail(startup,
                                    BMS_AFE_STARTUP_FAILURE_CALIBRATION,
                                    status);
                return BMS_AFE_STARTUP_RESULT_FAILED;
            }
            if (!BMS_AfeStartup_StageProtectionRegisters(startup))
            {
                return BMS_AFE_STARTUP_RESULT_FAILED;
            }
            startup->register_index = BMS_AFE_STARTUP_REG_OV_TRIP;
            startup->state = BMS_AFE_STARTUP_STATE_WRITE_REGISTER;
            return BMS_AFE_STARTUP_RESULT_PENDING;

        case BMS_AFE_STARTUP_STATE_WAIT_INITIAL_DATA:
            if (BMS_AfeStartup_TimeElapsed(
                    now_ms,
                    startup->wait_started_ms,
                    (uint32_t)BMS_AFE_INITIAL_DATA_SETTLE_MS))
            {
                startup->state = BMS_AFE_STARTUP_STATE_READ_FINAL_STATUS;
            }
            return BMS_AFE_STARTUP_RESULT_PENDING;

        case BMS_AFE_STARTUP_STATE_READ_FINAL_STATUS:
            return BMS_AfeStartup_ReadFinalStatus(startup);

        case BMS_AFE_STARTUP_STATE_CLEAR_XREADY:
            return BMS_AfeStartup_ClearXready(startup);

        case BMS_AFE_STARTUP_STATE_SAFE_OFF_WRITE:
            return BMS_AfeStartup_SafeOffWrite(startup);

        case BMS_AFE_STARTUP_STATE_SAFE_OFF_VERIFY:
            return BMS_AfeStartup_SafeOffVerify(startup, now_ms);

        case BMS_AFE_STARTUP_STATE_COMPLETE:
            return BMS_AFE_STARTUP_RESULT_COMPLETE;

        case BMS_AFE_STARTUP_STATE_UNINITIALIZED:
        case BMS_AFE_STARTUP_STATE_FAILED:
        default:
            return BMS_AFE_STARTUP_RESULT_FAILED;
    }
}

BMS_AfeStartupState_t BMS_AfeStartup_GetState(
    const BMS_AfeStartup_t *startup)
{
    if (startup == NULL)
    {
        return BMS_AFE_STARTUP_STATE_FAILED;
    }
    return startup->state;
}

BMS_AfeStartupFailure_t BMS_AfeStartup_GetFailure(
    const BMS_AfeStartup_t *startup)
{
    if (startup == NULL)
    {
        return BMS_AFE_STARTUP_FAILURE_INVALID_ARGUMENT;
    }
    return startup->failure;
}

bool BMS_AfeStartup_GetCalibration(
    const BMS_AfeStartup_t *startup,
    BQ76940_Calibration_t *calibration)
{
    if ((startup == NULL) || (calibration == NULL) ||
        (startup->state != BMS_AFE_STARTUP_STATE_COMPLETE) ||
        !startup->calibration.valid)
    {
        return false;
    }
    *calibration = startup->calibration;
    return true;
}

bool BMS_AfeStartup_IsFetOffConfirmed(
    const BMS_AfeStartup_t *startup)
{
    return (startup != NULL) && startup->fet_off_confirmed;
}
