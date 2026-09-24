#include "bms_sample.h"
#include "bms_runtime_port.h"

/*
 * 一次 sampling transaction 先捕获 device/configuration/XREADY identity，
 * 再在 I2C mutex 边界内完成 mandatory core 与可选 TS1 读取。发布前重新核对
 * 所有 identity；任一读失败、binding 改变或 mutex 不可用，都保留上一完整
 * snapshot，绝不把半更新数据暴露给 State、FET 或诊断消费者。
 */

#include <stddef.h>

#include "bms_config.h"
#include "bms_data.h"
#include "bms_protect.h"
#include "bq76940_measurement.h"
#include "bq76940_regs.h"

typedef enum
{
    BMS_SAMPLE_GROUP_NONE = 0, /* 尚未进入任一测量组。 */
    BMS_SAMPLE_GROUP_CELL, /* 13 节电芯电压组。 */
    BMS_SAMPLE_GROUP_PACK, /* 电池包 BAT 电压组。 */
    BMS_SAMPLE_GROUP_CURRENT, /* Protect CC 样本关联组。 */
    BMS_SAMPLE_GROUP_TEMPERATURE /* TS1 与 NTC 温度组。 */
} BMS_SampleGroup_t;

/* 一次采样捕获的配置及身份；读总线期间不持配置临界区。 */
typedef struct
{
    BQ76940_t *device; /* 本轮使用的 AFE 句柄。 */
    BQ76940_Calibration_t calibration; /* 本轮 ADC 换算的校准副本。 */
    const BMS_NtcPoint_t *ntc_points; /* 本轮温度插值表指针。 */
    uint16_t ntc_point_count; /* 插值表中的有效节点数。 */
    uint32_t xready_generation; /* 校准绑定的 XREADY 世代。 */
    uint32_t recovery_revision; /* 校准绑定的恢复事务修订号。 */
    uint32_t revision; /* 本轮配置快照的发布前复核版本。 */
    bool generation_bound; /* 校准已绑定本轮 XREADY 世代。 */
    bool post_clear_verified; /* 恢复后配置复核证据已成立。 */
    bool current_epoch_invalidation_pending; /* 旧电流等待完整新帧淘汰。 */
    bool current_invalidation_required; /* 本轮发布须使旧电流失效。 */
} BMS_SampleConfiguration_t;

/* 启动期绑定的 AFE 句柄，Sample 是测量发布唯一写者。 */
static BQ76940_t *s_device;
/* 当前可用于 ADC 换算的校准值，需与 AFE 世代一起核验。 */
static BQ76940_Calibration_t s_calibration;
/* 当前校准绑定的 XREADY 世代。 */
static uint32_t s_calibration_xready_generation;
/* 当前校准已绑定具体 XREADY 世代的标志。 */
static bool s_calibration_generation_bound;
/* 恢复后校准对应的 Recovery 修订号；零表示启动期来源。 */
static uint32_t s_calibration_recovery_revision;
/* 恢复后校准已具备清除后配置复核证据的标志。 */
static bool s_calibration_post_clear_verified;
/* 当前生命周期要求恢复交接证据才能使用校准的标志。 */
static bool s_recovery_provenance_required;
/* 启动期绑定的只读 NTC 插值表。 */
static const BMS_NtcPoint_t *s_ntc_points;
/* NTC 插值表中的有效节点数。 */
static uint16_t s_ntc_point_count;
/* Sample owner 私有的分组失败与过期诊断。 */
static BMS_SampleDiagnostics_t s_diagnostics;
/* 距离下一次 TS1 读取还剩多少个采样周期。 */
static uint16_t s_temperature_cycles_remaining;
/* 本周期需要读取 TS1 的标志。 */
static bool s_temperature_due;
/* 上次已并入 Data 帧的 Protect CC mailbox 序号。 */
static uint32_t s_last_published_cc_sequence;
/* 该已发布 CC 样本所属 AFE 世代。 */
static uint32_t s_last_published_cc_xready_generation;
/* 当前至少发布过一条 CC 样本的标志。 */
static bool s_cc_sequence_published;
/* 上次成功发布完整电芯核心帧的 AFE 世代。 */
static uint32_t s_last_published_core_xready_generation;
/* 已有完整核心帧世代基线的标志。 */
static bool s_core_xready_generation_published;
/* 新 AFE 世代尚未随完整帧淘汰旧电流的待决标志。 */
static bool s_current_epoch_invalidation_pending;
/* 设备、校准或 NTC 配置变化时推进的发布前复核版本。 */
static uint32_t s_configuration_revision;
/* 已观察过旧数据并锁存过期诊断的标志。 */
static bool s_stale_observed;

/* 饱和递增采样诊断计数，防止回绕掩盖持续失败。 */
static void BMS_Sample_SaturatingIncrement(uint32_t *value)
{
    if (*value < UINT32_MAX)
    {
        ++(*value);
    }
}

/* 校验增益、偏移及有效位，拒绝不可用于换算的校准。 */
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

/* 把驱动状态区分为总线传输故障与其他采样失败。 */
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

/* 按失败测量组累计对应诊断，保留失败来源。 */
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

/* 统一累计整帧拒绝、连续失败和具体传输或配置原因。 */
static void BMS_Sample_RecordFailure(BMS_SampleGroup_t group,
                                     BQ76940_Status_t status,
                                     bool mutex_timeout,
                                     bool configuration_not_ready,
                                     bool publish_failure)
{
    BMS_Runtime_CriticalEnter();
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
    BMS_Runtime_CriticalExit();
}

/* 完整帧发布后清零连续失败并推进成功诊断。 */
static void BMS_Sample_RecordSuccess(bool ntc_curve_unavailable,
                                     bool temperature_unavailable)
{
    BMS_Runtime_CriticalEnter();
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
    BMS_Runtime_CriticalExit();
}

/* 记录本轮 CC 样本缺席，核心电压帧仍可完整发布。 */
static void BMS_Sample_RecordCcUnavailable(void)
{
    BMS_Runtime_CriticalEnter();
    BMS_Sample_SaturatingIncrement(
        &s_diagnostics.cc_mailbox_unavailable_count);
    BMS_Runtime_CriticalExit();
}

/* 记录 XREADY 身份不匹配导致的本轮发布拒绝。 */
static void BMS_Sample_RecordXreadyReject(bool postcheck)
{
    BMS_Runtime_CriticalEnter();
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
    BMS_Runtime_CriticalExit();
}

/* 根据现有快照时效更新采样诊断，不发布半帧。 */
static void BMS_Sample_CheckStale(BMS_TimestampMs_t now_ms)
{
    BMS_DataFreshnessSnapshot_t snapshot;
    bool voltage_stale;
    bool current_stale;
    bool temperature_stale;
    bool stale;

    if (!BMS_Data_GetFreshnessSnapshot(&snapshot, now_ms))
    {
        BMS_Runtime_CriticalEnter();
        BMS_Sample_SaturatingIncrement(
            &s_diagnostics.stale_check_failure_count);
        BMS_Runtime_CriticalExit();
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

    BMS_Runtime_CriticalEnter();
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
    BMS_Runtime_CriticalExit();
}

/* 为本轮采样建立局部 staging 帧，所有字段先取不可用初值。 */
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

/* 判断本周期是否达到 TS1 分频采样节拍。 */
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

/* 仅在存在并发任务时取得配置更新保护，并返回其 ownership。 */
static bool BMS_Sample_BeginConfigUpdate(void)
{
    return BMS_Runtime_ConcurrencyGuardEnter();
}

/* 按 Begin 返回的 ownership 释放配置更新保护。 */
static void BMS_Sample_EndConfigUpdate(bool guard_entered)
{
    BMS_Runtime_ConcurrencyGuardExit(guard_entered);
}

/* 建立未绑定设备、校准无效的采样初始状态。 */
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

/* 更换 AFE 句柄并推进配置修订号，禁止旧采样跨设备发布。 */
void BMS_Sample_SetDevice(BQ76940_t *device)
{
    BMS_ProtectLatestCc_t latest_cc;
    bool have_latest_cc;
    bool guard_entered;

    guard_entered = BMS_Sample_BeginConfigUpdate();
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
    BMS_Sample_EndConfigUpdate(guard_entered);
}

/* 绑定启动期 ADC 校准及当前世代，失败时保持校准不可用。 */
bool BMS_Sample_SetCalibration(
    const BQ76940_Calibration_t *calibration)
{
    BMS_ProtectXreadyState_t xready_state;
    bool valid;
    bool xready_available;
    bool guard_entered;

    valid = BMS_Sample_CalibrationIsValid(calibration) &&
        !s_recovery_provenance_required;
    guard_entered = BMS_Sample_BeginConfigUpdate();
    xready_available = false;
    if (valid)
    {
        /*
         * 嵌套 runtime critical region 是有意的：Protect snapshot 与 calibration
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
    BMS_Sample_EndConfigUpdate(guard_entered);
    return valid;
}

/* 仅接纳带当前 XREADY 世代和恢复修订号的校准交接。 */
bool BMS_Sample_SetRecoveryCalibration(
    const BMS_SampleCalibrationEvidence_t *evidence,
    uint32_t current_recovery_revision,
    bool handoff_permitted)
{
    BMS_ProtectXreadyState_t xready_state;
    bool valid;
    bool guard_entered;

    valid = (evidence != NULL) && handoff_permitted &&
        evidence->post_clear_verified &&
        (evidence->recovery_revision == current_recovery_revision) &&
        BMS_Sample_CalibrationIsValid(
            evidence == NULL ? NULL : &evidence->calibration);
    guard_entered = BMS_Sample_BeginConfigUpdate();
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
    BMS_Sample_EndConfigUpdate(guard_entered);
    return valid;
}

/* 在新 XREADY 世代出现时使旧校准和电流证据失效。 */
void BMS_Sample_InvalidateCalibrationForXready(
    uint32_t xready_generation)
{
    bool guard_entered;

    guard_entered = BMS_Sample_BeginConfigUpdate();
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
    BMS_Sample_EndConfigUpdate(guard_entered);
}

/* 绑定已验证 NTC 表并推进配置修订号，供下轮采样使用。 */
bool BMS_Sample_SetNtcTable(const BMS_NtcPoint_t *points,
                            uint16_t point_count)
{
    bool valid;
    bool guard_entered;

    if ((points == NULL) && (point_count == 0U))
    {
        guard_entered = BMS_Sample_BeginConfigUpdate();
        s_ntc_points = NULL;
        s_ntc_point_count = 0U;
        s_configuration_revision =
            BMS_SAMPLE_CONFIGURATION_REVISION_NEXT(
                s_configuration_revision);
        BMS_Sample_EndConfigUpdate(guard_entered);
        return true;
    }

    valid = BMS_Ntc_ValidateTable(points, point_count);
    if (!valid)
    {
        return false;
    }

    guard_entered = BMS_Sample_BeginConfigUpdate();
    s_ntc_points = points;
    s_ntc_point_count = point_count;
    s_configuration_revision =
        BMS_SAMPLE_CONFIGURATION_REVISION_NEXT(
            s_configuration_revision);
    BMS_Sample_EndConfigUpdate(guard_entered);
    return true;
}

#if defined(TEST_PHASE8_SAMPLE_IMAGE)
/* 测试镜像设置配置修订号以覆盖回绕边界。 */
void BMS_Sample_TestSeedConfigurationRevision(uint32_t revision)
{
    bool guard_entered;

    guard_entered = BMS_Sample_BeginConfigUpdate();
    s_configuration_revision = revision;
    BMS_Sample_EndConfigUpdate(guard_entered);
}
#endif

/* 在短临界区复制分组失败和发布结果计数。 */
BMS_SampleDiagnostics_t BMS_Sample_GetDiagnostics(void)
{
    BMS_SampleDiagnostics_t snapshot;

    BMS_Runtime_CriticalEnter();
    snapshot = s_diagnostics;
    BMS_Runtime_CriticalExit();
    return snapshot;
}

/* 在短临界区一次捕获配置和 XREADY 身份；离开后才进行慢速总线读取。 */
static void BMS_Sample_CaptureConfiguration(
    BMS_SampleConfiguration_t *config,
    BMS_ProtectXreadyState_t *xready_state,
    bool *xready_available)
{
    BMS_Runtime_CriticalEnter();
    config->device = s_device;
    config->calibration = s_calibration;
    config->xready_generation = s_calibration_xready_generation;
    config->generation_bound = s_calibration_generation_bound;
    config->recovery_revision = s_calibration_recovery_revision;
    config->post_clear_verified = s_calibration_post_clear_verified;
    config->ntc_points = s_ntc_points;
    config->ntc_point_count = s_ntc_point_count;
    config->current_epoch_invalidation_pending =
        s_current_epoch_invalidation_pending;
    config->current_invalidation_required =
        config->current_epoch_invalidation_pending ||
        (s_core_xready_generation_published &&
         (s_last_published_core_xready_generation !=
          config->xready_generation));
    config->revision = s_configuration_revision;
    *xready_available = BMS_Protect_GetXreadyState(xready_state);
    BMS_Runtime_CriticalExit();
}

/* 完成一次采样、证据复核和整帧发布；任一步失败保留旧快照。 */
bool BMS_Sample_RunOnce(BMS_TimestampMs_t now_ms)
{
    BMS_MeasurementFrame_t frame;
    BMS_SampleConfiguration_t config; /* 本轮一致捕获的 AFE/校准/NTC 身份。 */
    BMS_ProtectLatestCc_t latest_cc;
    BMS_ProtectXreadyState_t xready_state;
    BQ76940_Status_t status;
    uint32_t cell_index;
    uint16_t cell_mask;
    BMS_PackVoltageMv_t pack_min_mv;
    BMS_PackVoltageMv_t pack_max_mv;
    bool temperature_due;
    bool have_latest_cc;
    bool new_cc;
    bool ntc_curve_unavailable;
    bool temperature_unavailable;
    bool xready_available;
    bool xready_guard_current;
    bool configuration_current;
    bool ntc_config_current;
    bool publish_succeeded;

    /*
     * 采样流水线：
     *   捕获 AFE、校准与 XREADY 的同轮 identity
     *     -> 分段取得 I2C ownership，读取 13S 与 BAT
     *     -> 绑定 ProtectTask 已接纳的同代 CC
     *     -> 到期时读取 TS1 并换算温度
     *     -> 发布前复核 configuration/generation/revision
     *     -> 原子发布完整 measurement frame
     * 所有中间值只放在本地 frame；任一步失败都不会污染上一份共享快照。
     */
    ntc_curve_unavailable = false;
    temperature_unavailable = false;
    BMS_Sample_InitFrame(&frame, now_ms);
    BMS_Sample_CheckStale(now_ms);

    /*
     * 1. 捕获本轮 identity 与配置快照。critical region 只保护多字段复制，
     * 不包围 I2C；这样 Recovery 的配置更新不会被撕裂，也不会被慢总线长期阻塞。
     */
    BMS_Sample_CaptureConfiguration(&config, &xready_state,
                                    &xready_available);

    /* 首次访问 AFE 前证明本轮校准来源仍绑定当前非 active generation。 */
    if (config.device == NULL)
    {
        BMS_Sample_RecordFailure(BMS_SAMPLE_GROUP_NONE,
                                 BQ76940_STATUS_NOT_INITIALIZED,
                                 false, true, false);
        return false;
    }
    if (!BMS_Sample_CalibrationIsValid(&config.calibration))
    {
        BMS_Sample_RecordFailure(BMS_SAMPLE_GROUP_NONE,
                                 BQ76940_STATUS_CALIBRATION_INVALID,
                                 false, false, false);
        return false;
    }
    xready_guard_current = xready_available &&
        config.generation_bound &&
        (((config.recovery_revision == 0UL) &&
          !config.post_clear_verified) ||
         ((config.recovery_revision != 0UL) &&
          config.post_clear_verified)) &&
        BMS_Protect_XreadyBindingIsCurrent(
            &xready_state, config.xready_generation);
    if (!xready_guard_current)
    {
        BMS_Sample_RecordXreadyReject(false);
        BMS_Sample_RecordFailure(BMS_SAMPLE_GROUP_NONE,
                                 BQ76940_STATUS_NOT_INITIALIZED,
                                 false, true, false);
        return false;
    }
    frame.afe_generation = config.xready_generation;
    temperature_due = BMS_Sample_TemperatureIsDue();

    /*
     * 2/3. 取得总线 ownership 后读取 mandatory 13S core。读完立即释放 mutex，
     * 不把后续换算和检查放在锁内；ProtectTask 因优先级更高，可在 transaction
     * 边界之间及时处理 ALERT。完整 BQ transaction 内部仍保持不可交叉。
     */
    if (!BMS_Runtime_BusLock(BMS_I2C_MUTEX_TIMEOUT_MS))
    {
        BMS_Sample_RecordFailure(BMS_SAMPLE_GROUP_CELL,
                                 BQ76940_STATUS_I2C_TIMEOUT,
                                 true, false, false);
        return false;
    }
    status = BQ76940_ReadCellVoltages13(config.device,
                                        &config.calibration,
                                        frame.cell_voltage_mv);
    BMS_Runtime_BusUnlock();
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

    /*
     * 4. BAT 是独立诊断通道，仍属于 mandatory core；它失败时不发布只有电芯的
     * 半帧。再次短暂取锁让 ALERT 有机会插入，而不是从 13S 一直锁到温度结束。
     */
    if (!BMS_Runtime_BusLock(BMS_I2C_MUTEX_TIMEOUT_MS))
    {
        BMS_Sample_RecordFailure(BMS_SAMPLE_GROUP_PACK,
                                 BQ76940_STATUS_I2C_TIMEOUT,
                                 true, false, false);
        return false;
    }
    status = BQ76940_ReadPackVoltageMv(config.device,
                                       &config.calibration,
                                       &frame.bq_pack_voltage_mv);
    BMS_Runtime_BusUnlock();
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

    /*
     * 5. current 绑定 ProtectTask mailbox，而不重新读取 CC 寄存器。Protect 已经
     * 完成 CC_READY/W1C 生命周期；这里只接受同 xready_generation 且尚未发布的
     * sequence，使 Data 中的电流与 AFE 生命周期一致，又不抢走 SOC queue ownership。
     */
    have_latest_cc = BMS_Protect_GetLatestCc(&latest_cc);
    have_latest_cc = have_latest_cc && latest_cc.valid &&
        (latest_cc.xready_generation ==
         config.xready_generation);
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
        frame.current_timestamp_ms = latest_cc.sample_ms;
        frame.current_valid = true;
        frame.current_in_range = true;
    }
    else if (config.current_invalidation_required)
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

    /*
     * 6. TS1 变化慢，按分频节拍读取以减少 I2C 占用。raw/resistance 与最终温度
     * 分层记录：缺 NTC 曲线时仍可保留电阻诊断，但不能伪造 temperature_valid。
     */
    if (temperature_due)
    {
        if (!BMS_Runtime_BusLock(BMS_I2C_MUTEX_TIMEOUT_MS))
        {
            BMS_Sample_RecordFailure(BMS_SAMPLE_GROUP_TEMPERATURE,
                                     BQ76940_STATUS_I2C_TIMEOUT,
                                     true, false, false);
            return false;
        }
        status = BQ76940_ReadTs1Raw(config.device, &frame.ts1_raw14);
        BMS_Runtime_BusUnlock();
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
        if ((config.ntc_points != NULL) && (config.ntc_point_count != 0U))
        {
            if (BMS_Ntc_Interpolate(config.ntc_points,
                                    config.ntc_point_count,
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
     * 7/8. 发布前重新核对 identity，然后原子发布整帧。ProtectTask 优先级高于
     * SampleTask。最终 generation check 与 zero-wait
     * publish 必须处在同一 critical region 内，否则 Protect execution context
     * 可能在两者之间发布 XREADY 新代，使旧 staging frame 误进入新生命周期。
     * 注意这里没有持有 I2C mutex：共享数据发布不能反向阻塞 ALERT 总线事务。
     */
    publish_succeeded = false;
    BMS_Runtime_CriticalEnter();
    xready_available = BMS_Protect_GetXreadyState(&xready_state);
    xready_guard_current = xready_available &&
        s_calibration_generation_bound &&
        (s_calibration_xready_generation ==
         config.xready_generation) &&
        (s_device == config.device) &&
        (s_calibration.valid == config.calibration.valid) &&
        (s_calibration.gain_uv_per_lsb ==
         config.calibration.gain_uv_per_lsb) &&
        (s_calibration.offset_mv == config.calibration.offset_mv) &&
        (s_calibration_recovery_revision ==
         config.recovery_revision) &&
        (s_calibration_post_clear_verified ==
         config.post_clear_verified) &&
        BMS_Protect_XreadyBindingIsCurrent(
            &xready_state, config.xready_generation);
    configuration_current =
        (s_configuration_revision == config.revision) &&
        (s_current_epoch_invalidation_pending ==
         config.current_epoch_invalidation_pending);
    ntc_config_current = !frame.update_temperature ||
        ((s_ntc_points == config.ntc_points) &&
         (s_ntc_point_count == config.ntc_point_count));
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
                config.xready_generation;
            s_core_xready_generation_published = true;
            s_current_epoch_invalidation_pending = false;
        }
    }
    BMS_Runtime_CriticalExit();

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
    return true;
}
