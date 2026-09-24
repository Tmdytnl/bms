#include "bms_state.h"
#include "bms_runtime_port.h"

/*
 * State owner 只负责软件保护、DATA_STALE、RTOS_HEALTH 与运行状态分类。
 * 每次 decision 绑定同一 sample_sequence/afe_generation，并在发布前比较当前
 * identity；FET Manager 读取 directional inhibit，而不是把 FAULT 直接翻译成
 * CHG/DSG 双关断命令，从而保留充电/放电方向不同的保护动作。
 */

#include <stddef.h>

#include "bms_data.h"

/* State owner 独占的软件保护与分类资格状态。 */
static BMS_StateEngine_t s_engine;
/* State 发布的方向性禁止与运行状态权威快照。 */
static BMS_StateSafetySnapshot_t s_snapshot;
/* 启动期绑定的不可变状态和保护策略。 */
static const BMS_Policy_t *s_policy;

#if defined(TEST_PHASE9_IMAGE)
/* 测试镜像在 State 发布前注入测量竞态的回调。 */
static BMS_StatePrePublishHook_t s_pre_publish_hook;
#endif

/* 按回绕安全的毫秒差判断状态资格窗口到期。 */
static bool BMS_State_TimeElapsed(uint32_t now_ms,
                                  uint32_t started_ms,
                                  uint32_t duration_ms)
{
    return ((uint32_t)(now_ms - started_ms) >= duration_ms);
}

/* 清空候选条件的连续资格计时与锁存证据。 */
static void BMS_State_ResetTracking(BMS_StateCondition_t *condition)
{
    condition->assert_tracking = false;
    condition->recovery_tracking = false;
}

/* 按当前条件更新连续资格计时并报告是否达标。 */
static void BMS_State_UpdateCondition(BMS_StateCondition_t *condition,
                                      bool assert_condition,
                                      bool recovery_condition,
                                      uint32_t debounce_ms,
                                      uint32_t recovery_ms,
                                      uint32_t now_ms)
{
    /*
     * 同一个保护源使用两条相互独立的连续时间证据：
     *
     * normal --触发条件持续 debounce_ms--> active
     * active --跨回 hysteresis 且持续 recovery_ms--> normal
     *
     * 触发门限与恢复门限不同形成 hysteresis，避免噪声在单一阈值附近反复置位/
     * 清除。任何一次条件中断都清除对应 tracking，因为断续样本不能拼成一段
     * 连续证据；触发阶段也不能借用之前的恢复计时，反之亦然。
     */
    if (!condition->active)
    {
        condition->recovery_tracking = false;
        if (!assert_condition)
        {
            condition->assert_tracking = false;
        }
        else if (!condition->assert_tracking)
        {
            condition->assert_tracking = true;
            condition->assert_started_ms = now_ms;
        }
        else if (BMS_State_TimeElapsed(now_ms,
                                       condition->assert_started_ms,
                                       debounce_ms))
        {
            condition->active = true;
            condition->assert_tracking = false;
        }
    }
    else
    {
        condition->assert_tracking = false;
        if (!recovery_condition)
        {
            condition->recovery_tracking = false;
        }
        else if (!condition->recovery_tracking)
        {
            condition->recovery_tracking = true;
            condition->recovery_started_ms = now_ms;
        }
        else if (BMS_State_TimeElapsed(now_ms,
                                       condition->recovery_started_ms,
                                       recovery_ms))
        {
            condition->active = false;
            condition->recovery_tracking = false;
        }
    }
}

/* 确认全部必需电芯测量在当前时效窗口内。 */
static bool BMS_State_CellCoreIsFresh(const BMS_DataSnapshot_t *measurement)
{
    uint32_t index;

    if ((measurement->cell_metadata.valid_bitmap != BMS_CELL_DEFINED_MASK) ||
        (measurement->cell_metadata.in_range_bitmap != BMS_CELL_DEFINED_MASK) ||
        (measurement->cell_metadata.stale_bitmap != 0U) ||
        !BMS_Data_IsFresh(measurement->pack_metadata.valid,
                          measurement->pack_metadata.stale_latched,
                          measurement->pack_metadata.age_ms,
                          BMS_DATA_VOLTAGE_FRESH_MAX_MS))
    {
        return false;
    }
    for (index = 0UL; index < (uint32_t)BMS_CELL_COUNT; ++index)
    {
        if (measurement->cell_metadata.age_ms[index] >
            BMS_DATA_VOLTAGE_FRESH_MAX_MS)
        {
            return false;
        }
    }
    return true;
}

/* 确认软件保护所需的电压、电流和温度证据均新鲜。 */
static bool BMS_State_AllSafetyMeasurementsFresh(
    const BMS_DataSnapshot_t *measurement)
{
    return BMS_State_CellCoreIsFresh(measurement) &&
        BMS_Data_IsFresh(measurement->current_metadata.valid,
                         measurement->current_metadata.stale_latched,
                         measurement->current_metadata.age_ms,
                         BMS_DATA_CURRENT_FRESH_MAX_MS) &&
        BMS_Data_IsFresh(measurement->temperature_metadata.valid,
                         measurement->temperature_metadata.stale_latched,
                         measurement->temperature_metadata.age_ms,
                         BMS_DATA_TEMPERATURE_FRESH_MAX_MS);
}

/* 把测量年龄和有效位映射为过期故障状态。 */
static void BMS_State_UpdateDataStale(BMS_StateEngine_t *engine,
                                      const BMS_Policy_t *policy,
                                      const BMS_DataSnapshot_t *measurement)
{
    bool fresh;

    /*
     * cell、pack、current、temperature 都参与安全判断，任一组无效、越界或过期，
     * 继续沿用旧判断都可能允许已经不安全的方向，因此 DATA_STALE 立即双向禁止。
     * 恢复时只接受 sample_sequence 不同的完整 fresh 帧；反复读同一帧不能增加
     * 证据。要求连续多帧可过滤“偶尔成功一帧后又失联”的短暂恢复。
     */
    fresh = BMS_State_AllSafetyMeasurementsFresh(measurement);
    if (!fresh)
    {
        engine->data_stale_active = true;
        engine->fresh_frame_count = 0U;
        engine->last_fresh_sequence = measurement->sample_sequence;
        return;
    }
    if (measurement->sample_sequence != engine->last_fresh_sequence)
    {
        engine->last_fresh_sequence = measurement->sample_sequence;
        if (engine->fresh_frame_count < UINT8_MAX)
        {
            ++engine->fresh_frame_count;
        }
    }
    if (engine->fresh_frame_count >= policy->freshness.recovery_fresh_frames)
    {
        engine->data_stale_active = false;
    }
}

/* 按电压、电流与温度条件维护软件保护故障。 */
static void BMS_State_UpdateSoftwareProtection(
    BMS_StateEngine_t *engine,
    const BMS_Policy_t *policy,
    const BMS_DataSnapshot_t *measurement,
    uint32_t now_ms)
{
    uint32_t index;
    uint16_t minimum_cell_mv;
    uint16_t maximum_cell_mv;
    int32_t current_ma;
    int16_t temperature_decic;

    /*
     * OV 看最高单体、UV 看最低单体；电流符号区分充/放方向，温度则分别使用
     * 充电与放电策略。这里只在上层确认所有安全 measurement fresh 后运行，
     * 所以不会用过期的极值刷新 debounce/recovery 计时器。
     */
    minimum_cell_mv = measurement->cell_voltage_mv[0];
    maximum_cell_mv = measurement->cell_voltage_mv[0];
    for (index = 1UL; index < (uint32_t)BMS_CELL_COUNT; ++index)
    {
        if (minimum_cell_mv > measurement->cell_voltage_mv[index])
        {
            minimum_cell_mv = measurement->cell_voltage_mv[index];
        }
        if (maximum_cell_mv < measurement->cell_voltage_mv[index])
        {
            maximum_cell_mv = measurement->cell_voltage_mv[index];
        }
    }
    current_ma = measurement->current_ma;
    temperature_decic = measurement->temperature_decic;

    BMS_State_UpdateCondition(&engine->sw_ov,
        maximum_cell_mv >= (uint16_t)policy->sw_ov.trigger,
        maximum_cell_mv <= (uint16_t)policy->sw_ov.recovery,
        policy->sw_ov.debounce_ms, policy->sw_ov.recovery_qualify_ms,
        now_ms);
    BMS_State_UpdateCondition(&engine->sw_uv,
        minimum_cell_mv <= (uint16_t)policy->sw_uv.trigger,
        minimum_cell_mv >= (uint16_t)policy->sw_uv.recovery,
        policy->sw_uv.debounce_ms, policy->sw_uv.recovery_qualify_ms,
        now_ms);
    BMS_State_UpdateCondition(&engine->sw_oc_charge,
        current_ma >= policy->sw_oc_charge.trigger,
        current_ma < policy->sw_oc_charge.recovery,
        policy->sw_oc_charge.debounce_ms,
        policy->sw_oc_charge.recovery_qualify_ms, now_ms);
    BMS_State_UpdateCondition(&engine->sw_oc_discharge,
        current_ma <= policy->sw_oc_discharge.trigger,
        current_ma > policy->sw_oc_discharge.recovery,
        policy->sw_oc_discharge.debounce_ms,
        policy->sw_oc_discharge.recovery_qualify_ms, now_ms);

    BMS_State_UpdateCondition(&engine->charge_temp_low,
        temperature_decic <= policy->charge_temperature.low_trigger_decic,
        temperature_decic >= policy->charge_temperature.low_recovery_decic,
        policy->charge_temperature.debounce_ms,
        policy->charge_temperature.recovery_qualify_ms, now_ms);
    BMS_State_UpdateCondition(&engine->charge_temp_high,
        temperature_decic >= policy->charge_temperature.high_trigger_decic,
        temperature_decic <= policy->charge_temperature.high_recovery_decic,
        policy->charge_temperature.debounce_ms,
        policy->charge_temperature.recovery_qualify_ms, now_ms);
    BMS_State_UpdateCondition(&engine->discharge_temp_low,
        temperature_decic <= policy->discharge_temperature.low_trigger_decic,
        temperature_decic >= policy->discharge_temperature.low_recovery_decic,
        policy->discharge_temperature.debounce_ms,
        policy->discharge_temperature.recovery_qualify_ms, now_ms);
    BMS_State_UpdateCondition(&engine->discharge_temp_high,
        temperature_decic >= policy->discharge_temperature.high_trigger_decic,
        temperature_decic <= policy->discharge_temperature.high_recovery_decic,
        policy->discharge_temperature.debounce_ms,
        policy->discharge_temperature.recovery_qualify_ms, now_ms);
}

/* 按条件切换软件故障活动位并保留锁存规则。 */
static void BMS_State_SetFault(BMS_StateSafetySnapshot_t *decision,
                               BMS_FaultId_t fault_id,
                               bool active)
{
    if (active)
    {
        decision->faults.active |= BMS_Fault_Mask(fault_id);
    }
}

/* 把软件故障条件映射为两个方向的禁止原因。 */
static void BMS_State_BuildSoftwareActions(
    const BMS_StateEngine_t *engine,
    BMS_StateSafetySnapshot_t *decision,
    bool rtos_health_fault)
{
    /*
     * fault bitmap 用于说明“发生了什么”，directional inhibit 决定“哪个方向不
     * 允许”。例如 OV 只禁止继续充电、UV 只禁止继续放电；DATA_STALE 与任务
     * 失活无法证明任一方向安全，必须 BOTH inhibit。FAULT 分类本身不直接写 MOS。
     */
    BMS_State_SetFault(decision, BMS_FAULT_ID_SW_OV, engine->sw_ov.active);
    BMS_State_SetFault(decision, BMS_FAULT_ID_SW_UV, engine->sw_uv.active);
    BMS_State_SetFault(decision, BMS_FAULT_ID_SW_OC_CHARGE,
                       engine->sw_oc_charge.active);
    BMS_State_SetFault(decision, BMS_FAULT_ID_SW_OC_DISCHARGE,
                       engine->sw_oc_discharge.active);
    BMS_State_SetFault(decision, BMS_FAULT_ID_TEMPERATURE_LOW,
                       engine->charge_temp_low.active ||
                       engine->discharge_temp_low.active);
    BMS_State_SetFault(decision, BMS_FAULT_ID_TEMPERATURE_HIGH,
                       engine->charge_temp_high.active ||
                       engine->discharge_temp_high.active);
    BMS_State_SetFault(decision, BMS_FAULT_ID_DATA_STALE,
                       engine->data_stale_active);
    BMS_State_SetFault(decision, BMS_FAULT_ID_RTOS_HEALTH,
                       rtos_health_fault);

    if (engine->sw_ov.active)
    {
        decision->inhibit_chg_reasons |=
            BMS_INHIBIT_REASON_FAULT(BMS_FAULT_ID_SW_OV);
    }
    if (engine->sw_uv.active)
    {
        decision->inhibit_dsg_reasons |=
            BMS_INHIBIT_REASON_FAULT(BMS_FAULT_ID_SW_UV);
    }
    if (engine->sw_oc_charge.active)
    {
        decision->inhibit_chg_reasons |=
            BMS_INHIBIT_REASON_FAULT(BMS_FAULT_ID_SW_OC_CHARGE);
    }
    if (engine->sw_oc_discharge.active)
    {
        decision->inhibit_dsg_reasons |=
            BMS_INHIBIT_REASON_FAULT(BMS_FAULT_ID_SW_OC_DISCHARGE);
    }
    if (engine->charge_temp_low.active || engine->charge_temp_high.active)
    {
        decision->inhibit_chg_reasons |=
            BMS_INHIBIT_REASON_FAULT(
                engine->charge_temp_high.active ?
                BMS_FAULT_ID_TEMPERATURE_HIGH :
                BMS_FAULT_ID_TEMPERATURE_LOW);
    }
    if (engine->discharge_temp_low.active ||
        engine->discharge_temp_high.active)
    {
        decision->inhibit_dsg_reasons |=
            BMS_INHIBIT_REASON_FAULT(
                engine->discharge_temp_high.active ?
                BMS_FAULT_ID_TEMPERATURE_HIGH :
                BMS_FAULT_ID_TEMPERATURE_LOW);
    }
    if (engine->data_stale_active)
    {
        decision->inhibit_chg_reasons |=
            BMS_INHIBIT_REASON_FAULT(BMS_FAULT_ID_DATA_STALE);
        decision->inhibit_dsg_reasons |=
            BMS_INHIBIT_REASON_FAULT(BMS_FAULT_ID_DATA_STALE);
    }
    if (rtos_health_fault)
    {
        decision->inhibit_chg_reasons |=
            BMS_INHIBIT_REASON_FAULT(BMS_FAULT_ID_RTOS_HEALTH);
        decision->inhibit_dsg_reasons |=
            BMS_INHIBIT_REASON_FAULT(BMS_FAULT_ID_RTOS_HEALTH);
    }
}

/* 依据当前测量和故障条件提出候选运行状态。 */
static BMS_State_t BMS_State_CurrentCandidate(const BMS_Policy_t *policy,
                                               int32_t current_ma)
{
    if (current_ma >= policy->state.charge_enter_current_ma)
    {
        return BMS_STATE_CHARGE;
    }
    if (current_ma <= policy->state.discharge_enter_current_ma)
    {
        return BMS_STATE_DISCHARGE;
    }
    return BMS_STATE_STANDBY;
}

/* 读取候选状态需要连续满足的资格时长。 */
static uint32_t BMS_State_CandidateQualifyMs(const BMS_Policy_t *policy,
                                             BMS_State_t candidate)
{
    if (candidate == BMS_STATE_CHARGE)
    {
        return policy->state.charge_enter_qualify_ms;
    }
    if (candidate == BMS_STATE_DISCHARGE)
    {
        return policy->state.discharge_enter_qualify_ms;
    }
    return policy->state.standby_enter_qualify_ms;
}

/* 对候选运行状态累计资格时间后发布分类。 */
static void BMS_State_UpdateClassification(BMS_StateEngine_t *engine,
                                           const BMS_Policy_t *policy,
                                           const BMS_DataSnapshot_t *measurement,
                                           uint32_t now_ms,
                                           bool technical_ready,
                                           bool fault_present)
{
    BMS_State_t candidate;
    uint32_t qualify_ms;
    int32_t current_ma;

    /*
     * 分类优先级：技术恢复未完成时保持 INIT（超时后显示 FAULT）；任一 State
     * fault 存在时显示 FAULT；其余情况下再按电流方向分类。CHARGE/DISCHARGE
     * 使用 enter/exit 两组阈值形成 hysteresis，并要求候选方向持续 qualify_ms，
     * 防止零点噪声让状态在三个运行分类之间抖动。
     */
    if (!technical_ready)
    {
        engine->state = BMS_State_TimeElapsed(
            now_ms, engine->init_started_ms, policy->state.startup_timeout_ms) ?
            BMS_STATE_FAULT : BMS_STATE_INIT;
        BMS_State_ResetTracking(&engine->state_transition);
        return;
    }
    if (fault_present)
    {
        engine->state = BMS_STATE_FAULT;
        BMS_State_ResetTracking(&engine->state_transition);
        return;
    }

    current_ma = measurement->current_ma;
    candidate = BMS_State_CurrentCandidate(policy, current_ma);
    if ((engine->state == BMS_STATE_CHARGE) &&
        (current_ma >= policy->state.charge_exit_current_ma))
    {
        candidate = BMS_STATE_CHARGE;
    }
    if ((engine->state == BMS_STATE_DISCHARGE) &&
        (current_ma <= policy->state.discharge_exit_current_ma))
    {
        candidate = BMS_STATE_DISCHARGE;
    }
    if ((engine->state == BMS_STATE_INIT) ||
        (engine->state == BMS_STATE_FAULT))
    {
        engine->state = BMS_STATE_STANDBY;
    }
    if (candidate == engine->state)
    {
        BMS_State_ResetTracking(&engine->state_transition);
        return;
    }
    if (!engine->state_transition.assert_tracking ||
        (engine->transition_candidate != candidate))
    {
        engine->transition_candidate = candidate;
        engine->state_transition.assert_tracking = true;
        engine->state_transition.assert_started_ms = now_ms;
        return;
    }
    qualify_ms = BMS_State_CandidateQualifyMs(policy, candidate);
    if (BMS_State_TimeElapsed(now_ms,
                              engine->state_transition.assert_started_ms,
                              qualify_ms))
    {
        engine->state = candidate;
        BMS_State_ResetTracking(&engine->state_transition);
    }
}

/* 绑定策略并建立初始状态、软件保护与方向性禁止。 */
void BMS_State_Init(const BMS_Policy_t *policy, uint32_t now_ms)
{
    BMS_StateCondition_t empty_condition;

    empty_condition.active = false;
    empty_condition.assert_tracking = false;
    empty_condition.recovery_tracking = false;
    empty_condition.assert_started_ms = 0UL;
    empty_condition.recovery_started_ms = 0UL;
    s_engine.sw_ov = empty_condition;
    s_engine.sw_uv = empty_condition;
    s_engine.sw_oc_charge = empty_condition;
    s_engine.sw_oc_discharge = empty_condition;
    s_engine.charge_temp_low = empty_condition;
    s_engine.charge_temp_high = empty_condition;
    s_engine.discharge_temp_low = empty_condition;
    s_engine.discharge_temp_high = empty_condition;
    s_engine.state_transition = empty_condition;
    s_engine.state = BMS_STATE_INIT;
    s_engine.transition_candidate = BMS_STATE_STANDBY;
    s_engine.init_started_ms = now_ms;
    s_engine.last_fresh_sequence = 0UL;
    s_engine.fresh_frame_count = 0U;
    s_engine.data_stale_active = true;
    s_engine.initialized = true;
    s_policy = policy;

    BMS_Fault_Init(&s_snapshot.faults);
    s_snapshot.inhibit_chg_reasons = BMS_INHIBIT_REASON_DECISION_STALE;
    s_snapshot.inhibit_dsg_reasons = BMS_INHIBIT_REASON_DECISION_STALE;
    s_snapshot.evaluated_sample_sequence = 0UL;
    s_snapshot.evaluated_afe_generation = 0UL;
    s_snapshot.publication_revision = 0UL;
    s_snapshot.state = BMS_STATE_INIT;
    s_snapshot.operational_intent.chg = BQ76940_FET_DESIRE_DISABLE;
    s_snapshot.operational_intent.dsg = BQ76940_FET_DESIRE_DISABLE;
    s_snapshot.technical_ready = false;
#if defined(TEST_PHASE9_IMAGE)
    s_pre_publish_hook = NULL;
#endif
}

/* 基于一致测量快照计算软件保护、状态分类和方向性禁止。 */
bool BMS_State_Evaluate(BMS_StateEngine_t *engine,
                        const BMS_Policy_t *policy,
                        const BMS_DataSnapshot_t *measurement,
                        uint32_t now_ms,
                        bool technical_ready,
                        bool rtos_health_fault,
                        BMS_StateSafetySnapshot_t *decision)
{
    bool safety_measurements_fresh;

    if ((engine == NULL) || (policy == NULL) || (measurement == NULL) ||
        (decision == NULL) || !engine->initialized ||
        !BMS_Policy_Validate(policy))
    {
        return false;
    }
    /*
     * decision 从零构造，并先记录它所依据的 sample identity。Evaluate 只修改
     * 调用者提供的临时对象和 engine；真正的全局发布留给 PublishIfCurrent，
     * 因而可以在发布前再次发现并拒绝并发到达的新测量。
     */
    BMS_Fault_Init(&decision->faults);
    decision->inhibit_chg_reasons = 0UL;
    decision->inhibit_dsg_reasons = 0UL;
    decision->evaluated_sample_sequence = measurement->sample_sequence;
    decision->evaluated_afe_generation = measurement->afe_generation;
    decision->publication_revision = 0UL;
    decision->technical_ready = technical_ready;

    BMS_State_UpdateDataStale(engine, policy, measurement);
    safety_measurements_fresh =
        BMS_State_AllSafetyMeasurementsFresh(measurement);
    if (safety_measurements_fresh)
    {
        BMS_State_UpdateSoftwareProtection(engine, policy, measurement,
                                           now_ms);
    }
    BMS_State_BuildSoftwareActions(engine, decision, rtos_health_fault);
    BMS_State_UpdateClassification(engine, policy, measurement, now_ms,
        technical_ready, decision->faults.active != 0UL);
    decision->state = engine->state;

    if (technical_ready)
    {
        decision->operational_intent.chg = BQ76940_FET_DESIRE_ENABLE;
        decision->operational_intent.dsg = BQ76940_FET_DESIRE_ENABLE;
    }
    else
    {
        decision->operational_intent.chg = BQ76940_FET_DESIRE_DISABLE;
        decision->operational_intent.dsg = BQ76940_FET_DESIRE_DISABLE;
    }
    return true;
}

/* 只有测量身份与捕获时一致才发布软件保护和运行状态。 */
bool BMS_State_PublishIfCurrent(BMS_StateSafetySnapshot_t *decision)
{
    BMS_DataIdentity_t identity;

    if (decision == NULL)
    {
        return false;
    }
#if defined(TEST_PHASE9_IMAGE)
    if (s_pre_publish_hook != NULL)
    {
        s_pre_publish_hook();
    }
#endif
    /*
     * Evaluate 可能耗时；期间 SampleTask 可以发布新帧。如果不比较 identity，
     * 旧电压/电流计算出的“允许”就可能覆盖到新 generation。先比较，再在调度器
     * 临界区内递增 revision 并整 struct 替换，使 FET Manager 看到一致的权威版本。
     */
    if (!BMS_Data_GetIdentity(&identity) ||
        (identity.sample_sequence != decision->evaluated_sample_sequence) ||
        (identity.afe_generation != decision->evaluated_afe_generation))
    {
        return false;
    }
    BMS_Runtime_CriticalEnter();
    decision->publication_revision =
        (uint32_t)(s_snapshot.publication_revision + 1UL);
    s_snapshot = *decision;
    BMS_Runtime_CriticalExit();
    return true;
}

/* 基于当前测量和安全输入计算状态，按测量身份比较后发布决策。 */
bool BMS_State_RunOnce(uint32_t now_ms,
                       bool technical_ready,
                       bool rtos_health_fault,
                       BMS_StateSafetySnapshot_t *published)
{
    BMS_DataSnapshot_t measurement;
    BMS_StateSafetySnapshot_t decision;

    if ((s_policy == NULL) || !s_engine.initialized ||
        !BMS_Data_GetSnapshot(&measurement, now_ms) ||
        !BMS_State_Evaluate(&s_engine, s_policy, &measurement, now_ms,
                            technical_ready, rtos_health_fault, &decision) ||
        !BMS_State_PublishIfCurrent(&decision))
    {
        return false;
    }
    if (published != NULL)
    {
        *published = decision;
    }
    return true;
}

/* 复制 State 发布的运行意图、禁止原因与测量身份。 */
BMS_StateSafetySnapshot_t BMS_State_GetSafetySnapshot(void)
{
    BMS_StateSafetySnapshot_t snapshot;

    BMS_Runtime_CriticalEnter();
    snapshot = s_snapshot;
    BMS_Runtime_CriticalExit();
    return snapshot;
}

#if defined(TEST_PHASE9_IMAGE)
/* 测试镜像设置发布前竞态注入点。 */
void BMS_State_TestSetPrePublishHook(BMS_StatePrePublishHook_t hook)
{
    s_pre_publish_hook = hook;
}
#endif
