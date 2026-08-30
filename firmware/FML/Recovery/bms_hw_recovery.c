#include "bms_hw_recovery.h"

/*
 * 本模块不清 fault，只把 StateTask 观察到的 measurement recovery evidence
 * 组织成带 identity 的 request。generation 变化、source inactive/重触发、数据
 * 失效或 qualification expiry 都会使旧 exchange 失效，避免延迟 ack 清错事件。
 *
 * 以 HW_OV 为例：Protect 捕获 OV 并推进 source_generation -> 电压跨回恢复门限
 * -> State/HwRecovery 用连续 fresh frame 完成资格计时 -> 形成带 sample/generation
 * identity 的 request -> Protect 重新读取 SYS_STAT 并复核 identity -> 返回 ack。
 * “当前 SYS_STAT 没有 OV bit”只是一个瞬时寄存器观察，不能替代这条证据链。
 */

#include <stddef.h>

static bool BMS_HwRecovery_TimeElapsed(uint32_t now_ms,
                                       uint32_t started_ms,
                                       uint32_t duration_ms)
{
    return ((uint32_t)(now_ms - started_ms) >= duration_ms);
}

static bool BMS_HwRecovery_MeasurementFresh(
    const BMS_DataSnapshot_t *measurement)
{
    return (measurement->cell_metadata.valid_bitmap ==
            BMS_CELL_DEFINED_MASK) &&
        (measurement->cell_metadata.in_range_bitmap ==
         BMS_CELL_DEFINED_MASK) &&
        (measurement->cell_metadata.stale_bitmap == 0U) &&
        BMS_Data_IsFresh(measurement->current_metadata.valid,
                         measurement->current_metadata.stale_latched,
                         measurement->current_metadata.age_ms,
                         BMS_DATA_CURRENT_FRESH_MAX_MS);
}

static bool BMS_HwRecovery_SourceConditionSafe(
    BMS_ProtectSourceId_t source,
    const BMS_Policy_t *policy,
    const BMS_DataSnapshot_t *measurement)
{
    uint32_t index;

    if (source == BMS_PROTECT_SOURCE_HW_OV)
    {
        for (index = 0UL; index < (uint32_t)BMS_CELL_COUNT; ++index)
        {
            if (measurement->cell_voltage_mv[index] >
                (uint16_t)policy->sw_ov.recovery)
            {
                return false;
            }
        }
        return true;
    }
    if (source == BMS_PROTECT_SOURCE_HW_UV)
    {
        for (index = 0UL; index < (uint32_t)BMS_CELL_COUNT; ++index)
        {
            if (measurement->cell_voltage_mv[index] <
                (uint16_t)policy->sw_uv.recovery)
            {
                return false;
            }
        }
        return true;
    }
    if (source == BMS_PROTECT_SOURCE_HW_OCD)
    {
        return measurement->current_ma > policy->sw_oc_discharge.recovery;
    }
    return false;
}

static BMS_FaultId_t BMS_HwRecovery_FaultOfSource(
    BMS_ProtectSourceId_t source)
{
    if (source == BMS_PROTECT_SOURCE_HW_OV)
    {
        return BMS_FAULT_ID_HW_OV;
    }
    if (source == BMS_PROTECT_SOURCE_HW_UV)
    {
        return BMS_FAULT_ID_HW_UV;
    }
    return BMS_FAULT_ID_HW_OCD;
}

void BMS_HwRecovery_Init(BMS_HwRecoveryEngine_t *engine)
{
    uint8_t index;

    if (engine == NULL)
    {
        return;
    }
    for (index = 0U; index < (uint8_t)BMS_PROTECT_SOURCE_COUNT; ++index)
    {
        engine->tracking[index] = false;
        engine->started_ms[index] = 0UL;
        engine->tracked_generation[index] = 0UL;
    }
    engine->request_id = 0UL;
    engine->qualification_revision = 0UL;
}

bool BMS_HwRecovery_Evaluate(
    BMS_HwRecoveryEngine_t *engine,
    const BMS_Policy_t *policy,
    const BMS_ProtectSafetySnapshot_t *protect,
    const BMS_DataSnapshot_t *measurement,
    uint32_t now_ms,
    BMS_ProtectHwRecoveryRequest_t *request)
{
    BMS_ProtectSourceId_t source;
    BMS_FaultId_t fault;
    bool active;
    bool safe;

    if ((engine == NULL) || (policy == NULL) || (protect == NULL) ||
        (measurement == NULL) || (request == NULL) ||
        !BMS_Policy_Validate(policy))
    {
        return false;
    }
    /*
     * 每个 source 独立跟踪自己的 generation 与连续安全窗口。条件中断或同类新
     * 事件到达都会从 now_ms 重新计时，防止把两个不连续安全片段拼成恢复证据。
     * SCD 不进入这里，因为它要求 source-specific service reset，而不是自动恢复。
     */
    for (source = BMS_PROTECT_SOURCE_HW_OV;
         source <= BMS_PROTECT_SOURCE_HW_OCD;
         source = (BMS_ProtectSourceId_t)((uint32_t)source + 1UL))
    {
        fault = BMS_HwRecovery_FaultOfSource(source);
        active = BMS_Fault_Contains(protect->faults.active, fault);
        safe = active && BMS_HwRecovery_MeasurementFresh(measurement) &&
            (measurement->afe_generation == protect->xready_generation) &&
            !protect->xready_active &&
            BMS_HwRecovery_SourceConditionSafe(source, policy, measurement);
        if (!safe ||
            (engine->tracked_generation[source] !=
             protect->source_generation[source]))
        {
            engine->tracking[source] = safe;
            engine->started_ms[source] = now_ms;
            engine->tracked_generation[source] =
                protect->source_generation[source];
        }
        else if (engine->tracking[source] &&
                 BMS_HwRecovery_TimeElapsed(
                     now_ms, engine->started_ms[source],
                     policy->service_reset_qualify_ms))
        {
            engine->tracking[source] = false;
            engine->request_id =
                (uint32_t)(engine->request_id + 1UL);
            engine->qualification_revision =
                (uint32_t)(engine->qualification_revision + 1UL);
            request->fault_id = fault;
            request->request_id = engine->request_id;
            request->expected_source_generation =
                protect->source_generation[source];
            request->evaluated_sample_sequence =
                measurement->sample_sequence;
            request->evaluated_afe_generation =
                measurement->afe_generation;
            request->qualification_revision =
                engine->qualification_revision;
            request->expiry_ms = (uint32_t)(now_ms +
                policy->service_reset_qualify_ms);
            request->valid = true;
            return true;
        }
    }
    return false;
}
