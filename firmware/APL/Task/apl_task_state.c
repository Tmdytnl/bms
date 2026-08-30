#include "apl_tasks.h"

#include "apl_rtos.h"
#include "bms_data.h"
#include "bms_fet_manager.h"
#include "bms_health.h"
#include "bms_hw_recovery.h"
#include "bms_policy.h"
#include "bms_protect.h"
#include "bms_recovery.h"
#include "bms_state.h"
#include "bsp_iwdg.h"

/*
 * StateTask 是安全 owner 的编排/执行上下文，不在 APL 重做领域算法：每轮按
 * health -> recovery -> state -> HW recovery qualification -> FET 的依赖顺序推进，
 * 随后只发布诊断投影并执行 watchdog 决定。任一算法 authority 仍属于对应 FML。
 */
static void APL_State_PublishDiagnostics(void)
{
    BMS_ProtectSafetySnapshot_t protect;
    BMS_StateSafetySnapshot_t state;
    BMS_FaultSummary_t diagnostic_faults;

    state = BMS_State_GetSafetySnapshot();
    protect = BMS_Protect_GetSafetySnapshot();
    diagnostic_faults.active = state.faults.active |
                               protect.faults.active;
    diagnostic_faults.latched = state.faults.latched |
                                protect.faults.latched;
    (void)BMS_Data_PublishStateDiagnostic(state.state,
                                           &diagnostic_faults);
}

static void APL_State_ServiceWatchdog(
    const BMS_HealthPolicy_t *health_policy,
    const BMS_HealthDecision_t *health,
    bool *iwdg_started)
{
    if ((health_policy == NULL) || (health == NULL) ||
        (iwdg_started == NULL) || !health->feed_allowed)
    {
        return;
    }
    if (!*iwdg_started)
    {
        *iwdg_started = BSP_IWDG_StartNominal(
            health_policy->iwdg_nominal_timeout_ms);
    }
    else
    {
        BSP_IWDG_Feed();
    }
}

void APL_TaskState(void *argument)
{
    const BMS_Policy_t *policy;
    BMS_HealthMonitor_t health_monitor;
    BMS_HealthDecision_t health;
    BMS_HwRecoveryEngine_t hw_recovery;
    BMS_ProtectHwRecoveryRequest_t request;
    BMS_ProtectSafetySnapshot_t protect;
    BMS_RecoverySnapshot_t recovery;
    BMS_StateSafetySnapshot_t state;
    BMS_DataSnapshot_t measurement;
    uint32_t now_ms;
    bool iwdg_started;

    (void)argument;
    policy = BMS_Policy_Get();
    now_ms = APL_TimeMs();
    BMS_Health_MonitorInit(&health_monitor, now_ms);
    BMS_HwRecovery_Init(&hw_recovery);
    iwdg_started = false;
    for (;;)
    {
        /* 1. 先记录本任务存活，再以同一 now_ms 评估完整 roster。 */
        now_ms = APL_TimeMs();
        BMS_Health_Heartbeat(BMS_HEALTH_TASK_STATE);
        health = BMS_Health_Evaluate(&health_monitor,
                                     &policy->health, now_ms);

        /* 2. Recovery 每轮至多一个有界硬件步骤；需要 Protect W1C 时只发请求。 */
        if (BMS_Recovery_Service(now_ms))
        {
            APL_Rtos_RequestProtectService();
        }
        recovery = BMS_Recovery_GetSnapshot();

        /* 3. State 只消费快照并 compare-and-publish，不直接写 FET。 */
        (void)BMS_State_RunOnce(now_ms,
                                recovery.technical_ready,
                                health.rtos_health_fault,
                                &state);

        /* 4. HW fault 释放证据由 State 上下文形成，最终 source clear 仍归 Protect。 */
        protect = BMS_Protect_GetSafetySnapshot();
        if (BMS_Data_GetSnapshot(&measurement, now_ms) &&
            BMS_HwRecovery_Evaluate(&hw_recovery, policy,
                                    &protect, &measurement,
                                    now_ms, &request) &&
            BMS_Protect_SubmitHwRecoveryRequest(&request, now_ms))
        {
            APL_Rtos_RequestProtectService();
        }

        /* 5. FET Manager 在 APL 上下文执行，但完整 transaction authority 留在 FML。 */
        BMS_FetManager_Service();

        /* 6. BMS_Data 仅聚合诊断；watchdog 也只执行 Health 已作出的 feed 决定。 */
        APL_State_PublishDiagnostics();
        APL_State_ServiceWatchdog(&policy->health, &health,
                                  &iwdg_started);

        /* 7. 100 ms 是最大有界等待；urgent notify 可提前结束，绝不是固定 10 ms loop。 */
        (void)ulTaskNotifyTake(pdTRUE,
            pdMS_TO_TICKS(policy->state.period_ms));
    }
}
