#include "bms_balance.h"
#include "bms_balance_evaluate.h"
#include "bms_runtime_port.h"

/*
 * 独立 BalanceTask 是调度器启动后 CELLBAL sole writer。Evaluate 只产生 bitmap；
 * RunOnce 在 I2C mutex 内 write+readback，并在写期间重新核对 sample/revision/
 * generation。任一证据变化或 transport 不确定都尝试 verified all-off，防止旧
 * 高电芯选择跨过 fault、温度变化或 AFE reset 继续均衡。
 */

#include <stddef.h>
#include <string.h>

#include "bq76940_control.h"
#include "bq76940_regs.h"

#define BMS_BALANCE_I2C_TIMEOUT_MS               (20U)

/* 仅 Balance owner 更新的选择、滞回及轮换状态。 */
static BMS_BalanceEngine_t s_engine;
/* 均衡写入与 readback 的权威诊断结果。 */
static BMS_BalanceSnapshot_t s_snapshot;
/* 启动期绑定的 AFE 句柄，仅用于 CELLBAL 事务。 */
static BQ76940_t *s_device;
/* 启动期绑定并经验证的不可变均衡策略。 */
static const BMS_Policy_t *s_policy;

/* 在短临界区发布一次均衡事务结果并推进修订号。 */
static void BMS_Balance_Publish(BMS_BalanceSnapshot_t *snapshot)
{
    BMS_Runtime_CriticalEnter();
    snapshot->publication_revision =
        (uint32_t)(s_snapshot.publication_revision + 1UL);
    s_snapshot = *snapshot;
    BMS_Runtime_CriticalExit();
}

/* 按无符号毫秒差判断当前均衡轮换窗口是否结束。 */
static bool BMS_Balance_TimeElapsed(uint32_t now_ms,
                                    uint32_t started_ms,
                                    uint32_t duration_ms)
{
    return ((uint32_t)(now_ms - started_ms) >= duration_ms);
}

/* 检查用于均衡选择的测量质量、时效和范围。 */
static bool BMS_Balance_MeasurementEligible(
    const BMS_BalancePolicy_t *policy,
    const BMS_DataSnapshot_t *measurement)
{
    uint8_t index;

    if ((measurement->cell_metadata.valid_bitmap != BMS_CELL_DEFINED_MASK) ||
        (measurement->cell_metadata.stale_bitmap != 0U) ||
        !BMS_Data_IsFresh(measurement->current_metadata.valid,
                          measurement->current_metadata.stale_latched,
                          measurement->current_metadata.age_ms,
                          BMS_DATA_CURRENT_FRESH_MAX_MS) ||
        !BMS_Data_IsFresh(measurement->temperature_metadata.valid,
                          measurement->temperature_metadata.stale_latched,
                          measurement->temperature_metadata.age_ms,
                          BMS_DATA_TEMPERATURE_FRESH_MAX_MS) ||
        (measurement->temperature_decic <
         policy->minimum_temperature_decic) ||
        (measurement->temperature_decic >
         policy->maximum_temperature_decic) ||
        (measurement->current_ma > policy->max_abs_current_ma) ||
        (measurement->current_ma < -policy->max_abs_current_ma))
    {
        return false;
    }
    for (index = 0U; index < BMS_CELL_COUNT; ++index)
    {
        if ((measurement->cell_metadata.age_ms[index] >
             BMS_DATA_VOLTAGE_FRESH_MAX_MS) ||
            (measurement->cell_voltage_mv[index] <
             policy->low_voltage_stop_mv))
        {
            return false;
        }
    }
    return true;
}

/* 找出有效测量中的最低电芯及电压，供压差判断。 */
static uint16_t BMS_Balance_MinCell(
    const BMS_DataSnapshot_t *measurement)
{
    uint16_t minimum;
    uint8_t index;

    minimum = measurement->cell_voltage_mv[0];
    for (index = 1U; index < BMS_CELL_COUNT; ++index)
    {
        if (measurement->cell_voltage_mv[index] < minimum)
        {
            minimum = measurement->cell_voltage_mv[index];
        }
    }
    return minimum;
}

/* 检查上轮均衡选择是否仍符合当前测量和策略。 */
static bool BMS_Balance_SelectionCompatible(uint16_t selected,
                                             uint8_t cell,
                                             bool adjacent_permitted)
{
    uint16_t bit;

    if (adjacent_permitted)
    {
        return true;
    }
    bit = (uint16_t)(1U << cell);
    return (((cell == 0U) || ((selected & (uint16_t)(bit >> 1U)) == 0U)) &&
            ((cell == (BMS_CELL_COUNT - 1U)) ||
             ((selected & (uint16_t)(bit << 1U)) == 0U)));
}

/* 仅在测量与安全条件满足时选择均衡电芯，否则请求全关。 */
uint16_t BMS_Balance_Evaluate(
    BMS_BalanceEngine_t *engine,
    const BMS_BalancePolicy_t *policy,
    const BMS_DataSnapshot_t *measurement,
    const BMS_StateSafetySnapshot_t *state,
    const BMS_ProtectSafetySnapshot_t *protect,
    const BMS_RecoverySnapshot_t *recovery,
    uint32_t now_ms)
{
    uint16_t minimum;
    uint16_t selected;
    uint16_t mask;
    uint16_t delta;
    uint8_t offset;
    uint8_t index;
    uint8_t selected_count;
    bool was_active;

    /*
     * 均衡决策漏斗：运行状态许可 -> 完整数据 valid/fresh/in-range -> 无任何方向
     * fault/inhibit -> Recovery complete -> 温度与绝对电流合格 -> 最低单体高于
     * 门限 -> max-min 达到 start/stop delta -> 按 rotation 选择最高候选 -> 检查
     * adjacent 与 max_parallel 规则。任一上游门禁失败立即返回 all-off。
     */
    if ((engine == NULL) || (policy == NULL) || (measurement == NULL) ||
        (state == NULL) || (protect == NULL) || (recovery == NULL) ||
        ((state->state != BMS_STATE_CHARGE) &&
         (state->state != BMS_STATE_STANDBY)) ||
        (state->inhibit_chg_reasons != 0UL) ||
        (state->inhibit_dsg_reasons != 0UL) ||
        (protect->inhibit_chg_reasons != 0UL) ||
        (protect->inhibit_dsg_reasons != 0UL) ||
        recovery->recovery_in_progress || !recovery->technical_ready ||
        !BMS_Balance_MeasurementEligible(policy, measurement))
    {
        if (engine != NULL)
        {
            engine->active_bitmap = 0U;
        }
        return 0U;
    }

    if (!engine->rotation_started)
    {
        engine->rotation_started = true;
        engine->rotation_started_ms = now_ms;
    }
    else if (BMS_Balance_TimeElapsed(now_ms,
                                     engine->rotation_started_ms,
                                     policy->rotation_ms))
    {
        engine->rotation_cursor = (uint8_t)(
            (engine->rotation_cursor + 1U) % BMS_CELL_COUNT);
        engine->rotation_started_ms = now_ms;
    }

    minimum = BMS_Balance_MinCell(measurement);
    selected = 0U;
    selected_count = 0U;
    for (offset = 0U; offset < BMS_CELL_COUNT; ++offset)
    {
        index = (uint8_t)((engine->rotation_cursor + offset) %
                          BMS_CELL_COUNT);
        mask = (uint16_t)(1U << index);
        /*
         * 已经 active 的电芯用较小 stop_delta，新的候选用较大 start_delta。
         * start_delta > stop_delta 形成 hysteresis，防止均衡电流造成的毫伏波动
         * 在阈值附近每秒启停。
         */
        delta = (uint16_t)(measurement->cell_voltage_mv[index] - minimum);
        was_active = (engine->active_bitmap & mask) != 0U;
        if ((measurement->cell_voltage_mv[index] >=
             policy->minimum_cell_mv) &&
            (delta >= (was_active ? policy->stop_delta_mv :
                                    policy->start_delta_mv)) &&
            BMS_Balance_SelectionCompatible(
                selected, index, policy->adjacent_cells_permitted))
        {
            selected |= mask;
            ++selected_count;
            if (selected_count >= policy->max_parallel_cells)
            {
                break;
            }
        }
    }
    engine->active_bitmap = selected;
    return selected;
}

/* 在总线锁内写入三个 CELLBAL 字节并全量回读确认。 */
static BQ76940_Status_t BMS_Balance_WriteAndVerifyLocked(
    uint8_t bal1, uint8_t bal2, uint8_t bal3,
    uint16_t *confirmed_bitmap)
{
    BQ76940_Status_t status;
    uint8_t actual1;
    uint8_t actual2;
    uint8_t actual3;

    /*
     * 调用者持有 bus transaction guard。三个 CELLBAL 寄存器共同表达一个逻辑 bitmap，必须
     * 全部写完再全部回读；只确认其中一个字节会把部分更新误报成成功。
     */
    status = BQ76940_WriteByte(s_device, BQ76940_REG_CELLBAL1, bal1);
    if (status == BQ76940_STATUS_OK)
    {
        status = BQ76940_WriteByte(s_device, BQ76940_REG_CELLBAL2, bal2);
    }
    if (status == BQ76940_STATUS_OK)
    {
        status = BQ76940_WriteByte(s_device, BQ76940_REG_CELLBAL3, bal3);
    }
    if (status == BQ76940_STATUS_OK)
    {
        status = BQ76940_ReadByte(s_device, BQ76940_REG_CELLBAL1, &actual1);
    }
    if (status == BQ76940_STATUS_OK)
    {
        status = BQ76940_ReadByte(s_device, BQ76940_REG_CELLBAL2, &actual2);
    }
    if (status == BQ76940_STATUS_OK)
    {
        status = BQ76940_ReadByte(s_device, BQ76940_REG_CELLBAL3, &actual3);
    }
    if ((status == BQ76940_STATUS_OK) &&
        ((actual1 != bal1) || (actual2 != bal2) || (actual3 != bal3)))
    {
        status = BQ76940_STATUS_RANGE_ERROR;
    }
    if ((status == BQ76940_STATUS_OK) && (confirmed_bitmap != NULL))
    {
        *confirmed_bitmap = BQ76940_Control_DecodeCellBal(
            actual1, actual2, actual3);
    }
    return status;
}

/* 在失败路径尽力写入并回读均衡全关状态。 */
static void BMS_Balance_AttemptAllOffLocked(void)
{
    uint16_t ignored;

    (void)BMS_Balance_WriteAndVerifyLocked(0U, 0U, 0U, &ignored);
}

/* 绑定 AFE 与策略，建立均衡全关的启动证据。 */
void BMS_Balance_Init(BQ76940_t *device,
                      const BMS_Policy_t *policy,
                      bool startup_all_off_confirmed)
{
    (void)memset(&s_engine, 0, sizeof(s_engine));
    (void)memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_device = device;
    s_policy = BMS_Policy_Validate(policy) ? policy : NULL;
    s_snapshot.last_transport_status = BQ76940_STATUS_OK;
    s_snapshot.register_state_confirmed = startup_all_off_confirmed;
    s_snapshot.confirmed_all_off = startup_all_off_confirmed;
}

/* 用当前测量与安全快照选择均衡位，写入并回读 CELLBAL 后发布结果。 */
void BMS_Balance_RunOnce(uint32_t now_ms)
{
    BMS_DataSnapshot_t measurement;
    BMS_DataIdentity_t identity;
    BMS_StateSafetySnapshot_t state;
    BMS_StateSafetySnapshot_t state_after;
    BMS_ProtectSafetySnapshot_t protect;
    BMS_ProtectSafetySnapshot_t protect_after;
    BMS_RecoverySnapshot_t recovery;
    BMS_RecoverySnapshot_t recovery_after;
    BMS_BalanceSnapshot_t next;
    BQ76940_Status_t status;
    uint16_t desired;
    uint16_t confirmed;
    uint8_t bal1;
    uint8_t bal2;
    uint8_t bal3;
    bool identities_current;

    /*
     * 先捕获 measurement 与三份安全 owner 快照并计算 desired；取得 I2C mutex 后
     * 再比较 identity/revision，写后又比较一次。这样采样、fault 或 XREADY 在
     * transaction 中途变化时，旧选择不会被提交；失败路径尽力写入并验证 all-off。
     */
    if ((s_policy == NULL) || (s_device == NULL) ||
        !BMS_Data_GetSnapshot(&measurement, now_ms))
    {
        return;
    }
    state = BMS_State_GetSafetySnapshot();
    protect = BMS_Protect_GetSafetySnapshot();
    recovery = BMS_Recovery_GetSnapshot();
    next = BMS_Balance_GetSnapshot();
    desired = BMS_Balance_Evaluate(
        &s_engine, &s_policy->balance, &measurement,
        &state, &protect, &recovery, now_ms);
    next.requested_bitmap = desired;
    next.evaluated_sample_sequence = measurement.sample_sequence;
    next.evaluated_afe_generation = measurement.afe_generation;

    if (!BQ76940_Control_ComposeCellBalPolicy(
            desired, s_policy->balance.max_parallel_cells,
            s_policy->balance.adjacent_cells_permitted,
            &bal1, &bal2, &bal3) ||
        !BMS_Runtime_BusLock(BMS_BALANCE_I2C_TIMEOUT_MS))
    {
        next.last_transport_status = BQ76940_STATUS_I2C_TIMEOUT;
        next.register_state_confirmed = false;
        next.confirmed_all_off = false;
        BMS_Balance_Publish(&next);
        return;
    }

    identities_current = BMS_Data_GetIdentity(&identity) &&
        (identity.sample_sequence == measurement.sample_sequence) &&
        (identity.afe_generation == measurement.afe_generation);
    state_after = BMS_State_GetSafetySnapshot();
    protect_after = BMS_Protect_GetSafetySnapshot();
    recovery_after = BMS_Recovery_GetSnapshot();
    identities_current = identities_current &&
        (state_after.publication_revision == state.publication_revision) &&
        (protect_after.publication_revision == protect.publication_revision) &&
        (recovery_after.publication_revision ==
         recovery.publication_revision);
    if (!identities_current)
    {
        bal1 = 0U;
        bal2 = 0U;
        bal3 = 0U;
        desired = 0U;
    }

    confirmed = 0U;
    status = BMS_Balance_WriteAndVerifyLocked(
        bal1, bal2, bal3, &confirmed);
    state_after = BMS_State_GetSafetySnapshot();
    protect_after = BMS_Protect_GetSafetySnapshot();
    recovery_after = BMS_Recovery_GetSnapshot();
    if ((status != BQ76940_STATUS_OK) ||
        (state_after.publication_revision != state.publication_revision) ||
        (protect_after.publication_revision != protect.publication_revision) ||
        (recovery_after.publication_revision !=
         recovery.publication_revision))
    {
        BMS_Balance_AttemptAllOffLocked();
        status = status == BQ76940_STATUS_OK ?
            BQ76940_STATUS_RANGE_ERROR : status;
        confirmed = 0U;
        next.register_state_confirmed = false;
        next.confirmed_all_off = false;
    }
    else
    {
        next.register_state_confirmed = true;
        next.confirmed_all_off = (confirmed == 0U);
    }
    BMS_Runtime_BusUnlock();
    next.requested_bitmap = desired;
    next.confirmed_bitmap = confirmed;
    next.confirmed_afe_generation = protect_after.xready_generation;
    next.last_transport_status = status;
    BMS_Balance_Publish(&next);
}

/* 在短临界区复制均衡目标和 CELLBAL 回读结果。 */
BMS_BalanceSnapshot_t BMS_Balance_GetSnapshot(void)
{
    BMS_BalanceSnapshot_t snapshot;

    BMS_Runtime_CriticalEnter();
    snapshot = s_snapshot;
    BMS_Runtime_CriticalExit();
    return snapshot;
}
