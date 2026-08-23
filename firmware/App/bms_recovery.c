#include "bms_recovery.h"

#include <stddef.h>

#include "app_rtos.h"
#include "bms_afe_startup.h"
#include "bms_data.h"
#include "bms_protect.h"
#include "bms_sample.h"
#include "bq76940_control.h"
#include "bq76940_regs.h"

#define BMS_RECOVERY_CONFIG_REGISTER_COUNT      (12U)
#define BMS_RECOVERY_SETTLE_MS                  (800UL)
#define BMS_RECOVERY_I2C_TIMEOUT_MS             (20U)

typedef struct
{
    BQ76940_t *device;
    const BMS_Policy_t *policy;
    BMS_RecoverySnapshot_t snapshot;
    uint8_t register_addresses[BMS_RECOVERY_CONFIG_REGISTER_COUNT];
    uint8_t register_values[BMS_RECOVERY_CONFIG_REGISTER_COUNT];
    uint8_t config_index;
    uint8_t calibration_index;
    uint8_t adc_gain1;
    uint8_t adc_offset;
    uint8_t adc_gain2;
    uint32_t settle_started_ms;
    uint32_t handoff_baseline_sequence;
    bool verify_register;
} BMS_RecoveryContext_t;

static BMS_RecoveryContext_t s_recovery;

#if defined(TEST_PHASE9_IMAGE)
static BMS_RecoveryTestHook_t s_pre_handoff_hook;
#endif

static bool BMS_Recovery_TimeElapsed(uint32_t now_ms,
                                     uint32_t started_ms,
                                     uint32_t duration_ms)
{
    return ((uint32_t)(now_ms - started_ms) >= duration_ms);
}

static void BMS_Recovery_PublishChange(void)
{
    s_recovery.snapshot.publication_revision =
        (uint32_t)(s_recovery.snapshot.publication_revision + 1UL);
}

static void BMS_Recovery_SetPhase(BMS_RecoveryPhase_t phase)
{
    if (s_recovery.snapshot.phase != phase)
    {
        s_recovery.snapshot.phase = phase;
        BMS_Recovery_PublishChange();
    }
}

static void BMS_Recovery_Fail(BQ76940_Status_t status)
{
    s_recovery.snapshot.last_transport_status = status;
    s_recovery.snapshot.recovery_in_progress = true;
    s_recovery.snapshot.technical_ready = false;
    BMS_Recovery_SetPhase(BMS_RECOVERY_PHASE_FAILED);
}

static void BMS_Recovery_ResetEvidence(uint32_t generation)
{
    s_recovery.snapshot.xready_generation = generation;
    s_recovery.snapshot.recovery_revision =
        (uint32_t)(s_recovery.snapshot.recovery_revision + 1UL);
    s_recovery.snapshot.first_valid_sample_sequence = 0UL;
    s_recovery.snapshot.first_valid_afe_generation = generation;
    s_recovery.snapshot.inhibit_chg_reasons = BMS_INHIBIT_REASON_RECOVERY;
    s_recovery.snapshot.inhibit_dsg_reasons = BMS_INHIBIT_REASON_RECOVERY;
    s_recovery.snapshot.calibration.gain_uv_per_lsb = 0U;
    s_recovery.snapshot.calibration.offset_mv = 0;
    s_recovery.snapshot.calibration.valid = false;
    s_recovery.snapshot.last_transport_status = BQ76940_STATUS_OK;
    s_recovery.snapshot.recovery_in_progress = true;
    s_recovery.snapshot.post_clear_verified = false;
    s_recovery.snapshot.calibration_handed_off = false;
    s_recovery.snapshot.first_valid_sample_accepted = false;
    s_recovery.snapshot.technical_ready = false;
    s_recovery.config_index = 0U;
    s_recovery.calibration_index = 0U;
    s_recovery.verify_register = false;
    s_recovery.handoff_baseline_sequence = 0UL;
    BMS_Sample_InvalidateCalibrationForXready(generation);
    BMS_Recovery_SetPhase(BMS_RECOVERY_PHASE_PRE_CLEAR_PREPARE);
    BMS_Recovery_PublishChange();
}

static bool BMS_Recovery_GenerationStillCurrent(void)
{
    BMS_ProtectXreadyState_t state;

    return BMS_Protect_GetXreadyState(&state) &&
        (state.xready_generation ==
         s_recovery.snapshot.xready_generation);
}

static BQ76940_Status_t BMS_Recovery_OneRead(uint8_t address,
                                             uint8_t *value)
{
    BQ76940_Status_t status;

    if ((s_recovery.device == NULL) || (value == NULL) ||
        (xI2CMutex == NULL))
    {
        return BQ76940_STATUS_NOT_INITIALIZED;
    }
    if (xSemaphoreTake(xI2CMutex,
                       pdMS_TO_TICKS(BMS_RECOVERY_I2C_TIMEOUT_MS)) != pdTRUE)
    {
        return BQ76940_STATUS_I2C_TIMEOUT;
    }
    status = BQ76940_ReadByte(s_recovery.device, address, value);
    (void)xSemaphoreGive(xI2CMutex);
    return status;
}

static BQ76940_Status_t BMS_Recovery_OneWrite(uint8_t address,
                                              uint8_t value)
{
    BQ76940_Status_t status;

    if ((s_recovery.device == NULL) || (xI2CMutex == NULL))
    {
        return BQ76940_STATUS_NOT_INITIALIZED;
    }
    if (xSemaphoreTake(xI2CMutex,
                       pdMS_TO_TICKS(BMS_RECOVERY_I2C_TIMEOUT_MS)) != pdTRUE)
    {
        return BQ76940_STATUS_I2C_TIMEOUT;
    }
    status = BQ76940_WriteByte(s_recovery.device, address, value);
    (void)xSemaphoreGive(xI2CMutex);
    return status;
}

static bool BMS_Recovery_StageRegisterPlan(void)
{
    const BMS_AfeStartupConfig_t *config;
    BQ76940_Status_t status;
    uint8_t protect1;
    uint8_t protect2;
    uint8_t protect3;
    uint8_t ov_trip;
    uint8_t uv_trip;

    config = &s_recovery.policy->afe_startup;
    status = BQ76940_Control_ComposeProtect1(
        config->protect1_rsns,
        config->protect1_scd_delay_code,
        config->protect1_scd_threshold_code, &protect1);
    if (status != BQ76940_STATUS_OK)
    {
        return false;
    }
    status = BQ76940_Control_ComposeProtect2(
        config->protect2_ocd_delay_code,
        config->protect2_ocd_threshold_code, &protect2);
    if (status != BQ76940_STATUS_OK)
    {
        return false;
    }
    status = BQ76940_Control_ComposeProtect3(
        config->protect3_uv_delay_code,
        config->protect3_ov_delay_code, &protect3);
    if (status != BQ76940_STATUS_OK)
    {
        return false;
    }
    status = BQ76940_Control_EncodeOvTrip(
        config->ov_trip_mv, &s_recovery.snapshot.calibration, &ov_trip);
    if (status != BQ76940_STATUS_OK)
    {
        return false;
    }
    status = BQ76940_Control_EncodeUvTrip(
        config->uv_trip_mv, &s_recovery.snapshot.calibration, &uv_trip);
    if (status != BQ76940_STATUS_OK)
    {
        return false;
    }

    s_recovery.register_addresses[0] = BQ76940_REG_SYS_CTRL2;
    s_recovery.register_values[0] = BMS_AFE_STARTUP_SYS_CTRL2_FET_OFF;
    s_recovery.register_addresses[1] = BQ76940_REG_CELLBAL1;
    s_recovery.register_values[1] = 0U;
    s_recovery.register_addresses[2] = BQ76940_REG_CELLBAL2;
    s_recovery.register_values[2] = 0U;
    s_recovery.register_addresses[3] = BQ76940_REG_CELLBAL3;
    s_recovery.register_values[3] = 0U;
    s_recovery.register_addresses[4] = BQ76940_REG_CC_CFG;
    s_recovery.register_values[4] = BQ76940_CC_CFG_REQUIRED_VALUE;
    s_recovery.register_addresses[5] = BQ76940_REG_OV_TRIP;
    s_recovery.register_values[5] = ov_trip;
    s_recovery.register_addresses[6] = BQ76940_REG_UV_TRIP;
    s_recovery.register_values[6] = uv_trip;
    s_recovery.register_addresses[7] = BQ76940_REG_PROTECT3;
    s_recovery.register_values[7] = protect3;
    s_recovery.register_addresses[8] = BQ76940_REG_PROTECT1;
    s_recovery.register_values[8] = protect1;
    s_recovery.register_addresses[9] = BQ76940_REG_PROTECT2;
    s_recovery.register_values[9] = protect2;
    s_recovery.register_addresses[10] = BQ76940_REG_SYS_CTRL1;
    s_recovery.register_values[10] = BMS_AFE_STARTUP_SYS_CTRL1_REQUIRED;
    s_recovery.register_addresses[11] = BQ76940_REG_SYS_CTRL2;
    s_recovery.register_values[11] = BMS_AFE_STARTUP_SYS_CTRL2_CC_FET_OFF;
    return true;
}

static void BMS_Recovery_ServiceCalibrationRead(void)
{
    BQ76940_Status_t status;

    if (s_recovery.calibration_index == 0U)
    {
        status = BMS_Recovery_OneRead(BQ76940_REG_ADCGAIN1,
                                      &s_recovery.adc_gain1);
    }
    else if (s_recovery.calibration_index == 1U)
    {
        status = BMS_Recovery_OneRead(BQ76940_REG_ADCOFFSET,
                                      &s_recovery.adc_offset);
    }
    else
    {
        status = BMS_Recovery_OneRead(BQ76940_REG_ADCGAIN2,
                                      &s_recovery.adc_gain2);
    }
    if (status != BQ76940_STATUS_OK)
    {
        BMS_Recovery_Fail(status);
        return;
    }
    ++s_recovery.calibration_index;
    if (s_recovery.calibration_index == 3U)
    {
        status = BQ76940_DecodeCalibration(
            s_recovery.adc_gain1, s_recovery.adc_offset,
            s_recovery.adc_gain2, &s_recovery.snapshot.calibration);
        if ((status != BQ76940_STATUS_OK) ||
            !BMS_Recovery_StageRegisterPlan())
        {
            BMS_Recovery_Fail(status == BQ76940_STATUS_OK ?
                              BQ76940_STATUS_RANGE_ERROR : status);
        }
    }
}

static void BMS_Recovery_ServiceConfiguration(uint32_t now_ms)
{
    BQ76940_Status_t status;
    uint8_t actual;

    if (s_recovery.calibration_index < 3U)
    {
        BMS_Recovery_ServiceCalibrationRead();
        return;
    }
    if (s_recovery.config_index >= BMS_RECOVERY_CONFIG_REGISTER_COUNT)
    {
        s_recovery.settle_started_ms = now_ms;
        BMS_Recovery_SetPhase(BMS_RECOVERY_PHASE_POST_CLEAR_SETTLE);
        return;
    }
    if (!s_recovery.verify_register)
    {
        status = BMS_Recovery_OneWrite(
            s_recovery.register_addresses[s_recovery.config_index],
            s_recovery.register_values[s_recovery.config_index]);
        if (status != BQ76940_STATUS_OK)
        {
            BMS_Recovery_Fail(status);
            return;
        }
        s_recovery.verify_register = true;
        return;
    }
    actual = 0U;
    status = BMS_Recovery_OneRead(
        s_recovery.register_addresses[s_recovery.config_index], &actual);
    if (status != BQ76940_STATUS_OK)
    {
        BMS_Recovery_Fail(status);
        return;
    }
    if (actual != s_recovery.register_values[s_recovery.config_index])
    {
        BMS_Recovery_Fail(BQ76940_STATUS_RANGE_ERROR);
        return;
    }
    s_recovery.verify_register = false;
    ++s_recovery.config_index;
}

void BMS_Recovery_Init(BQ76940_t *device,
                       const BMS_Policy_t *policy)
{
    BMS_ProtectXreadyState_t xready;

    xready.xready_generation = 0UL;
    xready.active = false;
    s_recovery.device = device;
    s_recovery.policy = policy;
    s_recovery.snapshot.phase = BMS_RECOVERY_PHASE_COMPLETE;
    s_recovery.snapshot.xready_generation = 0UL;
    if (BMS_Protect_GetXreadyState(&xready))
    {
        s_recovery.snapshot.xready_generation = xready.xready_generation;
    }
    s_recovery.snapshot.recovery_revision = 0UL;
    s_recovery.snapshot.publication_revision = 0UL;
    s_recovery.snapshot.first_valid_sample_sequence = 0UL;
    s_recovery.snapshot.first_valid_afe_generation =
        s_recovery.snapshot.xready_generation;
    s_recovery.snapshot.inhibit_chg_reasons = 0UL;
    s_recovery.snapshot.inhibit_dsg_reasons = 0UL;
    s_recovery.snapshot.calibration.gain_uv_per_lsb = 0U;
    s_recovery.snapshot.calibration.offset_mv = 0;
    s_recovery.snapshot.calibration.valid = false;
    s_recovery.snapshot.last_transport_status = BQ76940_STATUS_OK;
    s_recovery.snapshot.recovery_in_progress = false;
    s_recovery.snapshot.post_clear_verified = false;
    s_recovery.snapshot.calibration_handed_off = false;
    s_recovery.snapshot.first_valid_sample_accepted = false;
    s_recovery.snapshot.technical_ready =
        (device != NULL) && BQ76940_IsInitialized(device) &&
        BMS_Policy_Validate(policy) && (!xready.active);
    s_recovery.config_index = 0U;
    s_recovery.calibration_index = 0U;
    s_recovery.verify_register = false;
    s_recovery.settle_started_ms = 0UL;
    s_recovery.handoff_baseline_sequence = 0UL;
#if defined(TEST_PHASE9_IMAGE)
    s_pre_handoff_hook = NULL;
#endif
}

void BMS_Recovery_Service(uint32_t now_ms)
{
    BMS_ProtectXreadyState_t xready;
    BMS_ProtectXreadyClearAck_t ack;
    BMS_DataIdentity_t identity;
    BMS_SampleCalibrationEvidence_t evidence;
    BQ76940_Status_t status;
    uint8_t sys_stat;

    if ((s_recovery.device == NULL) ||
        !BQ76940_IsInitialized(s_recovery.device) ||
        !BMS_Policy_Validate(s_recovery.policy) ||
        !BMS_Protect_GetXreadyState(&xready))
    {
        BMS_Recovery_Fail(BQ76940_STATUS_NOT_INITIALIZED);
        return;
    }
    if ((xready.xready_generation !=
         s_recovery.snapshot.xready_generation) ||
        (xready.active &&
         (s_recovery.snapshot.phase == BMS_RECOVERY_PHASE_COMPLETE)))
    {
        BMS_Recovery_ResetEvidence(xready.xready_generation);
        return;
    }
    if (!BMS_Recovery_GenerationStillCurrent())
    {
        return;
    }

    switch (s_recovery.snapshot.phase)
    {
        case BMS_RECOVERY_PHASE_PRE_CLEAR_PREPARE:
            BMS_Recovery_SetPhase(BMS_RECOVERY_PHASE_PRE_CLEAR_READY);
            break;
        case BMS_RECOVERY_PHASE_PRE_CLEAR_READY:
            if (BMS_Protect_AuthorizeXreadyClear(
                    s_recovery.snapshot.xready_generation,
                    s_recovery.snapshot.recovery_revision))
            {
                App_Rtos_RequestProtectService();
                BMS_Recovery_SetPhase(BMS_RECOVERY_PHASE_WAIT_CLEAR_ACK);
            }
            break;
        case BMS_RECOVERY_PHASE_WAIT_CLEAR_ACK:
            if (BMS_Protect_GetXreadyClearAck(&ack) && ack.accepted &&
                (ack.xready_generation ==
                 s_recovery.snapshot.xready_generation) &&
                (ack.recovery_revision ==
                 s_recovery.snapshot.recovery_revision) && !xready.active)
            {
                BMS_Recovery_SetPhase(
                    BMS_RECOVERY_PHASE_POST_CLEAR_CONFIG);
            }
            else if (ack.finalization_ambiguous)
            {
                BMS_Recovery_Fail(
                    BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS);
            }
            break;
        case BMS_RECOVERY_PHASE_POST_CLEAR_CONFIG:
            BMS_Recovery_ServiceConfiguration(now_ms);
            break;
        case BMS_RECOVERY_PHASE_POST_CLEAR_SETTLE:
            if (BMS_Recovery_TimeElapsed(now_ms,
                                         s_recovery.settle_started_ms,
                                         BMS_RECOVERY_SETTLE_MS))
            {
                BMS_Recovery_SetPhase(
                    BMS_RECOVERY_PHASE_POST_CLEAR_VERIFY);
            }
            break;
        case BMS_RECOVERY_PHASE_POST_CLEAR_VERIFY:
            sys_stat = 0U;
            status = BMS_Recovery_OneRead(BQ76940_REG_SYS_STAT, &sys_stat);
            if (status != BQ76940_STATUS_OK)
            {
                BMS_Recovery_Fail(status);
            }
            else if ((sys_stat & (BMS_PROTECT_STAT_DEVICE_XREADY |
                                  BMS_AFE_STARTUP_STAT_BLOCKING_MASK)) != 0U)
            {
                BMS_Recovery_Fail(BQ76940_STATUS_RANGE_ERROR);
            }
            else
            {
                s_recovery.snapshot.post_clear_verified = true;
                BMS_Recovery_PublishChange();
                BMS_Recovery_SetPhase(
                    BMS_RECOVERY_PHASE_CALIBRATION_HANDOFF);
            }
            break;
        case BMS_RECOVERY_PHASE_CALIBRATION_HANDOFF:
            if (!BMS_Data_GetIdentity(&identity))
            {
                break;
            }
            s_recovery.handoff_baseline_sequence =
                identity.sample_sequence;
            evidence.xready_generation =
                s_recovery.snapshot.xready_generation;
            evidence.recovery_revision =
                s_recovery.snapshot.recovery_revision;
            evidence.post_clear_verified =
                s_recovery.snapshot.post_clear_verified;
            evidence.calibration = s_recovery.snapshot.calibration;
#if defined(TEST_PHASE9_IMAGE)
            if (s_pre_handoff_hook != NULL)
            {
                s_pre_handoff_hook();
            }
#endif
            if (!BMS_Recovery_GenerationStillCurrent())
            {
                break;
            }
            if (BMS_Sample_SetRecoveryCalibration(
                    &evidence,
                    s_recovery.snapshot.recovery_revision,
                    s_recovery.snapshot.post_clear_verified))
            {
                s_recovery.snapshot.calibration_handed_off = true;
                BMS_Recovery_PublishChange();
                BMS_Recovery_SetPhase(
                    BMS_RECOVERY_PHASE_WAIT_FIRST_VALID_SAMPLE);
            }
            else
            {
                BMS_Recovery_Fail(BQ76940_STATUS_CALIBRATION_INVALID);
            }
            break;
        case BMS_RECOVERY_PHASE_WAIT_FIRST_VALID_SAMPLE:
            if (BMS_Data_GetIdentity(&identity) &&
                (identity.afe_generation ==
                 s_recovery.snapshot.xready_generation) &&
                (identity.sample_sequence !=
                 s_recovery.handoff_baseline_sequence))
            {
                s_recovery.snapshot.first_valid_sample_sequence =
                    identity.sample_sequence;
                s_recovery.snapshot.first_valid_afe_generation =
                    identity.afe_generation;
                s_recovery.snapshot.first_valid_sample_accepted = true;
                if (BMS_Protect_ReleaseXreadyActionLatch(
                        s_recovery.snapshot.xready_generation,
                        s_recovery.snapshot.recovery_revision))
                {
                    s_recovery.snapshot.recovery_in_progress = false;
                    s_recovery.snapshot.inhibit_chg_reasons = 0UL;
                    s_recovery.snapshot.inhibit_dsg_reasons = 0UL;
                    s_recovery.snapshot.technical_ready = true;
                    BMS_Recovery_PublishChange();
                    BMS_Recovery_SetPhase(BMS_RECOVERY_PHASE_COMPLETE);
                }
            }
            break;
        case BMS_RECOVERY_PHASE_IDLE:
        case BMS_RECOVERY_PHASE_COMPLETE:
        case BMS_RECOVERY_PHASE_FAILED:
        default:
            break;
    }
}

BMS_RecoverySnapshot_t BMS_Recovery_GetSnapshot(void)
{
    BMS_RecoverySnapshot_t snapshot;

    vTaskSuspendAll();
    snapshot = s_recovery.snapshot;
    (void)xTaskResumeAll();
    return snapshot;
}

#if defined(TEST_PHASE9_IMAGE)
void BMS_Recovery_TestSetPreHandoffHook(BMS_RecoveryTestHook_t hook)
{
    s_pre_handoff_hook = hook;
}
#endif
