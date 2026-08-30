#include "bms_recovery.h"

/*
 * Recovery Coordinator 只协调分阶段证据，不接管 Protect W1C、FET SYS_CTRL2、
 * Balance CELLBAL 或 Sample measurement ownership。整个非 COMPLETE 流程发布
 * BOTH inhibit；old-generation calibration/sample 永远不能证明 new generation
 * 恢复完成。
 */

#include <stddef.h>

#include "apl_rtos.h"
#include "bms_afe_startup.h"
#include "bms_balance.h"
#include "bms_data.h"
#include "bms_fet_manager.h"
#include "bms_protect.h"
#include "bms_sample.h"
#include "bq76940_control.h"
#include "bq76940_regs.h"

#define BMS_RECOVERY_CONFIG_REGISTER_COUNT      (7U)
#define BMS_RECOVERY_SETTLE_MS                  (800UL)
#define BMS_RECOVERY_I2C_TIMEOUT_MS             (20U)

typedef struct
{
    BQ76940_t *device;              /* 共享 transport handle，生命周期覆盖任务运行。 */
    const BMS_Policy_t *policy;     /* 冻结策略，只读。 */
    BMS_RecoverySnapshot_t snapshot;/* 对外发布的权威恢复状态。 */
    uint8_t register_addresses[BMS_RECOVERY_CONFIG_REGISTER_COUNT]; /* 重写计划地址。 */
    uint8_t register_values[BMS_RECOVERY_CONFIG_REGISTER_COUNT];    /* 与地址同索引目标值。 */
    uint8_t config_index;           /* 当前正在 write/readback 的配置项。 */
    uint8_t calibration_index;      /* ADCGAIN1/OFFSET/ADCGAIN2 分步读取进度。 */
    uint8_t adc_gain1;              /* calibration staging，三字节齐备后才 decode。 */
    uint8_t adc_offset;
    uint8_t adc_gain2;
    uint32_t settle_started_ms;     /* 锁外 settle 计时起点。 */
    uint32_t handoff_baseline_sequence; /* 用于证明 handoff 后确有新帧。 */
    bool verify_register;           /* false=下一步 write，true=下一步 readback。 */
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
    /*
     * 新 XREADY generation 代表新的 AFE 生命周期。即使增益/偏移数值恰好相同，
     * 旧 calibration、旧 sample_sequence、旧 clear ack 也不能证明新器件状态；
     * 因而一次性清空全部阶段证据、递增 recovery_revision，并立即 BOTH inhibit。
     */
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

    /*
     * 配置表刻意不含 SYS_CTRL2 与 CELLBAL1..3：它们分别属于 FET Manager 和
     * BalanceTask。PRE_CLEAR_PREPARE 只等待两个 owner 的 verified safe state。
     */
    s_recovery.register_addresses[0] = BQ76940_REG_CC_CFG;
    s_recovery.register_values[0] = BQ76940_CC_CFG_REQUIRED_VALUE;
    s_recovery.register_addresses[1] = BQ76940_REG_OV_TRIP;
    s_recovery.register_values[1] = ov_trip;
    s_recovery.register_addresses[2] = BQ76940_REG_UV_TRIP;
    s_recovery.register_values[2] = uv_trip;
    s_recovery.register_addresses[3] = BQ76940_REG_PROTECT3;
    s_recovery.register_values[3] = protect3;
    s_recovery.register_addresses[4] = BQ76940_REG_PROTECT1;
    s_recovery.register_values[4] = protect1;
    s_recovery.register_addresses[5] = BQ76940_REG_PROTECT2;
    s_recovery.register_values[5] = protect2;
    s_recovery.register_addresses[6] = BQ76940_REG_SYS_CTRL1;
    s_recovery.register_values[6] = BMS_AFE_STARTUP_SYS_CTRL1_REQUIRED;
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

    /*
     * 每次 service 最多一次 read 或 write。单个寄存器也分成 write 与下一周期
     * readback 两步，使 Protect/Sample 能在 transaction 边界取得 I2C mutex；
     * 只有全表逐项相等后才开始锁外 settle。
     */
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
    BMS_BalanceSnapshot_t balance;
    BMS_FetManagerSnapshot_t fet;
    BMS_ProtectXreadyState_t xready;
    BMS_ProtectXreadyClearAck_t ack;
    BMS_DataIdentity_t identity;
    BMS_SampleCalibrationEvidence_t evidence;
    BQ76940_Status_t status;
    uint8_t sys_stat;

    /*
     * 总流程：安全执行器全关 -> Protect 单写 XREADY W1C -> 重建并回读配置 ->
     * 锁外 settle -> 状态复核 -> 同代 calibration handoff -> 等待首个完整新帧 ->
     * 释放 action latch 与 BOTH inhibit。每个箭头都代表不可省略的独立证据。
     */
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
            /*
             * 等待 FET Manager 回读 CHG/DSG 全关且 CC_EN 保持、BalanceTask 回读
             * CELLBAL 全关，并要求两者都绑定本 xready_generation。跳过此门槛会在
             * AFE 状态重建前留下旧执行命令。
             */
            balance = BMS_Balance_GetSnapshot();
            fet = BMS_FetManager_GetSnapshot();
            if (balance.register_state_confirmed &&
                balance.confirmed_all_off &&
                (balance.confirmed_afe_generation ==
                 s_recovery.snapshot.xready_generation) &&
                fet.register_state_confirmed &&
                !fet.observed.chg_on && !fet.observed.dsg_on &&
                ((fet.observed_sys_ctrl2 & 0x40U) != 0U))
            {
                BMS_Recovery_SetPhase(BMS_RECOVERY_PHASE_PRE_CLEAR_READY);
            }
            break;
        case BMS_RECOVERY_PHASE_PRE_CLEAR_READY:
            /*
             * safe execution state 已由两个 sole writer 证明；这里只提交带
             * generation/recovery_revision 的 clear authorization，真正 W1C 仍由
             * ProtectTask 执行，保证 XREADY generation 只有一个权威来源。
             */
            if (BMS_Protect_AuthorizeXreadyClear(
                    s_recovery.snapshot.xready_generation,
                    s_recovery.snapshot.recovery_revision))
            {
                App_Rtos_RequestProtectService();
                BMS_Recovery_SetPhase(BMS_RECOVERY_PHASE_WAIT_CLEAR_ACK);
            }
            break;
        case BMS_RECOVERY_PHASE_WAIT_CLEAR_ACK:
            /*
             * 等待 Protect ack 与本次 request identity 完全一致，并确认 XREADY
             * 已 inactive。ambiguous finalization 不能猜测成功或 replay，必须失败
             * 并保持 BOTH inhibit。
             */
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
            /*
             * XREADY 清除后按短 transaction 重新写入并回读 runtime configuration。
             * 每次 service 最多一次 I2C 操作，既保持状态机可抢占，也避免长期占用
             * I2C mutex 阻塞 ProtectTask。
             */
            BMS_Recovery_ServiceConfiguration(now_ms);
            break;
        case BMS_RECOVERY_PHASE_POST_CLEAR_SETTLE:
            /*
             * configuration 完成后释放 mutex 等待 AFE settle。直接在锁内 delay 会
             * 阻塞 ALERT/CC/Sample；跳过 settle 又可能把瞬态状态当成稳定 readback。
             */
            if (BMS_Recovery_TimeElapsed(now_ms,
                                         s_recovery.settle_started_ms,
                                         BMS_RECOVERY_SETTLE_MS))
            {
                BMS_Recovery_SetPhase(
                    BMS_RECOVERY_PHASE_POST_CLEAR_VERIFY);
            }
            break;
        case BMS_RECOVERY_PHASE_POST_CLEAR_VERIFY:
            /*
             * 重新读取 SYS_STAT，确认 XREADY 与 blocking status 未重现。此前写入
             * 成功只证明 transport transaction，不等于器件已进入可采样稳定态。
             */
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
            /*
             * 捕获当前 sample baseline，并把 calibration 与 xready_generation、
             * recovery_revision、post_clear_verified 一起交给 Sample owner。
             * old-generation calibration 即使数值相同，也不能作为新代 provenance。
             */
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
            /*
             * 等待 handoff 后 sample_sequence 真正推进，且 afe_generation 精确匹配
             * 本次恢复。只有这帧完整 measurement 被接受，才证明新配置/校准已贯通
             * 到数据面；随后才能释放 XREADY action latch 与 BOTH inhibit。
             */
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
            /* IDLE 只存在于尚未观察到运行期 XREADY 的稳态，不携带恢复证据。 */
        case BMS_RECOVERY_PHASE_COMPLETE:
            /* COMPLETE 表示全证据链成立；若新 generation 到达会立即 reset/restart。 */
        case BMS_RECOVERY_PHASE_FAILED:
            /* FAILED 不自动降级重试为 ready，双向 inhibit 继续由快照保持。 */
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
