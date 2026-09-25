#include "fml_protect.h"
#include "os_runtime.h"

#include <stddef.h>

#include "fml_data.h"
#include "fml_health.h"
#include "bsp_bq76940_measurement.h"
#include "bsp_bq76940_regs.h"

/* ------------------------------------------------------------------ */
/* ProtectTask 私有状态；其他模块只能通过一致快照或 identity request 访问。 */
/* ------------------------------------------------------------------ */
static BMS_FaultSummary_t s_fault;
/* ALERT、CC、W1C 和通信故障的私有诊断计数。 */
static BMS_ProtectDiagnostics_t s_diagnostics;
/* 已完成 APL 队列交接、供 Sample 只读的最新同代 CC 镜像。 */
static BMS_ProtectLatestCc_t s_latest_cc;
/* 已从 AFE 读取但尚未得到队列提交确认的 CC 样本。 */
static BMS_CcSample_t s_pending_cc;
/* 上述样本是否仍等待 APL 两阶段交接。 */
static bool s_pending_cc_valid;
/* 每次 CC 样本交接推进的单调 transport identity。 */
static uint32_t s_cc_transport_sequence;
/* Protect 权威 XREADY active 状态与 generation。 */
static BMS_ProtectXreadyState_t s_xready_state;
/* 已观察 XREADY 且需继续协调运行期恢复的标志。 */
static bool s_xready_recovery_pending;
/* CC_READY 已可清除但 W1C 尚未确认的待决状态。 */
static bool s_cc_clear_pending;
/* STOP 提交点不明、禁止盲目重放的 SYS_STAT 位图。 */
static uint8_t s_w1c_finalization_ambiguous_mask;
/* 启动期绑定且覆盖任务生命周期的 AFE 句柄。 */
static BQ76940_t *s_afe_device;
/* 兼容接口的恢复回调；运行期授权仍由 Protect 校验。 */
static BMS_ProtectXreadyRecoveryHook_t s_xready_recovery_hook;
/* 任一安全可见状态改变时推进的 Protect 快照版本。 */
static uint32_t s_publication_revision;
/* 各硬件故障源首次出现时推进的事件身份。 */
static uint32_t s_source_generation[BMS_PROTECT_SOURCE_COUNT];
/* 上次成功读取的 SYS_STAT 字节，用于识别新边沿。 */
static uint8_t s_last_sys_stat;
/* Recovery 提交的当前世代单次 XREADY 清除授权。 */
static BMS_ProtectXreadyClearAuthorization_t s_xready_clear_authorization;
/* Protect 对该清除授权的执行结果及修订号。 */
static BMS_ProtectXreadyClearAck_t s_xready_clear_ack;
/* 启动期绑定的不可变保护策略。 */
static const BMS_Policy_t *s_policy;
/* State 提交、待 Protect 校验的硬件故障释放请求。 */
static BMS_ProtectHwRecoveryRequest_t s_hw_recovery_request;
/* Protect 对硬件故障释放请求的身份绑定应答。 */
static BMS_ProtectHwRecoveryAck_t s_hw_recovery_ack;
/* CAN 等服务入口提交、待 Protect 接纳的受限重置请求。 */
static BMS_ServiceResetRequest_t s_service_reset_request;
/* Protect 对受限服务重置请求的身份绑定应答。 */
static BMS_ServiceResetAck_t s_service_reset_ack;
/* 连续 AFE 通信失败次数，供策略门限判断。 */
static uint8_t s_afe_consecutive_failures;
/* 连续 AFE 通信成功次数，供故障恢复资格判断。 */
static uint8_t s_afe_consecutive_successes;
/* 最近一次成功访问 AFE 的毫秒时刻。 */
static uint32_t s_afe_last_success_ms;
/* 当前 AFE 通信故障持续窗口的起始毫秒时刻。 */
static uint32_t s_afe_comm_active_started_ms;
/* AFE 通信故障持续计时窗口已开启的标志。 */
static bool s_afe_comm_active_timing;
/* 当前策略窗口内已观察的 OCD 事件次数。 */
static uint8_t s_ocd_event_count;
/* OCD 升级计数窗口的起始毫秒时刻。 */
static uint32_t s_ocd_window_started_ms;
/* OCD 升级计数窗口已开启的标志。 */
static bool s_ocd_window_active;

/* legacy lower-phase decision output 仅供本模块事件映射使用，不是 runtime FET authority。 */
static BQ76940_FetRequest_t s_legacy_fet_request;

/* 安全可见状态变化时推进 Protect 发布修订号。 */
static void FML_Protect_AdvanceRevision(void)
{
    s_publication_revision =
        (uint32_t)(s_publication_revision + 1UL);
}

/* 为新出现的硬件故障推进各自的 source generation。 */
static void FML_Protect_RecordNewSourceEvents(uint8_t stat,
                                              uint32_t now_ms)
{
    /* 本轮新观察到的故障事件位。 */
    uint8_t new_events;
    /* 本轮安全状态是否发生变化。 */
    bool changed;

    /*
     * source_generation 只在观察到 0→1 的新事件时推进。request/ack 绑定该代号，
     * 所以旧恢复资格不能清除随后到达的同类 OV/UV/OCD/SCD。持续高电平是同一
     * 未决事件，不应每次 drain 都制造新 generation。
     */
    new_events = (uint8_t)(stat & (uint8_t)(~s_last_sys_stat));
    changed = false;
    if ((new_events & BMS_PROTECT_STAT_OV) != 0U)
    {
        s_source_generation[BMS_PROTECT_SOURCE_HW_OV] =
            (uint32_t)(s_source_generation[BMS_PROTECT_SOURCE_HW_OV] + 1UL);
        changed = true;
        if (s_hw_recovery_ack.fault_id == BMS_FAULT_ID_HW_OV)
        {
            s_hw_recovery_ack.accepted = false;
        }
    }
    if ((new_events & BMS_PROTECT_STAT_UV) != 0U)
    {
        s_source_generation[BMS_PROTECT_SOURCE_HW_UV] =
            (uint32_t)(s_source_generation[BMS_PROTECT_SOURCE_HW_UV] + 1UL);
        changed = true;
        if (s_hw_recovery_ack.fault_id == BMS_FAULT_ID_HW_UV)
        {
            s_hw_recovery_ack.accepted = false;
        }
    }
    if ((new_events & BMS_PROTECT_STAT_OCD) != 0U)
    {
        s_source_generation[BMS_PROTECT_SOURCE_HW_OCD] =
            (uint32_t)(s_source_generation[BMS_PROTECT_SOURCE_HW_OCD] + 1UL);
        changed = true;
        if (s_hw_recovery_ack.fault_id == BMS_FAULT_ID_HW_OCD)
        {
            s_hw_recovery_ack.accepted = false;
        }
        if (s_policy != NULL)
        {
            if (!s_ocd_window_active ||
                ((uint32_t)(now_ms - s_ocd_window_started_ms) >
                 s_policy->ocd_escalation.event_window_ms))
            {
                s_ocd_window_started_ms = now_ms;
                s_ocd_event_count = 1U;
                s_ocd_window_active = true;
            }
            else if (s_ocd_event_count < UINT8_MAX)
            {
                ++s_ocd_event_count;
            }
            if (s_ocd_event_count >=
                s_policy->ocd_escalation.event_count_to_latch)
            {
                s_fault.latched |=
                    FML_Fault_Mask(BMS_FAULT_ID_HW_OCD);
            }
        }
    }
    if ((new_events & BMS_PROTECT_STAT_SCD) != 0U)
    {
        s_source_generation[BMS_PROTECT_SOURCE_HW_SCD] =
            (uint32_t)(s_source_generation[BMS_PROTECT_SOURCE_HW_SCD] + 1UL);
        changed = true;
    }
    s_last_sys_stat = stat;
    if (changed)
    {
        FML_Protect_AdvanceRevision();
    }
}

/* 建立 HW/AFE fault、事件身份和 W1C 隔离的 fail-safe 初值。 */
void FML_Protect_Init(void)
{
    /* 当前安全来源的索引。 */
    uint8_t source_index;

    FML_Fault_Init(&s_fault);
    s_diagnostics.cc_queue_overflow_count = 0UL;
    s_diagnostics.cc_sample_missed_count = 0UL;
    s_diagnostics.cc_enqueue_failure_count = 0UL;
    s_diagnostics.w1c_finalization_ambiguous_count = 0UL;
    s_diagnostics.cc_event_identity_ambiguous_count = 0UL;
    s_diagnostics.w1c_finalization_ambiguous_mask = 0U;
    s_diagnostics.cc_queue_overflow_latched = false;
    s_diagnostics.w1c_finalization_ambiguous_latched = false;
    s_latest_cc.raw = (int16_t)0;
    s_latest_cc.sample_ms = 0UL;
    s_latest_cc.sequence = 0UL;
    s_latest_cc.xready_generation = 0UL;
    s_latest_cc.valid = false;
    s_pending_cc.raw = (int16_t)0;
    s_pending_cc.sample_ms = 0UL;
    s_pending_cc.xready_generation = 0UL;
    s_pending_cc.transport_id = 0UL;
    s_pending_cc_valid = false;
    s_cc_transport_sequence = 0UL;
    s_xready_state.xready_generation = 0UL;
    s_xready_state.active = false;
    s_xready_recovery_pending = false;
    s_cc_clear_pending = false;
    s_w1c_finalization_ambiguous_mask = 0U;
    s_afe_device = NULL;
    s_xready_recovery_hook = NULL;
    s_publication_revision = 0UL;
    for (source_index = 0U;
         source_index < (uint8_t)BMS_PROTECT_SOURCE_COUNT;
         ++source_index)
    {
        s_source_generation[source_index] = 0UL;
    }
    s_last_sys_stat = 0U;
    s_xready_clear_authorization.xready_generation = 0UL;
    s_xready_clear_authorization.recovery_revision = 0UL;
    s_xready_clear_authorization.valid = false;
    s_xready_clear_ack.xready_generation = 0UL;
    s_xready_clear_ack.recovery_revision = 0UL;
    s_xready_clear_ack.protect_revision = 0UL;
    s_xready_clear_ack.accepted = false;
    s_xready_clear_ack.finalization_ambiguous = false;
    s_policy = NULL;
    s_hw_recovery_request.valid = false;
    s_hw_recovery_ack.fault_id = BMS_FAULT_ID_HW_OV;
    s_hw_recovery_ack.request_id = 0UL;
    s_hw_recovery_ack.source_generation = 0UL;
    s_hw_recovery_ack.qualification_revision = 0UL;
    s_hw_recovery_ack.protect_revision = 0UL;
    s_hw_recovery_ack.accepted = false;
    s_service_reset_request.valid = false;
    s_service_reset_ack.source = BMS_SERVICE_RESET_HW_SCD;
    s_service_reset_ack.request_id = 0UL;
    s_service_reset_ack.protect_revision = 0UL;
    s_service_reset_ack.accepted = false;
    s_afe_consecutive_failures = 0U;
    s_afe_consecutive_successes = 0U;
    s_afe_last_success_ms = 0UL;
    s_afe_comm_active_started_ms = 0UL;
    s_afe_comm_active_timing = false;
    s_ocd_event_count = 0U;
    s_ocd_window_started_ms = 0UL;
    s_ocd_window_active = false;
    s_legacy_fet_request.chg = BQ76940_FET_DESIRE_DISABLE;
    s_legacy_fet_request.dsg = BQ76940_FET_DESIRE_DISABLE;
}

/* 绑定启动期 AFE 句柄，供 Protect 独占读取 SYS_STAT。 */
void FML_Protect_SetDevice(BQ76940_t *device)
{
    s_afe_device = device;
}

/* 绑定已验证的不可变硬件保护策略。 */
void FML_Protect_SetPolicy(const BMS_Policy_t *policy, uint32_t now_ms)
{
    s_policy = FML_Policy_Validate(policy) ? policy : NULL;
    s_afe_last_success_ms = now_ms;
}

/* 绑定 XREADY 恢复通知回调，不交出 W1C 所有权。 */
void FML_Protect_SetXreadyRecoveryHook(
    BMS_ProtectXreadyRecoveryHook_t recovery_hook)
{
    s_xready_recovery_hook = recovery_hook;
}

/* 合并硬件活动与锁存故障，供状态判断读取。 */
BMS_FaultSummary_t FML_Protect_GetFaultSummary(void)
{
    /* 本次读取的一致状态快照。 */
    BMS_FaultSummary_t snapshot;

    OS_CriticalEnter();
    snapshot = s_fault;
    OS_CriticalExit();
    return snapshot;
}

/* 把指定故障原因同时加入充电与放电禁止位图。 */
static void FML_Protect_AddBothInhibit(
    BMS_ProtectSafetySnapshot_t *snapshot,
    BMS_FaultId_t fault_id)
{
    /* 本轮禁止 FET 导通的原因位图。 */
    BMS_InhibitReasonBitmap_t reason;

    reason = BMS_INHIBIT_REASON_FAULT(fault_id);
    snapshot->inhibit_chg_reasons |= reason;
    snapshot->inhibit_dsg_reasons |= reason;
}

/* 从 Protect 独占状态合成方向性禁止快照，供 FET 仲裁。 */
BMS_ProtectSafetySnapshot_t FML_Protect_GetSafetySnapshot(void)
{
    /* 本次读取的一致状态快照。 */
    BMS_ProtectSafetySnapshot_t snapshot;
    /* 本轮触发安全动作的来源位集合。 */
    BMS_FaultBitmap_t action_sources;
    /* 由硬件状态映射成的软件故障来源位图。 */
    BMS_FaultBitmap_t mapped_sources;
    /* 当前安全来源的索引。 */
    uint8_t source_index;

    /*
     * 先一致捕获 active/latched/generation/revision，再在私有副本上映射方向动作。
     * active 是“条件当前仍未恢复”，latched 是“事件历史要求额外释放流程”；
     * 某些源即使 active 已解除，latched 仍必须继续禁止对应方向。
     */
    OS_CriticalEnter();
    snapshot.faults = s_fault;
    snapshot.publication_revision = s_publication_revision;
    for (source_index = 0U;
         source_index < (uint8_t)BMS_PROTECT_SOURCE_COUNT;
         ++source_index)
    {
        snapshot.source_generation[source_index] =
            s_source_generation[source_index];
    }
    snapshot.xready_generation = s_xready_state.xready_generation;
    snapshot.xready_active = s_xready_state.active;
    OS_CriticalExit();

    snapshot.inhibit_chg_reasons = 0UL;
    snapshot.inhibit_dsg_reasons = 0UL;
    action_sources = snapshot.faults.active |
        (snapshot.faults.latched &
         (FML_Fault_Mask(BMS_FAULT_ID_HW_SCD) |
          FML_Fault_Mask(BMS_FAULT_ID_AFE_XREADY) |
          FML_Fault_Mask(BMS_FAULT_ID_AFE_OVRD_ALERT) |
          FML_Fault_Mask(BMS_FAULT_ID_AFE_COMM) |
          FML_Fault_Mask(BMS_FAULT_ID_HW_OCD)));
    mapped_sources = 0UL;

    if (FML_Fault_Contains(action_sources, BMS_FAULT_ID_HW_OV))
    {
        snapshot.inhibit_chg_reasons |=
            BMS_INHIBIT_REASON_FAULT(BMS_FAULT_ID_HW_OV);
        mapped_sources |= FML_Fault_Mask(BMS_FAULT_ID_HW_OV);
    }
    if (FML_Fault_Contains(action_sources, BMS_FAULT_ID_HW_UV))
    {
        snapshot.inhibit_dsg_reasons |=
            BMS_INHIBIT_REASON_FAULT(BMS_FAULT_ID_HW_UV);
        mapped_sources |= FML_Fault_Mask(BMS_FAULT_ID_HW_UV);
    }
    if (FML_Fault_Contains(action_sources, BMS_FAULT_ID_HW_OCD))
    {
        snapshot.inhibit_dsg_reasons |=
            BMS_INHIBIT_REASON_FAULT(BMS_FAULT_ID_HW_OCD);
        mapped_sources |= FML_Fault_Mask(BMS_FAULT_ID_HW_OCD);
    }
    if (FML_Fault_Contains(action_sources, BMS_FAULT_ID_HW_SCD))
    {
        FML_Protect_AddBothInhibit(&snapshot, BMS_FAULT_ID_HW_SCD);
        mapped_sources |= FML_Fault_Mask(BMS_FAULT_ID_HW_SCD);
    }
    if (FML_Fault_Contains(action_sources, BMS_FAULT_ID_AFE_XREADY))
    {
        FML_Protect_AddBothInhibit(&snapshot, BMS_FAULT_ID_AFE_XREADY);
        mapped_sources |= FML_Fault_Mask(BMS_FAULT_ID_AFE_XREADY);
    }
    if (FML_Fault_Contains(action_sources, BMS_FAULT_ID_AFE_OVRD_ALERT))
    {
        FML_Protect_AddBothInhibit(&snapshot,
                                   BMS_FAULT_ID_AFE_OVRD_ALERT);
        mapped_sources |= FML_Fault_Mask(BMS_FAULT_ID_AFE_OVRD_ALERT);
    }
    if (FML_Fault_Contains(action_sources, BMS_FAULT_ID_AFE_COMM))
    {
        FML_Protect_AddBothInhibit(&snapshot, BMS_FAULT_ID_AFE_COMM);
        mapped_sources |= FML_Fault_Mask(BMS_FAULT_ID_AFE_COMM);
    }
    if (FML_Fault_Contains(action_sources, BMS_FAULT_ID_AFE_CRC))
    {
        FML_Protect_AddBothInhibit(&snapshot, BMS_FAULT_ID_AFE_CRC);
        mapped_sources |= FML_Fault_Mask(BMS_FAULT_ID_AFE_CRC);
    }
    mapped_sources |= FML_Fault_Mask(BMS_FAULT_ID_AFE_STALE);
    if ((action_sources & ~mapped_sources) != 0UL)
    {
        snapshot.inhibit_chg_reasons |= BMS_INHIBIT_REASON_UNKNOWN_SOURCE;
        snapshot.inhibit_dsg_reasons |= BMS_INHIBIT_REASON_UNKNOWN_SOURCE;
    }
    return snapshot;
}

/* 复制 ALERT、W1C 和 CC 交接的模块诊断计数。 */
BMS_ProtectDiagnostics_t FML_Protect_GetDiagnostics(void)
{
    /* 本次读取的一致状态快照。 */
    BMS_ProtectDiagnostics_t snapshot;

    OS_CriticalEnter();
    snapshot = s_diagnostics;
    snapshot.w1c_finalization_ambiguous_mask =
        s_w1c_finalization_ambiguous_mask;
    OS_CriticalExit();
    return snapshot;
}

/* 累计库仑计采样队列溢出，并保留待处理样本。 */
static void FML_Protect_RecordCcOverflow(bool oldest_was_dropped,
                                         bool replacement_failed)
{
    if (s_diagnostics.cc_queue_overflow_count < UINT32_MAX)
    {
        ++s_diagnostics.cc_queue_overflow_count;
    }
    if (oldest_was_dropped &&
        (s_diagnostics.cc_sample_missed_count < UINT32_MAX))
    {
        ++s_diagnostics.cc_sample_missed_count;
    }
    if (replacement_failed &&
        (s_diagnostics.cc_enqueue_failure_count < UINT32_MAX))
    {
        ++s_diagnostics.cc_enqueue_failure_count;
    }
    s_diagnostics.cc_queue_overflow_latched = true;
}

/* 累计 AFE 传输失败并更新通信故障资格。 */
static void FML_Protect_RecordAfeFailure(BQ76940_Status_t status,
                                         uint32_t now_ms)
{
    /* 当前生效的保护故障位图。 */
    BMS_FaultBitmap_t active_mask;
    /* 上次发布时仍生效的保护故障位图。 */
    BMS_FaultBitmap_t previous_active;

    active_mask = (BMS_FaultBitmap_t)0U;
    if ((status == BQ76940_STATUS_CRC_MISMATCH) ||
        (status == BQ76940_STATUS_CRC_REJECTED))
    {
        active_mask = FML_Fault_Mask(BMS_FAULT_ID_AFE_CRC);
    }
    else if ((status != BQ76940_STATUS_OK) && (s_policy == NULL))
    {
        active_mask = FML_Fault_Mask(BMS_FAULT_ID_AFE_COMM);
    }
    else if (status != BQ76940_STATUS_OK)
    {
        if (s_afe_consecutive_failures < UINT8_MAX)
        {
            ++s_afe_consecutive_failures;
        }
        if (s_afe_consecutive_failures >=
            s_policy->afe_comm.consecutive_failures_to_active)
        {
            active_mask = FML_Fault_Mask(BMS_FAULT_ID_AFE_COMM);
        }
    }
    if (status != BQ76940_STATUS_OK)
    {
        s_afe_consecutive_successes = 0U;
    }
    if (active_mask != (BMS_FaultBitmap_t)0U)
    {
        OS_CriticalEnter();
        previous_active = s_fault.active;
        s_fault.active |= active_mask;
        if (previous_active != s_fault.active)
        {
            FML_Protect_AdvanceRevision();
            if (FML_Fault_Contains(active_mask,
                                   BMS_FAULT_ID_AFE_COMM))
            {
                s_afe_comm_active_started_ms = now_ms;
                s_afe_comm_active_timing = true;
            }
        }
        OS_CriticalExit();
    }
}

/* 隔离最终 STOP 未确认的 W1C 位并累加诊断，禁止盲目重放。 */
static void FML_Protect_RecordW1cFinalizationAmbiguity(uint8_t clear_mask)
{
    if (clear_mask == 0U)
    {
        return;
    }

    /*
     * W1C（Write 1 to Clear）不能用普通 read-modify-write 思维处理：写 1 会消费
     * 对应事件，写 0 才保持。若数据已 ACK 而 STOP 失败，软件不知道写入是否
     * 提交；立即重写可能把期间新到达的同类事件也清掉，所以先 quarantine，
     * 只在后续明确观察到该 bit 为低时退休未决标记。
     */
    OS_CriticalEnter();
    if (s_diagnostics.w1c_finalization_ambiguous_count < UINT32_MAX)
    {
        ++s_diagnostics.w1c_finalization_ambiguous_count;
    }
    if (((clear_mask & BMS_PROTECT_STAT_CC_READY) != 0U) &&
        (s_diagnostics.cc_event_identity_ambiguous_count < UINT32_MAX))
    {
        ++s_diagnostics.cc_event_identity_ambiguous_count;
    }
    s_w1c_finalization_ambiguous_mask |= clear_mask;
    s_diagnostics.w1c_finalization_ambiguous_latched = true;
    OS_CriticalExit();
}

/* 在读到寄存器位已低后退休对应的 W1C 未决隔离。 */
static void FML_Protect_ResolveObservedLowW1c(uint8_t stat)
{
    /* 完成来源映射后的安全禁止位掩码。 */
    uint8_t resolved_mask;
    /* 保护状态变化是否影响安全输出。 */
    bool safety_changed;

    OS_CriticalEnter();
    safety_changed = false;
    resolved_mask = (uint8_t)(s_w1c_finalization_ambiguous_mask &
                              (uint8_t)(~stat));
    s_w1c_finalization_ambiguous_mask &= stat;
    if ((resolved_mask & BMS_PROTECT_STAT_CC_READY) != 0U)
    {
        /*
         * 已入队 sample 仍然有效；只有 observed-low 能证明 quarantine 的 W1C
         * 已不再需要，软件才能安全退休该 transaction marker。
         */
        s_cc_clear_pending = false;
    }
    if (((resolved_mask & BMS_PROTECT_STAT_DEVICE_XREADY) != 0U) &&
        s_xready_recovery_pending)
    {
        /*
         * ambiguous W1C 之前 full recovery hook 已成功；观察到 XREADY low
         * 证明无需 replay，避免重复执行可能具有 side effect 的清除。
         */
        s_fault.active &=
            ~(FML_Fault_Mask(BMS_FAULT_ID_AFE_XREADY));
        s_xready_state.active = false;
        s_xready_recovery_pending = false;
        safety_changed = true;
    }
    if (safety_changed)
    {
        FML_Protect_AdvanceRevision();
    }
    OS_CriticalExit();
}

/* 记录 AFE 成功读取，推进通信恢复资格。 */
static void FML_Protect_RecordAfeReadSuccess(uint32_t now_ms)
{
    /* 上次发布时仍生效的保护故障位图。 */
    BMS_FaultBitmap_t previous_active;

    OS_CriticalEnter();
    s_afe_last_success_ms = now_ms;
    s_afe_consecutive_failures = 0U;
    if (s_afe_consecutive_successes < UINT8_MAX)
    {
        ++s_afe_consecutive_successes;
    }
    previous_active = s_fault.active;
    if ((s_policy == NULL) ||
        (s_afe_consecutive_successes >=
         s_policy->afe_comm.consecutive_successes_to_recover))
    {
        s_fault.active &= ~(FML_Fault_Mask(BMS_FAULT_ID_AFE_CRC));
        if (s_w1c_finalization_ambiguous_mask == 0U)
        {
            s_fault.active &= ~(FML_Fault_Mask(BMS_FAULT_ID_AFE_COMM));
        }
    }
    if (previous_active != s_fault.active)
    {
        FML_Protect_AdvanceRevision();
    }
    OS_CriticalExit();
}

/* 按连续失败和恢复窗口更新 AFE 通信故障状态。 */
static void FML_Protect_UpdateAfeCommPolicy(uint32_t now_ms)
{
    /* 通信类故障在保护位图中的掩码。 */
    BMS_FaultBitmap_t comm_mask;
    /* 上一次保存的值。 */
    BMS_FaultSummary_t previous;

    if (s_policy == NULL)
    {
        return;
    }
    comm_mask = FML_Fault_Mask(BMS_FAULT_ID_AFE_COMM);
    OS_CriticalEnter();
    previous = s_fault;
    if ((uint32_t)(now_ms - s_afe_last_success_ms) >=
        s_policy->afe_comm.no_success_timeout_ms)
    {
        s_fault.active |= comm_mask;
        if (!s_afe_comm_active_timing)
        {
            s_afe_comm_active_started_ms = now_ms;
            s_afe_comm_active_timing = true;
        }
    }
    if (FML_Fault_Contains(s_fault.active, BMS_FAULT_ID_AFE_COMM))
    {
        if (!s_afe_comm_active_timing)
        {
            s_afe_comm_active_started_ms = now_ms;
            s_afe_comm_active_timing = true;
        }
        if ((uint32_t)(now_ms - s_afe_comm_active_started_ms) >=
            s_policy->afe_comm.continuous_latch_ms)
        {
            s_fault.latched |= comm_mask;
        }
    }
    else
    {
        s_afe_comm_active_timing = false;
    }
    if ((previous.active != s_fault.active) ||
        (previous.latched != s_fault.latched))
    {
        FML_Protect_AdvanceRevision();
    }
    OS_CriticalExit();
}

#if defined(TEST_PHASE7_IMAGE) || defined(TEST_PHASE9_IMAGE)
/* 测试镜像直接推进 AFE 通信故障策略窗口。 */
void FML_Protect_TestUpdateAfeCommPolicy(uint32_t now_ms)
{
    FML_Protect_UpdateAfeCommPolicy(now_ms);
}
#endif

/* ------------------------------------------------------------------ */
/* CC domain sample：FML 生产，APL 以 newest-wins 语义传入 CC queue。 */
/* ------------------------------------------------------------------ */
bool FML_Protect_GetPendingCcSample(BMS_CcSample_t *sample)
{
    /* 当前来源的数据是否可用于安全决策。 */
    bool available;

    if (sample == NULL)
    {
        return false;
    }
    OS_CriticalEnter();
    *sample = s_pending_cc;
    available = s_pending_cc_valid;
    if (!available)
    {
        sample->transport_id = 0UL;
    }
    OS_CriticalExit();
    return available;
}

/* 接纳 APL 的队列交接结果，成功后才允许清除当前 CC_READY。 */
bool FML_Protect_CompleteCcTransport(uint32_t transport_id,
                                     bool inserted,
                                     bool overflowed,
                                     bool oldest_was_dropped)
{
    if (overflowed)
    {
        FML_Protect_RecordCcOverflow(oldest_was_dropped, !inserted);
    }
    OS_CriticalEnter();
    if (!s_pending_cc_valid ||
        (s_pending_cc.transport_id != transport_id))
    {
        OS_CriticalExit();
        return false;
    }
    if (!inserted)
    {
        OS_CriticalExit();
        return false;
    }
    if (!s_xready_state.active &&
        (s_pending_cc.xready_generation ==
         s_xready_state.xready_generation))
    {
        s_latest_cc.raw = s_pending_cc.raw;
        s_latest_cc.sample_ms = s_pending_cc.sample_ms;
        s_latest_cc.sequence =
            BMS_PROTECT_CC_SEQUENCE_NEXT(s_latest_cc.sequence);
        s_latest_cc.xready_generation = s_pending_cc.xready_generation;
        s_latest_cc.valid = true;
    }
    else
    {
        s_latest_cc.valid = false;
    }
    s_pending_cc_valid = false;
    s_cc_clear_pending = true;
    OS_CriticalExit();
    return true;
}

/* 复制已交接的同世代 CC 样本供 Sample 关联测量帧。 */
bool FML_Protect_GetLatestCc(BMS_ProtectLatestCc_t *snapshot)
{
    /* 当前来源的数据是否可用于安全决策。 */
    bool available;

    if (snapshot == NULL)
    {
        return false;
    }

    OS_CriticalEnter();
    *snapshot = s_latest_cc;
    available = snapshot->valid && !s_xready_state.active &&
        (snapshot->xready_generation ==
         s_xready_state.xready_generation);
    if (!available)
    {
        snapshot->valid = false;
    }
    OS_CriticalExit();
    return available;
}

/* 复制 Protect 持有的 XREADY active 与 generation 身份。 */
bool FML_Protect_GetXreadyState(BMS_ProtectXreadyState_t *snapshot)
{
    if (snapshot == NULL)
    {
        return false;
    }

    OS_CriticalEnter();
    *snapshot = s_xready_state;
    OS_CriticalExit();
    return true;
}

/* 确认校准或样本绑定当前且非 active 的 XREADY 世代。 */
bool FML_Protect_XreadyBindingIsCurrent(
    const BMS_ProtectXreadyState_t *state,
    uint32_t bound_generation)
{
    return (state != NULL) && !state->active &&
           (state->xready_generation == bound_generation);
}

/* ------------------------------------------------------------------ */
/* XREADY recovery：request/ack 划分 coordinator 与 Protect W1C ownership。 */
/* ------------------------------------------------------------------ */
bool FML_Protect_RecoverXready(BQ76940_t *device, uint32_t now_ms)
{
    /* 保护状态寄存器读取或清除的硬件状态。 */
    BQ76940_Status_t status;
    /* 恢复协调器是否授权清除当前 XREADY。 */
    bool coordinator_authorized;

    /*
     * Protect 是运行期 XREADY W1C 唯一写者。Recovery Coordinator 只能提交带
     * xready_generation/recovery_revision 的一次授权；Protect 复核 active identity
     * 后执行 W1C，并用相同 identity 返回 ack。这样 generation 的创建、清除与
     * active 发布只有一个权威来源，不会出现两个任务各自认为恢复完成。
     */
    if (device == NULL)
    {
        return false;
    }
    /*
     * 上一次 full recovery 到达 ambiguous W1C finalization。XREADY 仍高时重跑
     * hook 或 W1C，可能 replay 非幂等恢复或清除更新事件，因此等待 observed-low。
     */
    if ((s_w1c_finalization_ambiguous_mask &
         BMS_PROTECT_STAT_DEVICE_XREADY) != 0U)
    {
        return false;
    }
    OS_CriticalEnter();
    coordinator_authorized = s_xready_clear_authorization.valid &&
        s_xready_state.active &&
        (s_xready_clear_authorization.xready_generation ==
         s_xready_state.xready_generation);
    OS_CriticalExit();
    if (!coordinator_authorized &&
        ((s_xready_recovery_hook == NULL) ||
         !s_xready_recovery_hook(device)))
    {
        return false;
    }

    /* 权威 hook 确认完整 recovery contract 后才 W1C；history latch 有意保留。 */
    s_xready_recovery_pending = true;
    status = BSP_BQ76940_WriteByte(device, BQ76940_REG_SYS_STAT,
                               BMS_PROTECT_STAT_DEVICE_XREADY);
    if (status == BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS)
    {
        OS_CriticalEnter();
        s_xready_clear_ack.xready_generation =
            s_xready_state.xready_generation;
        s_xready_clear_ack.recovery_revision =
            s_xready_clear_authorization.recovery_revision;
        s_xready_clear_ack.protect_revision = s_publication_revision;
        s_xready_clear_ack.accepted = false;
        s_xready_clear_ack.finalization_ambiguous = true;
        s_xready_clear_authorization.valid = false;
        OS_CriticalExit();
        FML_Protect_RecordW1cFinalizationAmbiguity(
            BMS_PROTECT_STAT_DEVICE_XREADY);
        FML_Protect_RecordAfeFailure(status, now_ms);
        return false;
    }
    if (status != BQ76940_STATUS_OK)
    {
        /* clear 明确被拒绝，保留 fault pending，等待下一次有界 service。 */
        FML_Protect_RecordAfeFailure(status, now_ms);
        return false;
    }
    OS_CriticalEnter();
    s_fault.active &= ~(FML_Fault_Mask(BMS_FAULT_ID_AFE_XREADY));
    s_xready_state.active = false;
    s_xready_recovery_pending = false;
    FML_Protect_AdvanceRevision();
    s_xready_clear_ack.xready_generation =
        s_xready_state.xready_generation;
    s_xready_clear_ack.recovery_revision =
        s_xready_clear_authorization.recovery_revision;
    s_xready_clear_ack.protect_revision = s_publication_revision;
    s_xready_clear_ack.accepted = true;
    s_xready_clear_ack.finalization_ambiguous = false;
    s_xready_clear_authorization.valid = false;
    OS_CriticalExit();
    return true;
}

/* 保存 Recovery 的同世代一次性清除授权，由 Protect 执行 W1C。 */
bool FML_Protect_AuthorizeXreadyClear(uint32_t xready_generation,
                                     uint32_t recovery_revision)
{
    /* 目标 owner 是否接受当前请求。 */
    bool accepted;

    OS_CriticalEnter();
    accepted = s_xready_state.active &&
        (s_xready_state.xready_generation == xready_generation) &&
        !s_xready_clear_authorization.valid &&
        !s_xready_clear_ack.finalization_ambiguous;
    if (accepted)
    {
        s_xready_clear_authorization.xready_generation = xready_generation;
        s_xready_clear_authorization.recovery_revision = recovery_revision;
        s_xready_clear_authorization.valid = true;
        s_xready_clear_ack.accepted = false;
        s_xready_clear_ack.finalization_ambiguous = false;
    }
    OS_CriticalExit();
    return accepted;
}

/* 读取 Protect 对指定 XREADY 清除请求的身份绑定应答。 */
bool FML_Protect_GetXreadyClearAck(BMS_ProtectXreadyClearAck_t *ack)
{
    if (ack == NULL)
    {
        return false;
    }
    OS_CriticalEnter();
    *ack = s_xready_clear_ack;
    OS_CriticalExit();
    return true;
}

/* 仅在同代恢复证据完整时释放 XREADY 的动作锁存。 */
bool FML_Protect_ReleaseXreadyActionLatch(uint32_t xready_generation,
                                         uint32_t recovery_revision)
{
    /* 当前锁或硬件资源是否已释放。 */
    bool released;

    OS_CriticalEnter();
    released = !s_xready_state.active &&
        s_xready_clear_ack.accepted &&
        (s_xready_state.xready_generation == xready_generation) &&
        (s_xready_clear_ack.xready_generation == xready_generation) &&
        (s_xready_clear_ack.recovery_revision == recovery_revision);
    if (released)
    {
        s_fault.latched &=
            ~(FML_Fault_Mask(BMS_FAULT_ID_AFE_XREADY));
        FML_Protect_AdvanceRevision();
    }
    OS_CriticalExit();
    return released;
}

/* 按无符号回绕时间判断请求仍在有效期内。 */
static bool FML_Protect_TimeNotExpired(uint32_t now_ms,
                                       uint32_t expiry_ms)
{
    return ((int32_t)(expiry_ms - now_ms) >= 0);
}

/* 把可恢复硬件 fault ID 映射为 Protect source identity。 */
static bool FML_Protect_SourceOfFault(
    BMS_FaultId_t fault_id,
    BMS_ProtectSourceId_t *source,
    uint8_t *status_mask)
{
    if ((source == NULL) || (status_mask == NULL))
    {
        return false;
    }
    if (fault_id == BMS_FAULT_ID_HW_OV)
    {
        *source = BMS_PROTECT_SOURCE_HW_OV;
        *status_mask = BMS_PROTECT_STAT_OV;
        return true;
    }
    if (fault_id == BMS_FAULT_ID_HW_UV)
    {
        *source = BMS_PROTECT_SOURCE_HW_UV;
        *status_mask = BMS_PROTECT_STAT_UV;
        return true;
    }
    if (fault_id == BMS_FAULT_ID_HW_OCD)
    {
        *source = BMS_PROTECT_SOURCE_HW_OCD;
        *status_mask = BMS_PROTECT_STAT_OCD;
        return true;
    }
    return false;
}

/* 校验硬件恢复请求的 source generation、测量与资格身份。 */
static bool FML_Protect_HwRequestIsCurrent(
    const BMS_ProtectHwRecoveryRequest_t *request,
    uint32_t now_ms)
{
    /* 当前检查的故障来源或源数据地址。 */
    BMS_ProtectSourceId_t source;
    /* 当前测量的序列和 AFE 代身份。 */
    BMS_DataIdentity_t identity;
    /* 当前故障对应的 AFE 状态位掩码。 */
    uint8_t status_mask;

    if ((request == NULL) || !request->valid ||
        !FML_Protect_TimeNotExpired(now_ms, request->expiry_ms) ||
        !FML_Protect_SourceOfFault(request->fault_id,
                                   &source, &status_mask) ||
        !FML_Fault_Contains(s_fault.active, request->fault_id) ||
        (s_source_generation[source] !=
         request->expected_source_generation) ||
        s_xready_state.active ||
        !FML_Data_GetIdentity(&identity))
    {
        return false;
    }
    (void)status_mask;
    return (identity.sample_sequence ==
            request->evaluated_sample_sequence) &&
        (identity.afe_generation ==
         request->evaluated_afe_generation);
}

/* 提交带 source generation 和测量身份的硬件故障释放请求。 */
bool FML_Protect_SubmitHwRecoveryRequest(
    const BMS_ProtectHwRecoveryRequest_t *request,
    uint32_t now_ms)
{
    /* 目标 owner 是否接受当前请求。 */
    bool accepted;

    if ((request == NULL) || !request->valid)
    {
        return false;
    }
    OS_CriticalEnter();
    accepted = FML_Protect_HwRequestIsCurrent(request, now_ms);
    if (accepted)
    {
        s_hw_recovery_request = *request;
        s_hw_recovery_ack.accepted = false;
    }
    OS_CriticalExit();
    return accepted;
}

/* 读取 Protect 对硬件故障释放请求的身份绑定应答。 */
bool FML_Protect_GetHwRecoveryAck(BMS_ProtectHwRecoveryAck_t *ack)
{
    if (ack == NULL)
    {
        return false;
    }
    OS_CriticalEnter();
    *ack = s_hw_recovery_ack;
    OS_CriticalExit();
    return true;
}

/* 提交带时效与测量身份的受限服务重置请求。 */
bool FML_Protect_SubmitServiceResetRequest(
    const BMS_ServiceResetRequest_t *request)
{
    /* 当前测量的序列和 AFE 代身份。 */
    BMS_DataIdentity_t identity;
    /* 目标 owner 是否接受当前请求。 */
    bool accepted;

    if ((request == NULL) || !request->valid ||
        ((uint32_t)request->source >=
         (uint32_t)BMS_SERVICE_RESET_SOURCE_COUNT) ||
        !FML_Data_GetIdentity(&identity))
    {
        return false;
    }
    accepted = !s_xready_state.active &&
        (identity.sample_sequence == request->evaluated_sample_sequence) &&
        (identity.afe_generation == request->evaluated_afe_generation);
    if (accepted)
    {
        OS_CriticalEnter();
        s_service_reset_request = *request;
        s_service_reset_ack.accepted = false;
        OS_CriticalExit();
    }
    return accepted;
}

/* 读取 Protect 对服务重置请求的身份绑定应答。 */
bool FML_Protect_GetServiceResetAck(BMS_ServiceResetAck_t *ack)
{
    if (ack == NULL)
    {
        return false;
    }
    OS_CriticalEnter();
    *ack = s_service_reset_ack;
    OS_CriticalExit();
    return true;
}

/* 验证请求身份与当前故障源后，由 Protect owner 释放可恢复硬件故障。 */
static void FML_Protect_ServiceHwRecovery(uint32_t now_ms)
{
    /* 本轮提交给目标 owner 的请求。 */
    BMS_ProtectHwRecoveryRequest_t request;
    /* 当前检查的故障来源或源数据地址。 */
    BMS_ProtectSourceId_t source;
    /* 保护状态寄存器读取或清除的硬件状态。 */
    BQ76940_Status_t status;
    /* 写入目标寄存器的状态位掩码。 */
    uint8_t target_status_mask;
    /* 从 AFE 读取的状态寄存器内容。 */
    uint8_t stat;

    /*
     * SYS_STAT 某 bit 已低只说明寄存器当前没有报告该条件，不足以直接恢复。
     * 本函数还要求 State/HwRecovery 提交的连续 measurement 资格、sample/AFE
     * identity、source_generation 和 expiry 全部仍当前，并在 I2C 读取前后重复
     * 验证；否则新事件或新采样可能夹在检查之间，旧 request 必须作废。
     */
    OS_CriticalEnter();
    request = s_hw_recovery_request;
    OS_CriticalExit();
    if (!request.valid)
    {
        return;
    }
    if (!FML_Protect_HwRequestIsCurrent(&request, now_ms) ||
        !FML_Protect_SourceOfFault(request.fault_id,
                                   &source, &target_status_mask) ||
        !OS_BusLock(BMS_PROTECT_I2C_TIMEOUT_MS))
    {
        OS_CriticalEnter();
        s_hw_recovery_request.valid = false;
        s_hw_recovery_ack.accepted = false;
        OS_CriticalExit();
        return;
    }
    if (!FML_Protect_HwRequestIsCurrent(&request, now_ms))
    {
        OS_BusUnlock();
        OS_CriticalEnter();
        s_hw_recovery_request.valid = false;
        s_hw_recovery_ack.accepted = false;
        OS_CriticalExit();
        return;
    }
    stat = 0U;
    status = BSP_BQ76940_ReadByte(s_afe_device, BQ76940_REG_SYS_STAT, &stat);
    if (status != BQ76940_STATUS_OK)
    {
        OS_BusUnlock();
        FML_Protect_RecordAfeFailure(status, now_ms);
        OS_CriticalEnter();
        s_hw_recovery_request.valid = false;
        s_hw_recovery_ack.accepted = false;
        OS_CriticalExit();
        return;
    }
    if (((stat & target_status_mask) != 0U) ||
        ((stat & (BMS_PROTECT_STAT_DEVICE_XREADY |
                  BMS_PROTECT_STAT_OVRD_ALERT |
                  BMS_PROTECT_STAT_SCD)) != 0U) ||
        !FML_Protect_HwRequestIsCurrent(&request, now_ms))
    {
        OS_BusUnlock();
        OS_CriticalEnter();
        s_hw_recovery_request.valid = false;
        s_hw_recovery_ack.accepted = false;
        OS_CriticalExit();
        return;
    }
    OS_BusUnlock();

    OS_CriticalEnter();
    if (s_hw_recovery_request.valid &&
        (s_hw_recovery_request.request_id == request.request_id) &&
        (s_source_generation[source] ==
         request.expected_source_generation))
    {
        s_fault.active &= ~FML_Fault_Mask(request.fault_id);
        FML_Protect_AdvanceRevision();
        s_hw_recovery_ack.fault_id = request.fault_id;
        s_hw_recovery_ack.request_id = request.request_id;
        s_hw_recovery_ack.source_generation =
            request.expected_source_generation;
        s_hw_recovery_ack.qualification_revision =
            request.qualification_revision;
        s_hw_recovery_ack.protect_revision = s_publication_revision;
        s_hw_recovery_ack.accepted = true;
    }
    s_hw_recovery_request.valid = false;
    OS_CriticalExit();
}

/* 按服务重置来源映射可清除的故障范围。 */
static bool FML_Protect_ServiceResetMapping(
    BMS_ServiceResetSource_t source,
    BMS_FaultId_t *fault_id,
    uint8_t *status_mask)
{
    if ((fault_id == NULL) || (status_mask == NULL))
    {
        return false;
    }
    if (source == BMS_SERVICE_RESET_HW_SCD)
    {
        *fault_id = BMS_FAULT_ID_HW_SCD;
        *status_mask = BMS_PROTECT_STAT_SCD;
        return true;
    }
    if (source == BMS_SERVICE_RESET_AFE_OVRD_ALERT)
    {
        *fault_id = BMS_FAULT_ID_AFE_OVRD_ALERT;
        *status_mask = BMS_PROTECT_STAT_OVRD_ALERT;
        return true;
    }
    if (source == BMS_SERVICE_RESET_AFE_COMM)
    {
        *fault_id = BMS_FAULT_ID_AFE_COMM;
        *status_mask = 0U;
        return true;
    }
    return false;
}

/* 由 Protect owner 消费满足身份和策略约束的受限服务重置请求。 */
static void FML_Protect_ServiceReset(uint32_t now_ms)
{
    /* 本轮提交给目标 owner 的请求。 */
    BMS_ServiceResetRequest_t request;
    /* 当前测量的序列和 AFE 代身份。 */
    BMS_DataIdentity_t identity;
    /* 本轮服务的保护故障标识。 */
    BMS_FaultId_t fault_id;
    /* 保护状态寄存器读取或清除的硬件状态。 */
    BQ76940_Status_t status;
    /* 当前故障对应的 AFE 状态位掩码。 */
    uint8_t status_mask;
    /* 从 AFE 读取的状态寄存器内容。 */
    uint8_t stat;
    /* 当前计算或读取的值。 */
    bool current;

    OS_CriticalEnter();
    request = s_service_reset_request;
    OS_CriticalExit();
    if (!request.valid ||
        !FML_Protect_ServiceResetMapping(request.source,
                                         &fault_id, &status_mask) ||
        !FML_Protect_TimeNotExpired(now_ms, request.expiry_ms) ||
        s_xready_state.active || !FML_Data_GetIdentity(&identity) ||
        (identity.sample_sequence != request.evaluated_sample_sequence) ||
        (identity.afe_generation != request.evaluated_afe_generation) ||
        !OS_BusLock(BMS_PROTECT_I2C_TIMEOUT_MS))
    {
        OS_CriticalEnter();
        s_service_reset_request.valid = false;
        s_service_reset_ack.accepted = false;
        OS_CriticalExit();
        return;
    }
    stat = 0U;
    status = BSP_BQ76940_ReadByte(s_afe_device, BQ76940_REG_SYS_STAT, &stat);
    OS_BusUnlock();
    if (status != BQ76940_STATUS_OK)
    {
        FML_Protect_RecordAfeFailure(status, now_ms);
        current = false;
    }
    else
    {
        current = ((stat & status_mask) == 0U) &&
            !s_xready_state.active && FML_Data_GetIdentity(&identity) &&
            (identity.sample_sequence == request.evaluated_sample_sequence) &&
            (identity.afe_generation == request.evaluated_afe_generation);
        if (request.source == BMS_SERVICE_RESET_AFE_COMM)
        {
            current = current &&
                !FML_Fault_Contains(s_fault.active,
                                    BMS_FAULT_ID_AFE_COMM);
        }
    }
    OS_CriticalEnter();
    if (current && s_service_reset_request.valid &&
        (s_service_reset_request.request_id == request.request_id))
    {
        s_fault.active &= ~FML_Fault_Mask(fault_id);
        s_fault.latched &= ~FML_Fault_Mask(fault_id);
        FML_Protect_AdvanceRevision();
        s_service_reset_ack.source = request.source;
        s_service_reset_ack.request_id = request.request_id;
        s_service_reset_ack.protect_revision = s_publication_revision;
        s_service_reset_ack.accepted = true;
    }
    else
    {
        s_service_reset_ack.accepted = false;
    }
    s_service_reset_request.valid = false;
    OS_CriticalExit();
}

/* ------------------------------------------------------------------ */
/* SYS_STAT drain（H-05）与逐 bit 处理（H-01/H-02/H-03）。 */
/* ------------------------------------------------------------------ */
static void FML_Protect_HandleCcReady(BQ76940_t *device,
                                      uint8_t *clear_mask,
                                      uint32_t now_ms)
{
    /* 从 CC 寄存器读取的原始有符号电流值。 */
    int16_t cc_raw;
    /* 保护状态寄存器读取或清除的硬件状态。 */
    BQ76940_Status_t status;

    /*
     * data byte 已 ACK 而 STOP 失败时提交点未知，无法分辨 old/new event identity。
     * 对应 bit 持续高期间既不 replay W1C，也不重复 enqueue。
     */
    if ((s_w1c_finalization_ambiguous_mask &
         BMS_PROTECT_STAT_CC_READY) != 0U)
    {
        return;
    }

    /* sample 已提交 queue、W1C 又明确失败时，只重试 clear，不能二次 enqueue。 */
    if (s_cc_clear_pending)
    {
        *clear_mask |= BMS_PROTECT_STAT_CC_READY;
        return;
    }

    /*
     * H-02 两阶段交接：先读成 FML domain sample，但本轮不清 CC_READY。APL
     * 将 sample 放入 newest-wins queue 并确认后，下一次 service 才进入上面的
     * s_cc_clear_pending 分支，从而保持 queue commit 先于 W1C。
     */
    if (s_pending_cc_valid)
    {
        return;
    }
    status = BSP_BQ76940_ReadCcRaw(device, &cc_raw);
    if (status == BQ76940_STATUS_OK)
    {
        OS_CriticalEnter();
        s_cc_transport_sequence =
            (uint32_t)(s_cc_transport_sequence + 1UL);
        s_pending_cc.raw = cc_raw;
        s_pending_cc.sample_ms = now_ms;
        s_pending_cc.xready_generation =
            s_xready_state.xready_generation;
        s_pending_cc.transport_id = s_cc_transport_sequence;
        s_pending_cc_valid = true;
        OS_CriticalExit();
    }
    else
    {
        FML_Protect_RecordAfeFailure(status, now_ms);
    }
}

/* 把 SYS_STAT 位映射为故障状态、FET 旧接口请求及可清位。 */
void FML_Protect_Decide(uint8_t stat,
                        BMS_FaultSummary_t *faults,
                        BQ76940_FetRequest_t *request,
                        uint8_t *clear_mask)
{
    if ((faults == NULL) || (request == NULL) || (clear_mask == NULL))
    {
        return;
    }

    /*
     * SYS_STAT 软件语义：OV/UV/OCD 是可经证据恢复的方向性硬件源；SCD 是动作
     * 锁存源；OVRD_ALERT 表示外部 override；XREADY 表示 AFE 生命周期中断；
     * CC_READY 由独立队列路径处理。clear_mask 只包含本轮已经被各 owner 接纳的
     * W1C source，绝不把整寄存器读值原样写回。
     */
    *clear_mask = 0U;

    /* OV：捕获 unresolved active 并 inhibit CHG；只能通过完整 recovery handshake 清除。 */
    if ((stat & BMS_PROTECT_STAT_OV) != 0U)
    {
        faults->active |= FML_Fault_Mask(BMS_FAULT_ID_HW_OV);
        request->chg = BQ76940_FET_DESIRE_DISABLE;
        *clear_mask |= BMS_PROTECT_STAT_OV;
    }

    /* UV：捕获 unresolved active 并 inhibit DSG，不随 SYS_STAT W1C 自动清 active。 */
    if ((stat & BMS_PROTECT_STAT_UV) != 0U)
    {
        faults->active |= FML_Fault_Mask(BMS_FAULT_ID_HW_UV);
        request->dsg = BQ76940_FET_DESIRE_DISABLE;
        *clear_mask |= BMS_PROTECT_STAT_UV;
    }

    /* OCD：捕获 unresolved active 并 inhibit DSG，不随 SYS_STAT W1C 自动清 active。 */
    if ((stat & BMS_PROTECT_STAT_OCD) != 0U)
    {
        faults->active |= FML_Fault_Mask(BMS_FAULT_ID_HW_OCD);
        request->dsg = BQ76940_FET_DESIRE_DISABLE;
        *clear_mask |= BMS_PROTECT_STAT_OCD;
    }

    /* SCD：active + policy latch，并同时 inhibit CHG/DSG。 */
    if ((stat & BMS_PROTECT_STAT_SCD) != 0U)
    {
        faults->active |= FML_Fault_Mask(BMS_FAULT_ID_HW_SCD);
        faults->latched |= FML_Fault_Mask(BMS_FAULT_ID_HW_SCD);
        request->chg = BQ76940_FET_DESIRE_DISABLE;
        request->dsg = BQ76940_FET_DESIRE_DISABLE;
        *clear_mask |= BMS_PROTECT_STAT_SCD;
    }

    /* OVRD_ALERT（H-01）独立捕获 active+latched，并双向 inhibit，绝不自动恢复。 */
    if ((stat & BMS_PROTECT_STAT_OVRD_ALERT) != 0U)
    {
        faults->active |= FML_Fault_Mask(BMS_FAULT_ID_AFE_OVRD_ALERT);
        faults->latched |= FML_Fault_Mask(BMS_FAULT_ID_AFE_OVRD_ALERT);
        request->chg = BQ76940_FET_DESIRE_DISABLE;
        request->dsg = BQ76940_FET_DESIRE_DISABLE;
        *clear_mask |= BMS_PROTECT_STAT_OVRD_ALERT;
    }

    /*
     * XREADY（H-03）：active+latched 并双向 inhibit。完整 reinitialization 只可
     * 通过 BMS_Protect_RecoverXready 清 active；latched 由独立策略持有。
     */
    if ((stat & BMS_PROTECT_STAT_DEVICE_XREADY) != 0U)
    {
        faults->active |= FML_Fault_Mask(BMS_FAULT_ID_AFE_XREADY);
        faults->latched |= FML_Fault_Mask(BMS_FAULT_ID_AFE_XREADY);
        request->chg = BQ76940_FET_DESIRE_DISABLE;
        request->dsg = BQ76940_FET_DESIRE_DISABLE;
        /* H-03：此处绝不把 XREADY 加入普通 clear mask。 */
    }
}

/* 检查 SYS_STAT 是否含会影响安全判断的硬件故障位。 */
bool FML_Protect_HasFaultBits(uint8_t stat)
{
    return ((stat & (BMS_PROTECT_STAT_OV | BMS_PROTECT_STAT_UV |
                     BMS_PROTECT_STAT_SCD | BMS_PROTECT_STAT_OCD |
                     BMS_PROTECT_STAT_DEVICE_XREADY |
                     BMS_PROTECT_STAT_OVRD_ALERT)) != 0U);
}

/* 有界处理 SYS_STAT 事件、CC 交接和 W1C；未完成工作交由下一轮重试。 */
BMS_ProtectDrainResult_t FML_Protect_Drain(BQ76940_t *device,
                                           uint32_t now_ms)
{
    /* 从 AFE 读取的状态寄存器内容。 */
    uint8_t stat;
    /* 准备从硬件状态寄存器清除的位。 */
    uint8_t clear_mask;
    /* 本轮状态寄存器清理的尝试次数。 */
    uint8_t iteration;
    /* 保护状态寄存器读取或清除的硬件状态。 */
    BQ76940_Status_t status;
    /* 清理硬件前保存的保护故障摘要。 */
    BMS_FaultSummary_t previous_fault;
    /* 上一轮 XREADY 是否仍处于激活状态。 */
    bool xready_was_active;

    if (device == NULL)
    {
        return BMS_PROTECT_DRAIN_RETRY_REQUIRED;
    }

    /*
     * 一次 drain 最多循环固定次数：每轮 read snapshot -> 记录新 source identity
     * -> 分别 decode/接纳 -> 只 W1C 已拥有的位 -> 再读。上限防止 ALERT 持续高
     * 时最高优先级任务饿死其他任务；RETRY 会在任务层短延时后继续，不丢 pending。
     */
    for (iteration = 0U; iteration < BMS_PROTECT_DRAIN_MAX_ITER; ++iteration)
    {
        stat = 0U;
        status = BSP_BQ76940_ReadByte(device, BQ76940_REG_SYS_STAT, &stat);
        if (status != BQ76940_STATUS_OK)
        {
            /* I2C/CRC 失败：按 H-05 保留 pending，并发布 AFE comm fault。 */
            FML_Protect_RecordAfeFailure(status, now_ms);
            return BMS_PROTECT_DRAIN_RETRY_REQUIRED;
        }
        FML_Protect_ResolveObservedLowW1c(stat);
        FML_Protect_RecordAfeReadSuccess(now_ms);
        OS_CriticalEnter();
        FML_Protect_RecordNewSourceEvents(stat, now_ms);
        OS_CriticalExit();

        /*
         * 明确拒绝的 W1C 可能被 reset 或受权外部 clear 变得无须重试；成功读到
         * low 即退休 retry marker。ambiguous finalization 使用独立 quarantine。
         */
        if ((stat & BMS_PROTECT_STAT_CC_READY) == 0U)
        {
            s_cc_clear_pending = false;
        }

        if (stat == 0U)
        {
            if (s_xready_recovery_pending)
            {
                if (!FML_Protect_RecoverXready(device, now_ms))
                {
                    return BMS_PROTECT_DRAIN_RETRY_REQUIRED;
                }
                continue;
            }
            return BMS_PROTECT_DRAIN_COMPLETE;
        }

        clear_mask = 0U;
        /* getter 同样 suspend scheduler，使 active+latched 作为一代发布而非 torn words。 */
        OS_CriticalEnter();
        previous_fault = s_fault;
        xready_was_active = s_xready_state.active;
        FML_Protect_Decide(stat, &s_fault, &s_legacy_fet_request,
                           &clear_mask);
        if ((stat & BMS_PROTECT_STAT_DEVICE_XREADY) != 0U)
        {
            if (!s_xready_state.active)
            {
                s_xready_state.xready_generation =
                    BMS_PROTECT_XREADY_GENERATION_NEXT(
                        s_xready_state.xready_generation);
                /* 上一 AFE epoch 的 current 不能以可消费 latest-CC mailbox 跨过恢复。 */
                s_latest_cc.valid = false;
                s_xready_clear_authorization.valid = false;
                s_xready_clear_ack.accepted = false;
                s_xready_clear_ack.finalization_ambiguous = false;
            }
            s_xready_state.active = true;
            s_xready_recovery_pending = true;
        }
        if ((previous_fault.active != s_fault.active) ||
            (previous_fault.latched != s_fault.latched) ||
            (xready_was_active != s_xready_state.active))
        {
            FML_Protect_AdvanceRevision();
        }
        OS_CriticalExit();
        if ((stat & BMS_PROTECT_STAT_CC_READY) != 0U)
        {
            FML_Protect_HandleCcReady(device, &clear_mask, now_ms);
        }
        /* 前次 finalization 未决的 W1C 永不 replay；同 snapshot 其他新 bit 可独立清除。 */
        clear_mask = (uint8_t)(clear_mask &
                               (uint8_t)(~s_w1c_finalization_ambiguous_mask));

        if (clear_mask != 0U)
        {
            status = BSP_BQ76940_WriteByte(device, BQ76940_REG_SYS_STAT,
                                       clear_mask);
            if (status == BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS)
            {
                /*
                 * containment、diagnostic、quarantine 三者同时执行。BQ 无可见 commit
                 * point 时，只要目标 bit 仍高，软件就不能安全选择 replay 或新事件。
                 */
                FML_Protect_RecordW1cFinalizationAmbiguity(clear_mask);
                FML_Protect_RecordAfeFailure(status, now_ms);
                return BMS_PROTECT_DRAIN_RETRY_REQUIRED;
            }
            if (status != BQ76940_STATUS_OK)
            {
                /* clear 明确失败：保留 pending，后续只重试对应 W1C。 */
                FML_Protect_RecordAfeFailure(status, now_ms);
                return BMS_PROTECT_DRAIN_RETRY_REQUIRED;
            }
            if ((clear_mask & BMS_PROTECT_STAT_CC_READY) != 0U)
            {
                s_cc_clear_pending = false;
            }
        }

        /*
         * XREADY 最后清，且必须先完成外部完整 recovery contract。hook 缺失/失败时
         * 立即释放 mutex 并保留 pending，等待下一次短步骤，绝不持锁 delay。
         */
        if (s_xready_recovery_pending)
        {
            if (!FML_Protect_RecoverXready(device, now_ms))
            {
                return BMS_PROTECT_DRAIN_RETRY_REQUIRED;
            }
        }
        if (s_pending_cc_valid)
        {
            /*
             * 暂存 sample 后必须先返回 APL 完成 queue commit；在 transport ack 前
             * 不能继续读状态或清 CC_READY，否则硬件事件可能先于软件证据退休。
             */
            return BMS_PROTECT_DRAIN_RETRY_REQUIRED;
        }
        /* H-05：重新读取 SYS_STAT，捕获 drain 期间新到达的事件。 */
    }
    return BMS_PROTECT_DRAIN_RETRY_REQUIRED;
}

/* 在总线独占期间推进一次 ALERT drain，并向任务层报告是否仍需重试。 */
BMS_ProtectServiceResult_t FML_Protect_ServicePending(BQ76940_t *device,
                                                      uint32_t now_ms)
{
    /* 本轮 Protect 状态寄存器清理结果。 */
    BMS_ProtectDrainResult_t drain_result;

    if (device == NULL)
    {
        return BMS_PROTECT_SERVICE_RETRY_REQUIRED;
    }
    if (!OS_BusLock(BMS_PROTECT_I2C_TIMEOUT_MS))
    {
        return BMS_PROTECT_SERVICE_RETRY_REQUIRED;
    }

    drain_result = FML_Protect_Drain(device, now_ms);
    OS_BusUnlock();

    if (drain_result != BMS_PROTECT_DRAIN_COMPLETE)
    {
        return BMS_PROTECT_SERVICE_RETRY_REQUIRED;
    }
    return BMS_PROTECT_SERVICE_IDLE;
}

/* 在 ALERT 之外继续推进待决 W1C、通信资格和请求应答。 */
void FML_Protect_ServiceMaintenance(uint32_t now_ms)
{
    FML_Protect_UpdateAfeCommPolicy(now_ms);
    FML_Protect_ServiceHwRecovery(now_ms);
    FML_Protect_ServiceReset(now_ms);
}

#if defined(TEST_PHASE7_IMAGE)
/* 测试镜像暂存一条 CC 样本以覆盖交接边界。 */
bool FML_Protect_TestStageCcSample(int16_t raw, uint32_t sample_ms)
{
    /* 尚未正式发布的暂存值。 */
    bool staged;

    OS_CriticalEnter();
    staged = !s_pending_cc_valid;
    if (staged)
    {
        s_cc_transport_sequence = (uint32_t)(s_cc_transport_sequence + 1UL);
        s_pending_cc.raw = raw;
        s_pending_cc.sample_ms = sample_ms;
        s_pending_cc.xready_generation = s_xready_state.xready_generation;
        s_pending_cc.transport_id = s_cc_transport_sequence;
        s_pending_cc_valid = true;
    }
    OS_CriticalExit();
    return staged;
}
#endif
