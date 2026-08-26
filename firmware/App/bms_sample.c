#include "bms_sample.h"

/*
 * 一次 sampling transaction 先捕获 device/configuration/XREADY identity，
 * 再在 I2C mutex 边界内完成 mandatory core 与可选 TS1 读取。发布前重新核对
 * 所有 identity；任一读失败、binding 改变或 mutex 不可用，都保留上一完整
 * snapshot，绝不把半更新数据暴露给 State、FET 或诊断消费者。
 */

#include <stddef.h>

#include "app_rtos.h"
#include "bms_config.h"
#include "bms_data.h"
#include "bms_health.h"
#include "bms_protect.h"
#include "bq76940_measurement.h"
#include "bq76940_regs.h"

typedef enum
{
    BMS_SAMPLE_GROUP_NONE = 0,
    BMS_SAMPLE_GROUP_CELL,
    BMS_SAMPLE_GROUP_PACK,
    BMS_SAMPLE_GROUP_CURRENT,
    BMS_SAMPLE_GROUP_TEMPERATURE
} BMS_SampleGroup_t;

static BQ76940_t *s_device;
static BQ76940_Calibration_t s_calibration;
static uint32_t s_calibration_xready_generation;
static bool s_calibration_generation_bound;
static uint32_t s_calibration_recovery_revision;
static bool s_calibration_post_clear_verified;
static bool s_recovery_provenance_required;
static const BMS_NtcPoint_t *s_ntc_points;
static uint16_t s_ntc_point_count;
static BMS_SampleDiagnostics_t s_diagnostics;
static uint16_t s_temperature_cycles_remaining;
static bool s_temperature_due;
static uint32_t s_last_published_cc_sequence;
static uint32_t s_last_published_cc_xready_generation;
static bool s_cc_sequence_published;
static uint32_t s_last_published_core_xready_generation;
static bool s_core_xready_generation_published;
static bool s_current_epoch_invalidation_pending;
static uint32_t s_configuration_revision;
static bool s_stale_observed;

static void BMS_Sample_SaturatingIncrement(uint32_t *value)
{
    if (*value < UINT32_MAX)
    {
        ++(*value);
    }
}

static bool BMS_Sample_CalibrationIsValid(
    const BQ76940_Calibration_t *calibration)
{
    return (calibration != NULL) && calibration->valid &&
           (calibration->gain_uv_per_lsb >=
                BQ76940_ADC_GAIN_BASE_UV_PER_LSB) &&
           (calibration->gain_uv_per_lsb <=
                BQ76940_ADC_GAIN_MAX_UV_PER_LSB) &&
           (calibration->offset_mv >= -128) &&
           (calibration->offset_mv <= 127);
}

static bool BMS_Sample_StatusIsTransportFailure(BQ76940_Status_t status)
{
    switch (status)
    {
        case BQ76940_STATUS_I2C_TIMEOUT:
        case BQ76940_STATUS_I2C_NACK:
        case BQ76940_STATUS_I2C_ERROR:
        case BQ76940_STATUS_CRC_MISMATCH:
        case BQ76940_STATUS_CRC_REJECTED:
        case BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS:
            return true;
        default:
            return false;
    }
}

static void BMS_Sample_IncrementGroupFailure(BMS_SampleGroup_t group)
{
    switch (group)
    {
        case BMS_SAMPLE_GROUP_CELL:
            BMS_Sample_SaturatingIncrement(
                &s_diagnostics.cell_group_failure_count);
            break;
        case BMS_SAMPLE_GROUP_PACK:
            BMS_Sample_SaturatingIncrement(
                &s_diagnostics.pack_group_failure_count);
            break;
        case BMS_SAMPLE_GROUP_CURRENT:
            BMS_Sample_SaturatingIncrement(
                &s_diagnostics.current_group_failure_count);
            break;
        case BMS_SAMPLE_GROUP_TEMPERATURE:
            BMS_Sample_SaturatingIncrement(
                &s_diagnostics.temperature_group_failure_count);
            break;
        default:
            break;
    }
}

static void BMS_Sample_RecordFailure(BMS_SampleGroup_t group,
                                     BQ76940_Status_t status,
                                     bool mutex_timeout,
                                     bool configuration_not_ready,
                                     bool publish_failure)
{
    vTaskSuspendAll();
    BMS_Sample_SaturatingIncrement(&s_diagnostics.failure_count);
    BMS_Sample_SaturatingIncrement(&s_diagnostics.frame_reject_count);
    BMS_Sample_SaturatingIncrement(
        &s_diagnostics.consecutive_failure_count);
    if (s_diagnostics.max_consecutive_failure_count <
        s_diagnostics.consecutive_failure_count)
    {
        s_diagnostics.max_consecutive_failure_count =
            s_diagnostics.consecutive_failure_count;
    }
    BMS_Sample_IncrementGroupFailure(group);
    if (mutex_timeout || (status == BQ76940_STATUS_I2C_TIMEOUT))
    {
        BMS_Sample_SaturatingIncrement(
            &s_diagnostics.i2c_timeout_count);
    }
    if (!mutex_timeout &&
        BMS_Sample_StatusIsTransportFailure(status))
    {
        BMS_Sample_SaturatingIncrement(
            &s_diagnostics.transport_failure_count);
    }
    if (status == BQ76940_STATUS_CALIBRATION_INVALID)
    {
        BMS_Sample_SaturatingIncrement(
            &s_diagnostics.calibration_invalid_count);
    }
    if (configuration_not_ready)
    {
        BMS_Sample_SaturatingIncrement(
            &s_diagnostics.configuration_not_ready_count);
    }
    if (publish_failure)
    {
        BMS_Sample_SaturatingIncrement(
            &s_diagnostics.data_publish_failure_count);
    }
    (void)xTaskResumeAll();
}

static void BMS_Sample_RecordSuccess(bool ntc_curve_unavailable,
                                     bool temperature_unavailable)
{
    vTaskSuspendAll();
    BMS_Sample_SaturatingIncrement(&s_diagnostics.success_count);
    s_diagnostics.consecutive_failure_count = 0UL;
    if (ntc_curve_unavailable)
    {
        BMS_Sample_SaturatingIncrement(
            &s_diagnostics.ntc_curve_unavailable_count);
    }
    if (temperature_unavailable)
    {
        BMS_Sample_SaturatingIncrement(
            &s_diagnostics.temperature_conversion_unavailable_count);
    }
    (void)xTaskResumeAll();
}

static void BMS_Sample_RecordCcUnavailable(void)
{
    vTaskSuspendAll();
    BMS_Sample_SaturatingIncrement(
        &s_diagnostics.cc_mailbox_unavailable_count);
    (void)xTaskResumeAll();
}

static void BMS_Sample_RecordXreadyReject(bool postcheck)
{
    vTaskSuspendAll();
    if (postcheck)
    {
        BMS_Sample_SaturatingIncrement(
            &s_diagnostics.xready_postcheck_reject_count);
    }
    else
    {
        BMS_Sample_SaturatingIncrement(
            &s_diagnostics.xready_precheck_reject_count);
    }
    (void)xTaskResumeAll();
}

static void BMS_Sample_CheckStale(BMS_TimestampMs_t now_ms)
{
    BMS_DataFreshnessSnapshot_t snapshot;
    bool voltage_stale;
    bool current_stale;
    bool temperature_stale;
    bool stale;

    if (!BMS_Data_GetFreshnessSnapshot(&snapshot, now_ms))
    {
        vTaskSuspendAll();
        BMS_Sample_SaturatingIncrement(
            &s_diagnostics.stale_check_failure_count);
        (void)xTaskResumeAll();
        return;
    }

    voltage_stale = snapshot.pack_metadata.valid &&
        !BMS_Data_IsFresh(snapshot.pack_metadata.valid,
                          snapshot.pack_metadata.stale_latched,
                          snapshot.pack_metadata.age_ms,
                          BMS_VOLTAGE_FRESH_LIMIT_MS);
    current_stale = snapshot.current_metadata.valid &&
        !BMS_Data_IsFresh(snapshot.current_metadata.valid,
                          snapshot.current_metadata.stale_latched,
                          snapshot.current_metadata.age_ms,
                          BMS_CURRENT_FRESH_LIMIT_MS);
    temperature_stale = snapshot.temperature_metadata.valid &&
        !BMS_Data_IsFresh(snapshot.temperature_metadata.valid,
                          snapshot.temperature_metadata.stale_latched,
                          snapshot.temperature_metadata.age_ms,
                          BMS_TEMPERATURE_FRESH_LIMIT_MS);
    stale = voltage_stale || current_stale || temperature_stale;

    vTaskSuspendAll();
    if (stale)
    {
        BMS_Sample_SaturatingIncrement(&s_diagnostics.stale_sample_count);
        if (!s_stale_observed)
        {
            BMS_Sample_SaturatingIncrement(
                &s_diagnostics.stale_transition_count);
        }
    }
    s_stale_observed = stale;
    (void)xTaskResumeAll();
}

static void BMS_Sample_InitFrame(BMS_MeasurementFrame_t *frame,
                                 BMS_TimestampMs_t now_ms)
{
    uint32_t index;

    frame->timestamp_ms = now_ms;
    frame->afe_generation = 0UL;
    for (index = 0UL; index < (uint32_t)BMS_CELL_COUNT; ++index)
    {
        frame->cell_voltage_mv[index] = (BMS_CellVoltageMv_t)0U;
    }
    frame->cell_valid_bitmap = (uint16_t)0U;
    frame->cell_in_range_bitmap = (uint16_t)0U;
    frame->bq_pack_voltage_mv = (BMS_PackVoltageMv_t)0U;
    frame->bq_pack_valid = false;
    frame->bq_pack_in_range = false;
    frame->update_current = false;
    frame->current_ma = (BMS_CurrentMa_t)0;
    frame->current_timestamp_ms = (BMS_TimestampMs_t)0U;
    frame->current_valid = false;
    frame->current_in_range = false;
    frame->update_temperature = false;
    frame->ts1_raw14 = (uint16_t)0U;
    frame->ts1_resistance_ohm = 0UL;
    frame->temperature_timestamp_ms = (BMS_TimestampMs_t)0U;
    frame->ts1_valid = false;
    frame->temperature_decic = (BMS_TemperatureDeciC_t)0;
    frame->temperature_valid = false;
    frame->temperature_in_range = false;
}

static bool BMS_Sample_TemperatureIsDue(void)
{
    if (!s_temperature_due)
    {
        if (s_temperature_cycles_remaining > 1U)
        {
            --s_temperature_cycles_remaining;
        }
        else
        {
            s_temperature_cycles_remaining = 0U;
            s_temperature_due = true;
        }
    }
    return s_temperature_due;
}

static BMS_TimestampMs_t BMS_Sample_TickToTimestampMs(TickType_t tick)
{
    return (BMS_TimestampMs_t)(
        tick * (TickType_t)portTICK_PERIOD_MS);
}

static bool BMS_Sample_BeginConfigUpdate(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        vTaskSuspendAll();
        return true;
    }
    return false;
}

static void BMS_Sample_EndConfigUpdate(bool resume_scheduler)
{
    if (resume_scheduler)
    {
        (void)xTaskResumeAll();
    }
}

void BMS_Sample_Init(void)
{
    s_device = NULL;
    s_calibration.gain_uv_per_lsb = 0U;
    s_calibration.offset_mv = 0;
    s_calibration.valid = false;
    s_calibration_xready_generation = 0UL;
    s_calibration_generation_bound = false;
    s_calibration_recovery_revision = 0UL;
    s_calibration_post_clear_verified = false;
    s_recovery_provenance_required = false;
    s_ntc_points = NULL;
    s_ntc_point_count = 0U;

    s_diagnostics.success_count = 0UL;
    s_diagnostics.failure_count = 0UL;
    s_diagnostics.frame_reject_count = 0UL;
    s_diagnostics.i2c_timeout_count = 0UL;
    s_diagnostics.transport_failure_count = 0UL;
    s_diagnostics.calibration_invalid_count = 0UL;
    s_diagnostics.configuration_not_ready_count = 0UL;
    s_diagnostics.cell_group_failure_count = 0UL;
    s_diagnostics.pack_group_failure_count = 0UL;
    s_diagnostics.current_group_failure_count = 0UL;
    s_diagnostics.temperature_group_failure_count = 0UL;
    s_diagnostics.temperature_conversion_unavailable_count = 0UL;
    s_diagnostics.ntc_curve_unavailable_count = 0UL;
    s_diagnostics.data_publish_failure_count = 0UL;
    s_diagnostics.cc_mailbox_unavailable_count = 0UL;
    s_diagnostics.stale_sample_count = 0UL;
    s_diagnostics.stale_transition_count = 0UL;
    s_diagnostics.stale_check_failure_count = 0UL;
    s_diagnostics.xready_precheck_reject_count = 0UL;
    s_diagnostics.xready_postcheck_reject_count = 0UL;
    s_diagnostics.consecutive_failure_count = 0UL;
    s_diagnostics.max_consecutive_failure_count = 0UL;

    s_temperature_cycles_remaining = BMS_TEMPERATURE_SAMPLE_DIVIDER;
    s_temperature_due = false;
    s_last_published_cc_sequence = 0UL;
    s_last_published_cc_xready_generation = 0UL;
    s_cc_sequence_published = false;
    s_last_published_core_xready_generation = 0UL;
    s_core_xready_generation_published = false;
    s_current_epoch_invalidation_pending = false;
    s_configuration_revision = 0UL;
    s_stale_observed = false;
}

void BMS_Sample_SetDevice(BQ76940_t *device)
{
    BMS_ProtectLatestCc_t latest_cc;
    bool have_latest_cc;
    bool resume_scheduler;

    resume_scheduler = BMS_Sample_BeginConfigUpdate();
    have_latest_cc = BMS_Protect_GetLatestCc(&latest_cc);
    s_device = device;
    /* calibration 只属于一个 device/XREADY epoch；重绑 device 必须显式重装 calibration。 */
    s_calibration.gain_uv_per_lsb = 0U;
    s_calibration.offset_mv = 0;
    s_calibration.valid = false;
    s_calibration_xready_generation = 0UL;
    s_calibration_generation_bound = false;
    s_calibration_recovery_revision = 0UL;
    s_calibration_post_clear_verified = false;
    /*
     * 把早于 device binding 的 mailbox 值视为已消费。新 device 首个 core frame
     * 仍可接收后续 sequence/generation，但未消费的旧 device current 不能跨界。
     */
    if (have_latest_cc && latest_cc.valid)
    {
        s_last_published_cc_sequence = latest_cc.sequence;
        s_last_published_cc_xready_generation =
            latest_cc.xready_generation;
        s_cc_sequence_published = true;
    }
    else
    {
        s_last_published_cc_sequence = 0UL;
        s_last_published_cc_xready_generation = 0UL;
        s_cc_sequence_published = false;
    }
    s_current_epoch_invalidation_pending = true;
    s_configuration_revision =
        BMS_SAMPLE_CONFIGURATION_REVISION_NEXT(
            s_configuration_revision);
    BMS_Sample_EndConfigUpdate(resume_scheduler);
}

bool BMS_Sample_SetCalibration(
    const BQ76940_Calibration_t *calibration)
{
    BMS_ProtectXreadyState_t xready_state;
    bool valid;
    bool xready_available;
    bool resume_scheduler;

    valid = BMS_Sample_CalibrationIsValid(calibration) &&
        !s_recovery_provenance_required;
    resume_scheduler = BMS_Sample_BeginConfigUpdate();
    xready_available = false;
    if (valid)
    {
        /*
         * 嵌套 scheduler suspension 是有意的：Protect snapshot 与 calibration
         * binding 必须落在同一个外层 exclusion，内层 helper 不得提前恢复调度。
         */
        xready_available =
            BMS_Protect_GetXreadyState(&xready_state);
    }
    if (valid && xready_available && !xready_state.active)
    {
        s_calibration = *calibration;
        s_calibration_xready_generation =
            xready_state.xready_generation;
        s_calibration_generation_bound = true;
        s_calibration_recovery_revision = 0UL;
        s_calibration_post_clear_verified = false;
    }
    else
    {
        s_calibration.gain_uv_per_lsb = 0U;
        s_calibration.offset_mv = 0;
        s_calibration.valid = false;
        s_calibration_xready_generation = 0UL;
        s_calibration_generation_bound = false;
        s_calibration_recovery_revision = 0UL;
        s_calibration_post_clear_verified = false;
        valid = false;
    }
    s_configuration_revision =
        BMS_SAMPLE_CONFIGURATION_REVISION_NEXT(
            s_configuration_revision);
    BMS_Sample_EndConfigUpdate(resume_scheduler);
    return valid;
}

bool BMS_Sample_SetRecoveryCalibration(
    const BMS_SampleCalibrationEvidence_t *evidence,
    uint32_t current_recovery_revision,
    bool handoff_permitted)
{
    BMS_ProtectXreadyState_t xready_state;
    bool valid;
    bool resume_scheduler;

    valid = (evidence != NULL) && handoff_permitted &&
        evidence->post_clear_verified &&
        (evidence->recovery_revision == current_recovery_revision) &&
        BMS_Sample_CalibrationIsValid(
            evidence == NULL ? NULL : &evidence->calibration);
    resume_scheduler = BMS_Sample_BeginConfigUpdate();
    if (valid)
    {
        valid = BMS_Protect_GetXreadyState(&xready_state) &&
            !xready_state.active &&
            (xready_state.xready_generation ==
             evidence->xready_generation);
    }
    if (valid)
    {
        s_calibration = evidence->calibration;
        s_calibration_xready_generation = evidence->xready_generation;
        s_calibration_generation_bound = true;
        s_calibration_recovery_revision = evidence->recovery_revision;
        s_calibration_post_clear_verified = true;
    }
    else
    {
        s_calibration.gain_uv_per_lsb = 0U;
        s_calibration.offset_mv = 0;
        s_calibration.valid = false;
        s_calibration_xready_generation = 0UL;
        s_calibration_generation_bound = false;
        s_calibration_recovery_revision = 0UL;
        s_calibration_post_clear_verified = false;
    }
    s_configuration_revision =
        BMS_SAMPLE_CONFIGURATION_REVISION_NEXT(s_configuration_revision);
    BMS_Sample_EndConfigUpdate(resume_scheduler);
    return valid;
}

void BMS_Sample_InvalidateCalibrationForXready(
    uint32_t xready_generation)
{
    bool resume_scheduler;

    resume_scheduler = BMS_Sample_BeginConfigUpdate();
    /*
     * legacy calibration setter 永不清除此 latch。观察到运行期 XREADY 后，
     * 只有绑定 generation 与 recovery revision 的 coordinator handoff 能恢复采样。
     */
    s_recovery_provenance_required = true;
    if (!s_calibration_generation_bound ||
        (s_calibration_xready_generation != xready_generation) ||
        s_calibration.valid)
    {
        s_calibration.gain_uv_per_lsb = 0U;
        s_calibration.offset_mv = 0;
        s_calibration.valid = false;
        s_calibration_xready_generation = xready_generation;
        s_calibration_generation_bound = false;
        s_calibration_recovery_revision = 0UL;
        s_calibration_post_clear_verified = false;
        s_current_epoch_invalidation_pending = true;
        s_configuration_revision =
            BMS_SAMPLE_CONFIGURATION_REVISION_NEXT(s_configuration_revision);
    }
    BMS_Sample_EndConfigUpdate(resume_scheduler);
}

bool BMS_Sample_SetNtcTable(const BMS_NtcPoint_t *points,
                            uint16_t point_count)
{
    bool valid;
    bool resume_scheduler;

    if ((points == NULL) && (point_count == 0U))
    {
        resume_scheduler = BMS_Sample_BeginConfigUpdate();
        s_ntc_points = NULL;
        s_ntc_point_count = 0U;
        s_configuration_revision =
            BMS_SAMPLE_CONFIGURATION_REVISION_NEXT(
                s_configuration_revision);
        BMS_Sample_EndConfigUpdate(resume_scheduler);
        return true;
    }

    valid = BMS_Ntc_ValidateTable(points, point_count);
    if (!valid)
    {
        return false;
    }

    resume_scheduler = BMS_Sample_BeginConfigUpdate();
    s_ntc_points = points;
    s_ntc_point_count = point_count;
    s_configuration_revision =
        BMS_SAMPLE_CONFIGURATION_REVISION_NEXT(
            s_configuration_revision);
    BMS_Sample_EndConfigUpdate(resume_scheduler);
    return true;
}

#if defined(TEST_PHASE8_SAMPLE_IMAGE)
void BMS_Sample_TestSeedConfigurationRevision(uint32_t revision)
{
    bool resume_scheduler;

    resume_scheduler = BMS_Sample_BeginConfigUpdate();
    s_configuration_revision = revision;
    BMS_Sample_EndConfigUpdate(resume_scheduler);
}
#endif

BMS_SampleDiagnostics_t BMS_Sample_GetDiagnostics(void)
{
    BMS_SampleDiagnostics_t snapshot;

    vTaskSuspendAll();
    snapshot = s_diagnostics;
    (void)xTaskResumeAll();
    return snapshot;
}

bool BMS_Sample_RunOnce(BMS_TimestampMs_t now_ms)
{
    BMS_MeasurementFrame_t frame;
    BQ76940_t *device;
    BQ76940_Calibration_t calibration;
    const BMS_NtcPoint_t *ntc_points;
    uint16_t ntc_point_count;
    BMS_ProtectLatestCc_t latest_cc;
    BMS_ProtectXreadyState_t xready_state;
    BQ76940_Status_t status;
    uint32_t cell_index;
    uint32_t calibration_xready_generation;
    uint32_t calibration_recovery_revision;
    uint32_t configuration_revision;
    uint16_t cell_mask;
    BMS_PackVoltageMv_t pack_min_mv;
    BMS_PackVoltageMv_t pack_max_mv;
    bool temperature_due;
    bool have_latest_cc;
    bool new_cc;
    bool current_epoch_invalidation_pending;
    bool current_invalidation_required;
    bool ntc_curve_unavailable;
    bool temperature_unavailable;
    bool calibration_generation_bound;
    bool calibration_post_clear_verified;
    bool xready_available;
    bool xready_guard_current;
    bool configuration_current;
    bool ntc_config_current;
    bool publish_succeeded;

    ntc_curve_unavailable = false;
    temperature_unavailable = false;
    BMS_Sample_InitFrame(&frame, now_ms);
    BMS_Sample_CheckStale(now_ms);

    vTaskSuspendAll();
    device = s_device;
    calibration = s_calibration;
    calibration_xready_generation =
        s_calibration_xready_generation;
    calibration_generation_bound =
        s_calibration_generation_bound;
    calibration_recovery_revision =
        s_calibration_recovery_revision;
    calibration_post_clear_verified =
        s_calibration_post_clear_verified;
    ntc_points = s_ntc_points;
    ntc_point_count = s_ntc_point_count;
    current_epoch_invalidation_pending =
        s_current_epoch_invalidation_pending;
    current_invalidation_required =
        current_epoch_invalidation_pending ||
        (s_core_xready_generation_published &&
         (s_last_published_core_xready_generation !=
          calibration_xready_generation));
    configuration_revision = s_configuration_revision;
    xready_available = BMS_Protect_GetXreadyState(&xready_state);
    (void)xTaskResumeAll();

    if (device == NULL)
    {
        BMS_Sample_RecordFailure(BMS_SAMPLE_GROUP_NONE,
                                 BQ76940_STATUS_NOT_INITIALIZED,
                                 false, true, false);
        return false;
    }
    if (!BMS_Sample_CalibrationIsValid(&calibration))
    {
        BMS_Sample_RecordFailure(BMS_SAMPLE_GROUP_NONE,
                                 BQ76940_STATUS_CALIBRATION_INVALID,
                                 false, false, false);
        return false;
    }
    xready_guard_current = xready_available &&
        calibration_generation_bound &&
        (((calibration_recovery_revision == 0UL) &&
          !calibration_post_clear_verified) ||
         ((calibration_recovery_revision != 0UL) &&
          calibration_post_clear_verified)) &&
        BMS_Protect_XreadyBindingIsCurrent(
            &xready_state, calibration_xready_generation);
    if (!xready_guard_current)
    {
        BMS_Sample_RecordXreadyReject(false);
        BMS_Sample_RecordFailure(BMS_SAMPLE_GROUP_NONE,
                                 BQ76940_STATUS_NOT_INITIALIZED,
                                 false, true, false);
        return false;
    }
    frame.afe_generation = calibration_xready_generation;
    if (xI2CMutex == NULL)
    {
        BMS_Sample_RecordFailure(BMS_SAMPLE_GROUP_CELL,
                                 BQ76940_STATUS_NOT_INITIALIZED,
                                 false, true, false);
        return false;
    }

    temperature_due = BMS_Sample_TemperatureIsDue();

    if (xSemaphoreTake(xI2CMutex,
                       pdMS_TO_TICKS(BMS_I2C_MUTEX_TIMEOUT_MS)) != pdTRUE)
    {
        BMS_Sample_RecordFailure(BMS_SAMPLE_GROUP_CELL,
                                 BQ76940_STATUS_I2C_TIMEOUT,
                                 true, false, false);
        return false;
    }
    status = BQ76940_ReadCellVoltages13(device,
                                        &calibration,
                                        frame.cell_voltage_mv);
    (void)xSemaphoreGive(xI2CMutex);
    if (status != BQ76940_STATUS_OK)
    {
        BMS_Sample_RecordFailure(BMS_SAMPLE_GROUP_CELL, status,
                                 false, false, false);
        return false;
    }

    frame.cell_valid_bitmap = BMS_CELL_DEFINED_MASK;
    frame.cell_in_range_bitmap = (uint16_t)0U;
    for (cell_index = 0UL;
         cell_index < (uint32_t)BMS_CELL_COUNT;
         ++cell_index)
    {
        cell_mask = (uint16_t)((uint16_t)1U << cell_index);
        if ((frame.cell_voltage_mv[cell_index] >=
                (BMS_CellVoltageMv_t)BMS_CELL_VALID_MIN_MV) &&
            (frame.cell_voltage_mv[cell_index] <=
                (BMS_CellVoltageMv_t)BMS_CELL_VALID_MAX_MV))
        {
            frame.cell_in_range_bitmap |= cell_mask;
        }
    }

    if (xSemaphoreTake(xI2CMutex,
                       pdMS_TO_TICKS(BMS_I2C_MUTEX_TIMEOUT_MS)) != pdTRUE)
    {
        BMS_Sample_RecordFailure(BMS_SAMPLE_GROUP_PACK,
                                 BQ76940_STATUS_I2C_TIMEOUT,
                                 true, false, false);
        return false;
    }
    status = BQ76940_ReadPackVoltageMv(device,
                                       &calibration,
                                       &frame.bq_pack_voltage_mv);
    (void)xSemaphoreGive(xI2CMutex);
    if (status != BQ76940_STATUS_OK)
    {
        BMS_Sample_RecordFailure(BMS_SAMPLE_GROUP_PACK, status,
                                 false, false, false);
        return false;
    }
    pack_min_mv = (BMS_PackVoltageMv_t)BMS_CELL_COUNT *
                  (BMS_PackVoltageMv_t)BMS_CELL_VALID_MIN_MV;
    pack_max_mv = (BMS_PackVoltageMv_t)BMS_CELL_COUNT *
                  (BMS_PackVoltageMv_t)BMS_CELL_VALID_MAX_MV;
    frame.bq_pack_valid = true;
    frame.bq_pack_in_range =
        (frame.bq_pack_voltage_mv >= pack_min_mv) &&
        (frame.bq_pack_voltage_mv <= pack_max_mv);

    have_latest_cc = BMS_Protect_GetLatestCc(&latest_cc);
    have_latest_cc = have_latest_cc && latest_cc.valid &&
        (latest_cc.xready_generation ==
         calibration_xready_generation);
    new_cc = have_latest_cc &&
        (!s_cc_sequence_published ||
         (latest_cc.sequence != s_last_published_cc_sequence) ||
         (latest_cc.xready_generation !=
          s_last_published_cc_xready_generation));
    if (new_cc)
    {
        status = BQ76940_ConvertCcRawToCurrentMa(
            latest_cc.raw,
            BMS_RSENSE_REFERENCE_UOHM,
            (int8_t)BMS_CURRENT_POLARITY,
            &frame.current_ma);
        if (status != BQ76940_STATUS_OK)
        {
            BMS_Sample_RecordFailure(BMS_SAMPLE_GROUP_CURRENT, status,
                                     false, false, false);
            return false;
        }
        frame.update_current = true;
        frame.current_timestamp_ms =
            BMS_Sample_TickToTimestampMs(latest_cc.tick);
        frame.current_valid = true;
        frame.current_in_range = true;
    }
    else if (current_invalidation_required)
    {
        /*
         * 新 AFE epoch 的首个成功 core publication 若没有同代 CC，会原子淘汰
         * 上一代 current；core publish 失败则保留 invalidation pending 到下次。
         */
        frame.update_current = true;
        frame.current_timestamp_ms = now_ms;
        frame.current_valid = false;
        frame.current_in_range = false;
    }
    else if (!have_latest_cc)
    {
        BMS_Sample_RecordCcUnavailable();
    }

    if (temperature_due)
    {
        if (xSemaphoreTake(xI2CMutex,
                           pdMS_TO_TICKS(BMS_I2C_MUTEX_TIMEOUT_MS)) != pdTRUE)
        {
            BMS_Sample_RecordFailure(BMS_SAMPLE_GROUP_TEMPERATURE,
                                     BQ76940_STATUS_I2C_TIMEOUT,
                                     true, false, false);
            return false;
        }
        status = BQ76940_ReadTs1Raw(device, &frame.ts1_raw14);
        (void)xSemaphoreGive(xI2CMutex);
        if (status != BQ76940_STATUS_OK)
        {
            BMS_Sample_RecordFailure(BMS_SAMPLE_GROUP_TEMPERATURE, status,
                                     false, false, false);
            return false;
        }

        status = BQ76940_ConvertTs1RawToResistanceOhm(
            frame.ts1_raw14, &frame.ts1_resistance_ohm);
        if (status != BQ76940_STATUS_OK)
        {
            BMS_Sample_RecordFailure(BMS_SAMPLE_GROUP_TEMPERATURE, status,
                                     false, false, false);
            return false;
        }

        frame.update_temperature = true;
        frame.temperature_timestamp_ms = now_ms;
        frame.ts1_valid = true;
        if ((ntc_points != NULL) && (ntc_point_count != 0U))
        {
            if (BMS_Ntc_Interpolate(ntc_points,
                                    ntc_point_count,
                                    frame.ts1_resistance_ohm,
                                    &frame.temperature_decic))
            {
                frame.temperature_valid = true;
                frame.temperature_in_range = true;
            }
            else
            {
                temperature_unavailable = true;
            }
        }
        else
        {
            ntc_curve_unavailable = true;
            temperature_unavailable = true;
        }
    }

    /*
     * ProtectTask 优先级高于 SampleTask。最终 generation check 与 zero-wait
     * publish 必须处在同一 scheduler exclusion 内，否则 ProtectTask 可能在两者
     * 之间发布 XREADY 新代，使旧 staging frame 误进入新生命周期。
     */
    publish_succeeded = false;
    vTaskSuspendAll();
    xready_available = BMS_Protect_GetXreadyState(&xready_state);
    xready_guard_current = xready_available &&
        s_calibration_generation_bound &&
        (s_calibration_xready_generation ==
         calibration_xready_generation) &&
        (s_device == device) &&
        (s_calibration.valid == calibration.valid) &&
        (s_calibration.gain_uv_per_lsb ==
         calibration.gain_uv_per_lsb) &&
        (s_calibration.offset_mv == calibration.offset_mv) &&
        (s_calibration_recovery_revision ==
         calibration_recovery_revision) &&
        (s_calibration_post_clear_verified ==
         calibration_post_clear_verified) &&
        BMS_Protect_XreadyBindingIsCurrent(
            &xready_state, calibration_xready_generation);
    configuration_current =
        (s_configuration_revision == configuration_revision) &&
        (s_current_epoch_invalidation_pending ==
         current_epoch_invalidation_pending);
    ntc_config_current = !frame.update_temperature ||
        ((s_ntc_points == ntc_points) &&
         (s_ntc_point_count == ntc_point_count));
    if (xready_guard_current && configuration_current &&
        ntc_config_current)
    {
        publish_succeeded = BMS_Data_PublishMeasurement(&frame);
        if (publish_succeeded)
        {
            if (new_cc)
            {
                s_last_published_cc_sequence = latest_cc.sequence;
                s_last_published_cc_xready_generation =
                    latest_cc.xready_generation;
                s_cc_sequence_published = true;
            }
            s_last_published_core_xready_generation =
                calibration_xready_generation;
            s_core_xready_generation_published = true;
            s_current_epoch_invalidation_pending = false;
        }
    }
    (void)xTaskResumeAll();

    if (!xready_guard_current)
    {
        BMS_Sample_RecordXreadyReject(true);
        BMS_Sample_RecordFailure(BMS_SAMPLE_GROUP_NONE,
                                 BQ76940_STATUS_NOT_INITIALIZED,
                                 false, true, false);
        return false;
    }
    if (!configuration_current)
    {
        BMS_Sample_RecordFailure(BMS_SAMPLE_GROUP_NONE,
                                 BQ76940_STATUS_NOT_INITIALIZED,
                                 false, true, false);
        return false;
    }
    if (!ntc_config_current)
    {
        BMS_Sample_RecordFailure(BMS_SAMPLE_GROUP_TEMPERATURE,
                                 BQ76940_STATUS_NOT_INITIALIZED,
                                 false, true, false);
        return false;
    }
    if (!publish_succeeded)
    {
        BMS_Sample_RecordFailure(BMS_SAMPLE_GROUP_NONE,
                                 BQ76940_STATUS_OK,
                                 false, false, true);
        return false;
    }

    if (frame.update_temperature)
    {
        s_temperature_due = false;
        s_temperature_cycles_remaining = BMS_TEMPERATURE_SAMPLE_DIVIDER;
    }
    BMS_Sample_RecordSuccess(ntc_curve_unavailable,
                             temperature_unavailable);
    if (xSysEvents != NULL)
    {
        (void)xEventGroupSetBits(xSysEvents, EVT_SAMPLE_READY);
    }
    return true;
}

void Task_Sample(void *argument)
{
    const TickType_t period = pdMS_TO_TICKS(BMS_SAMPLE_PERIOD_MS);
    TickType_t last_wake;

    (void)argument;
    last_wake = xTaskGetTickCount();
    for (;;)
    {
        vTaskDelayUntil(&last_wake, period);
        (void)BMS_Sample_RunOnce(
            (BMS_TimestampMs_t)(xTaskGetTickCount() *
                                portTICK_PERIOD_MS));
        BMS_Health_Heartbeat(BMS_HEALTH_TASK_SAMPLE);
    }
}
