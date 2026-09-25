#include "fml_fet_manager.h"
#include "os_runtime.h"

/*
 * FET transaction 采用 capture revisions → compose requested/effective →
 * read current → write expected → readback → confirm revisions 的闭环。
 * enable 比 disable 更严格：任一安全输入在 transaction 中途变化，都会尝试
 * safe-off；写入 finalization 或 readback 不明确则 quarantine，禁止旧 enable
 * 在无法证明状态时继续传播。
 */

#include <stddef.h>

#include "fml_data.h"
#include "fml_protect.h"
#include "bsp_bq76940_regs.h"

#define BMS_FET_MANAGER_I2C_TIMEOUT_MS          (20U)

/* 启动期绑定的 AFE 句柄；仅 StateTask 服务此模块，句柄覆盖任务生命周期。 */
static BQ76940_t *s_afe_device;
/* FET 事务的权威结果与诊断快照；只在本模块更新。 */
static BMS_FetManagerSnapshot_t s_fet_snapshot;

#if defined(TEST_PHASE9_IMAGE)
/* 在 SYS_CTRL2 首次读取后、可能的写入前注入并发安全变化。 */
static BMS_FetManagerTestHook_t s_after_read_hook;
/* 在 SYS_CTRL2 写入后、回读前注入并发安全变化。 */
static BMS_FetManagerTestHook_t s_after_write_hook;
#endif

/* 在一次事务结果发布时推进快照版本，供消费者判断是否发生变化。 */
static void FML_FetManager_Publish(void)
{
    s_fet_snapshot.publication_revision =
        (uint32_t)(s_fet_snapshot.publication_revision + 1UL);
}

/* 判断合成后的方向请求是否包含任何一次新的 MOS 使能。 */
static bool FML_FetManager_RequestHasEnable(
    const BQ76940_FetRequest_t *request)
{
    return (request->chg == BQ76940_FET_DESIRE_ENABLE) ||
        (request->dsg == BQ76940_FET_DESIRE_ENABLE);
}

/* 复核三位安全 owner 的版本和 State 所依据的测量身份仍是当前值。 */
static bool FML_FetManager_SnapshotsStillCurrent(
    const BMS_ProtectSafetySnapshot_t *protect,
    const BMS_StateSafetySnapshot_t *state,
    const BMS_RecoverySnapshot_t *recovery)
{
    /* Protect owner 当前发布的安全限制。 */
    BMS_ProtectSafetySnapshot_t current_protect;
    /* State owner 当前发布的安全状态。 */
    BMS_StateSafetySnapshot_t current_state;
    /* 恢复协调器当前发布的阶段状态。 */
    BMS_RecoverySnapshot_t current_recovery;
    /* 当前测量的序列和 AFE 代身份。 */
    BMS_DataIdentity_t identity;

    current_protect = FML_Protect_GetSafetySnapshot();
    current_state = FML_State_GetSafetySnapshot();
    current_recovery = FML_Recovery_GetSnapshot();
    return FML_Data_GetIdentity(&identity) &&
        (current_protect.publication_revision ==
         protect->publication_revision) &&
        (current_state.publication_revision ==
         state->publication_revision) &&
        (current_recovery.publication_revision ==
         recovery->publication_revision) &&
        (identity.sample_sequence ==
         state->evaluated_sample_sequence) &&
        (identity.afe_generation ==
         state->evaluated_afe_generation);
}

/* 合并运行意图与三个安全 owner 的方向性禁止，形成唯一可下发请求。 */
static void FML_FetManager_ComposeEffective(
    const BMS_ProtectSafetySnapshot_t *protect,
    const BMS_StateSafetySnapshot_t *state,
    const BMS_RecoverySnapshot_t *recovery,
    BQ76940_FetRequest_t *effective)
{
    /*
     * requested 是 State 对运行方向的“希望”；Protect/State/Recovery 发布的
     * inhibit 是不可绕过的安全否决；effective 才是允许送入寄存器合成器的目标：
     *
     * requested + directional inhibits + recovery readiness + quarantine
     *     -> effective
     *
     * 因此 State=CHARGE 并不等于 CHG 位必然打开，任何 owner 的充电 inhibit 都
     * 会把 effective.chg 压回 DISABLE，且不影响另一个方向的独立仲裁。
     */
    s_fet_snapshot.requested = state->operational_intent;
    s_fet_snapshot.inhibit_chg_reasons =
        protect->inhibit_chg_reasons |
        state->inhibit_chg_reasons |
        recovery->inhibit_chg_reasons;
    s_fet_snapshot.inhibit_dsg_reasons =
        protect->inhibit_dsg_reasons |
        state->inhibit_dsg_reasons |
        recovery->inhibit_dsg_reasons;
    if (!recovery->technical_ready)
    {
        s_fet_snapshot.inhibit_chg_reasons |= BMS_INHIBIT_REASON_RECOVERY;
        s_fet_snapshot.inhibit_dsg_reasons |= BMS_INHIBIT_REASON_RECOVERY;
    }
    /*
     * quarantine 不是普通业务 fault，而是“软件无法证明上一次寄存器事务最终
     * 状态”。在重新获得可证明的 safe-off 之前，新的 enable 一律被技术性禁止。
     */
    if (s_fet_snapshot.transaction_state ==
        BMS_FET_TRANSACTION_QUARANTINED)
    {
        if (FML_FetManager_RequestHasEnable(&s_fet_snapshot.requested))
        {
            s_fet_snapshot.enable_denied_by_quarantine = true;
        }
        s_fet_snapshot.inhibit_chg_reasons |=
            BMS_INHIBIT_REASON_FET_UNVERIFIED;
        s_fet_snapshot.inhibit_dsg_reasons |=
            BMS_INHIBIT_REASON_FET_UNVERIFIED;
    }
    BSP_BQ76940_Control_ApplyInhibits(
        &s_fet_snapshot.requested,
        s_fet_snapshot.inhibit_chg_reasons != 0UL,
        s_fet_snapshot.inhibit_dsg_reasons != 0UL,
        effective);
}

/* 调用者持有总线锁；尽力写入并回读全关状态，失败时不宣称安全已确认。 */
static bool FML_FetManager_AttemptSafeOffLocked(void)
{
    /* 安全降级时要求关闭两路 FET 的命令。 */
    BQ76940_FetRequest_t safe_off;
    /* FET 命令写入或读回的硬件状态。 */
    BQ76940_Status_t status;
    /* 当前计算或读取的值。 */
    uint8_t current;
    /* 用于核对的预期值。 */
    uint8_t expected;
    /* 读取到的实际值。 */
    uint8_t actual;

    /*
     * 调用者已持有 runtime bus port。safe-off 仍执行 read -> compose -> write -> readback，
     * 而不是盲写常量，因为 SYS_CTRL2 中 CC_EN 等非 FET 位也属于完整寄存器契约。
     * 只有 readback 全字节一致，manager 才能声称“已确认安全关断”。
     */
    safe_off.chg = BQ76940_FET_DESIRE_DISABLE;
    safe_off.dsg = BQ76940_FET_DESIRE_DISABLE;
    current = 0U;
    status = BSP_BQ76940_ReadByte(s_afe_device, BQ76940_REG_SYS_CTRL2, &current);
    if (status != BQ76940_STATUS_OK)
    {
        s_fet_snapshot.last_transport_status = status;
        s_fet_snapshot.register_state_confirmed = false;
        return false;
    }
    expected = BSP_BQ76940_Control_SysCtrl2WithFets(current, &safe_off);
    /* manager 持有调度期完整 SYS_CTRL2 composition；safe-off 与运行态都保留 CC_EN。 */
    expected |= BQ76940_SYS_CTRL2_CC_EN_MASK;
    status = BSP_BQ76940_WriteByte(s_afe_device, BQ76940_REG_SYS_CTRL2, expected);
    if (status != BQ76940_STATUS_OK)
    {
        s_fet_snapshot.last_transport_status = status;
        s_fet_snapshot.register_state_confirmed = false;
        return false;
    }
    actual = 0U;
    status = BSP_BQ76940_ReadByte(s_afe_device, BQ76940_REG_SYS_CTRL2, &actual);
    s_fet_snapshot.expected_sys_ctrl2 = expected;
    s_fet_snapshot.observed_sys_ctrl2 = actual;
    s_fet_snapshot.last_transport_status = status;
    if ((status != BQ76940_STATUS_OK) || (actual != expected))
    {
        s_fet_snapshot.register_state_confirmed = false;
        return false;
    }
    BSP_BQ76940_Control_ObserveFets(actual, &s_fet_snapshot.observed);
    s_fet_snapshot.register_state_confirmed = true;
    s_fet_snapshot.effective = safe_off;
    return true;
}

/* 绑定 AFE 并建立未验证、双向禁止的初始事务状态。 */
void FML_FetManager_Init(BQ76940_t *device)
{
    s_afe_device = device;
    s_fet_snapshot.transaction_state = BMS_FET_TRANSACTION_UNVERIFIED;
    s_fet_snapshot.requested.chg = BQ76940_FET_DESIRE_DISABLE;
    s_fet_snapshot.requested.dsg = BQ76940_FET_DESIRE_DISABLE;
    s_fet_snapshot.effective = s_fet_snapshot.requested;
    s_fet_snapshot.observed.chg_on = false;
    s_fet_snapshot.observed.dsg_on = false;
    s_fet_snapshot.inhibit_chg_reasons = BMS_INHIBIT_REASON_FET_UNVERIFIED;
    s_fet_snapshot.inhibit_dsg_reasons = BMS_INHIBIT_REASON_FET_UNVERIFIED;
    s_fet_snapshot.publication_revision = 0UL;
    s_fet_snapshot.protect_revision = 0UL;
    s_fet_snapshot.state_revision = 0UL;
    s_fet_snapshot.recovery_revision = 0UL;
    s_fet_snapshot.expected_sys_ctrl2 = 0U;
    s_fet_snapshot.observed_sys_ctrl2 = 0U;
    s_fet_snapshot.last_transport_status = BQ76940_STATUS_NOT_INITIALIZED;
    s_fet_snapshot.register_state_confirmed = false;
    s_fet_snapshot.enable_denied_by_quarantine = false;
#if defined(TEST_PHASE9_IMAGE)
    s_after_read_hook = NULL;
    s_after_write_hook = NULL;
#endif
}

/* 仅供已持总线锁的服务路径调用；统一释放总线并发布本次事务结果。 */
static void FML_FetManager_FinishLocked(void)
{
    OS_BusUnlock();
    FML_FetManager_Publish();
}

/* 在 StateTask 上下文完成一次捕获、写入、回读和版本复核的 FET 事务。 */
void FML_FetManager_Service(void)
{
    /* Protect owner 发布的安全快照。 */
    BMS_ProtectSafetySnapshot_t protect;
    /* 当前状态或 State owner 的安全快照。 */
    BMS_StateSafetySnapshot_t state;
    /* 恢复协调器发布的阶段快照。 */
    BMS_RecoverySnapshot_t recovery;
    /* 仲裁后实际提交给 AFE 的 FET 请求。 */
    BQ76940_FetRequest_t effective;
    /* FET 命令写入或读回的硬件状态。 */
    BQ76940_Status_t status;
    /* 当前计算或读取的值。 */
    uint8_t current;
    /* 用于核对的预期值。 */
    uint8_t expected;
    /* 读取到的实际值。 */
    uint8_t actual;
    /* 本轮 FET 请求是否包含从关到开的动作。 */
    bool enabling;
    /* 硬件控制失败后是否需要隔离 FET。 */
    bool quarantined;

    /*
     * 一次完整 transaction：
     *
     * Capture Protect/State/Recovery snapshots
     *   -> Compose requested/effective
     *   -> Read SYS_CTRL2
     *   -> Compose expected（只改 CHG/DSG，并保持 CC_EN）
     *   -> Write（需要时）
     *   -> Readback full byte
     *   -> Confirm all revisions + measurement identity
     *   -> Commit APPLIED/SAFE，或进入 UNVERIFIED/QUARANTINED
     *
     * BMS_Data 是诊断聚合，不参与仲裁；三份 owner snapshot 才是安全输入。
     */
    protect = FML_Protect_GetSafetySnapshot();
    state = FML_State_GetSafetySnapshot();
    recovery = FML_Recovery_GetSnapshot();
    quarantined = s_fet_snapshot.transaction_state ==
        BMS_FET_TRANSACTION_QUARANTINED;
    FML_FetManager_ComposeEffective(&protect, &state, &recovery, &effective);
    enabling = FML_FetManager_RequestHasEnable(&effective);
    s_fet_snapshot.protect_revision = protect.publication_revision;
    s_fet_snapshot.state_revision = state.publication_revision;
    s_fet_snapshot.recovery_revision = recovery.publication_revision;
    s_fet_snapshot.effective = effective;
    s_fet_snapshot.register_state_confirmed = false;

    if ((s_afe_device == NULL) ||
        !OS_BusLock(BMS_FET_MANAGER_I2C_TIMEOUT_MS))
    {
        if (!quarantined)
        {
            s_fet_snapshot.transaction_state = BMS_FET_TRANSACTION_UNVERIFIED;
        }
        s_fet_snapshot.last_transport_status = BQ76940_STATUS_I2C_TIMEOUT;
        FML_FetManager_Publish();
        return;
    }

    /*
     * enable 比 disable 更严格：关断即使依据变旧通常仍是保守动作；打开 MOS 则
     * 必须证明从捕获快照到真正写寄存器期间没有新 fault、恢复变化或新测量。
     */
    if (enabling &&
        !FML_FetManager_SnapshotsStillCurrent(&protect, &state, &recovery))
    {
        (void)FML_FetManager_AttemptSafeOffLocked();
        if (!quarantined)
        {
            s_fet_snapshot.transaction_state =
                s_fet_snapshot.register_state_confirmed ?
                BMS_FET_TRANSACTION_CONFIRMED_SAFE :
                BMS_FET_TRANSACTION_UNVERIFIED;
        }
        FML_FetManager_FinishLocked();
        return;
    }

    current = 0U;
    status = BSP_BQ76940_ReadByte(s_afe_device, BQ76940_REG_SYS_CTRL2, &current);
    s_fet_snapshot.last_transport_status = status;
    if (status != BQ76940_STATUS_OK)
    {
        if (!quarantined)
        {
            s_fet_snapshot.transaction_state = BMS_FET_TRANSACTION_UNVERIFIED;
        }
        FML_FetManager_FinishLocked();
        return;
    }
#if defined(TEST_PHASE9_IMAGE)
    if (s_after_read_hook != NULL)
    {
        s_after_read_hook();
    }
#endif
    if (enabling &&
        (((current & BQ76940_SYS_CTRL2_CC_EN_MASK) == 0U) ||
         !FML_FetManager_SnapshotsStillCurrent(&protect, &state, &recovery)))
    {
        (void)FML_FetManager_AttemptSafeOffLocked();
        if (!quarantined)
        {
            s_fet_snapshot.transaction_state =
                s_fet_snapshot.register_state_confirmed ?
                BMS_FET_TRANSACTION_CONFIRMED_SAFE :
                BMS_FET_TRANSACTION_UNVERIFIED;
        }
        FML_FetManager_FinishLocked();
        return;
    }

    /* 仅替换 owner 管理的 CHG/DSG 位，并显式保持 CC_EN。 */
    expected = BSP_BQ76940_Control_SysCtrl2WithFets(current, &effective);
    expected |= BQ76940_SYS_CTRL2_CC_EN_MASK;
    s_fet_snapshot.expected_sys_ctrl2 = expected;
    if (current != expected)
    {
        status = BSP_BQ76940_WriteByte(s_afe_device, BQ76940_REG_SYS_CTRL2, expected);
        s_fet_snapshot.last_transport_status = status;
        if (status == BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS)
        {
            /*
             * enable 写入的数据可能已被 AFE 接收，但 STOP 失败使提交点未知。
             * 此时既不能声称打开成功，也不能简单重放；进入 quarantine，并尽力
             * 执行可验证 safe-off。disable 不确定仍记 UNVERIFIED，但不扩大权限。
             */
            if (enabling)
            {
                s_fet_snapshot.transaction_state =
                    BMS_FET_TRANSACTION_QUARANTINED;
                (void)FML_FetManager_AttemptSafeOffLocked();
                s_fet_snapshot.last_transport_status =
                    BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS;
            }
            else if (!quarantined)
            {
                s_fet_snapshot.transaction_state =
                    BMS_FET_TRANSACTION_UNVERIFIED;
            }
            FML_FetManager_FinishLocked();
            return;
        }
        if (status != BQ76940_STATUS_OK)
        {
            if (!quarantined)
            {
                s_fet_snapshot.transaction_state =
                    BMS_FET_TRANSACTION_UNVERIFIED;
            }
            FML_FetManager_FinishLocked();
            return;
        }
    }
#if defined(TEST_PHASE9_IMAGE)
    if (s_after_write_hook != NULL)
    {
        s_after_write_hook();
    }
#endif
    /* write 返回成功仍不足以证明结果；必须重新 readback 完整 SYS_CTRL2。 */
    actual = 0U;
    status = BSP_BQ76940_ReadByte(s_afe_device, BQ76940_REG_SYS_CTRL2, &actual);
    s_fet_snapshot.observed_sys_ctrl2 = actual;
    s_fet_snapshot.last_transport_status = status;
    if ((status != BQ76940_STATUS_OK) || (actual != expected))
    {
        if (enabling)
        {
            s_fet_snapshot.transaction_state = BMS_FET_TRANSACTION_QUARANTINED;
            (void)FML_FetManager_AttemptSafeOffLocked();
        }
        else if (!quarantined)
        {
            s_fet_snapshot.transaction_state = BMS_FET_TRANSACTION_UNVERIFIED;
        }
        FML_FetManager_FinishLocked();
        return;
    }
    BSP_BQ76940_Control_ObserveFets(actual, &s_fet_snapshot.observed);
    s_fet_snapshot.register_state_confirmed = true;

    /*
     * readback 成功后再次确认 revisions。若写寄存器期间新 fault 到达，旧 enable
     * 即使已经写入也不能 commit 为 applied，必须立刻回到 verified safe-off。
     */
    if (enabling &&
        !FML_FetManager_SnapshotsStillCurrent(&protect, &state, &recovery))
    {
        (void)FML_FetManager_AttemptSafeOffLocked();
        if (!quarantined)
        {
            s_fet_snapshot.transaction_state =
                s_fet_snapshot.register_state_confirmed ?
                BMS_FET_TRANSACTION_CONFIRMED_SAFE :
                BMS_FET_TRANSACTION_UNVERIFIED;
        }
    }
    else if (!quarantined)
    {
        s_fet_snapshot.transaction_state = enabling ?
            BMS_FET_TRANSACTION_CONFIRMED_APPLIED :
            BMS_FET_TRANSACTION_CONFIRMED_SAFE;
    }
    FML_FetManager_FinishLocked();
}

/* 一次复制 manager 的只读事务快照，避免消费者读到撕裂的字段。 */
BMS_FetManagerSnapshot_t FML_FetManager_GetSnapshot(void)
{
    /* 本次读取的一致状态快照。 */
    BMS_FetManagerSnapshot_t snapshot;

    OS_CriticalEnter();
    snapshot = s_fet_snapshot;
    OS_CriticalExit();
    return snapshot;
}

#if defined(TEST_PHASE9_IMAGE)
/* 测试镜像设置寄存器读取后的竞态注入点。 */
void FML_FetManager_TestSetAfterReadHook(BMS_FetManagerTestHook_t hook)
{
    s_after_read_hook = hook;
}

/* 测试镜像设置寄存器写入后的竞态注入点。 */
void FML_FetManager_TestSetAfterWriteHook(BMS_FetManagerTestHook_t hook)
{
    s_after_write_hook = hook;
}
#endif
