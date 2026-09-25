#include "apl_tasks.h"

#include "apl_rtos.h"
#include "fml_data.h"
#include "fml_fet_manager.h"
#include "fml_health.h"
#include "fml_hw_recovery.h"
#include "fml_policy.h"
#include "fml_protect.h"
#include "fml_recovery.h"
#include "fml_state.h"
#include "bsp_iwdg.h"

/*
 * StateTask 是安全 owner 的编排/执行上下文，不在 APL 重做领域算法：每轮按
 * health -> recovery -> state -> HW recovery qualification -> FET 的依赖顺序推进，
 * 随后只发布诊断投影并执行 watchdog 决定。任一算法 authority 仍属于对应 FML。
 */
static void APL_State_PublishDiagnostics(void)
{
    /* Protect owner 发布的安全快照。 */
    BMS_ProtectSafetySnapshot_t protect;
    /* 当前状态或 State owner 的安全快照。 */
    BMS_StateSafetySnapshot_t state;
    /* 向诊断接口发布的故障摘要。 */
    BMS_FaultSummary_t diagnostic_faults;

    state = FML_State_GetSafetySnapshot();
    protect = FML_Protect_GetSafetySnapshot();
    diagnostic_faults.active = state.faults.active |
                               protect.faults.active;
    diagnostic_faults.latched = state.faults.latched |
                                protect.faults.latched;
    (void)FML_Data_PublishStateDiagnostic(state.state,
                                           &diagnostic_faults);
}

/* 仅在七任务健康证据满足时由 StateTask 喂 IWDG。 */
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

/* 按安全依赖顺序编排 Health、Recovery、State、FET 和 IWDG 服务。 */
void APL_TaskState(void *argument)
{
    /* 本轮处理使用的只读 BMS 策略。 */
    const BMS_Policy_t *policy;
    /* 跨周期保存任务心跳的监控状态。 */
    BMS_HealthMonitor_t health_monitor;
    /* 本轮任务健康评估结果。 */
    BMS_HealthDecision_t health;
    /* 跨周期保存 AFE 硬件恢复进度的引擎。 */
    BMS_HwRecoveryEngine_t hw_recovery;
    /* 本轮提交给目标 owner 的请求。 */
    BMS_ProtectHwRecoveryRequest_t request;
    /* Protect owner 发布的安全快照。 */
    BMS_ProtectSafetySnapshot_t protect;
    /* 恢复协调器发布的阶段快照。 */
    BMS_RecoverySnapshot_t recovery;
    /* 当前状态或 State owner 的安全快照。 */
    BMS_StateSafetySnapshot_t state;
    /* 本轮计算使用的测量快照。 */
    BMS_DataSnapshot_t measurement;
    /* 本次处理的单调毫秒时刻。 */
    uint32_t now_ms;
    /* 独立看门狗是否已完成启动。 */
    bool iwdg_started;

    (void)argument;
    policy = FML_Policy_Get();
    now_ms = APL_TimeMs();
    FML_Health_MonitorInit(&health_monitor, now_ms);
    FML_HwRecovery_Init(&hw_recovery);
    iwdg_started = false;
    for (;;)
    {
        /* 1. 先记录本任务存活，再以同一 now_ms 评估完整 roster。 */
        now_ms = APL_TimeMs();
        FML_Health_Heartbeat(BMS_HEALTH_TASK_STATE);
        health = FML_Health_Evaluate(&health_monitor,
                                     &policy->health, now_ms);

        /* 2. Recovery 每轮至多一个有界硬件步骤；需要 Protect W1C 时只发请求。 */
        if (FML_Recovery_Service(now_ms))
        {
            APL_Rtos_RequestProtectService();
        }
        recovery = FML_Recovery_GetSnapshot();

        /* 3. State 只消费快照并 compare-and-publish，不直接写 FET。 */
        (void)FML_State_RunOnce(now_ms,
                                recovery.technical_ready,
                                health.rtos_health_fault,
                                &state);

        /* 4. HW fault 释放证据由 State 上下文形成，最终 source clear 仍归 Protect。 */
        protect = FML_Protect_GetSafetySnapshot();
        if (FML_Data_GetSnapshot(&measurement, now_ms) &&
            FML_HwRecovery_Evaluate(&hw_recovery, policy,
                                    &protect, &measurement,
                                    now_ms, &request) &&
            FML_Protect_SubmitHwRecoveryRequest(&request, now_ms))
        {
            APL_Rtos_RequestProtectService();
        }

        /* 5. FET Manager 在 APL 上下文执行，但完整 transaction authority 留在 FML。 */
        FML_FetManager_Service();

        /* 6. BMS_Data 仅聚合诊断；watchdog 也只执行 Health 已作出的 feed 决定。 */
        APL_State_PublishDiagnostics();
        APL_State_ServiceWatchdog(&policy->health, &health,
                                  &iwdg_started);

        /* 7. 100 ms 是最大有界等待；urgent notify 可提前结束，绝不是固定 10 ms loop。 */
        (void)OS_TaskNotifyTake(OS_PASS,
            OS_MsToTicks(policy->state.period_ms));
    }
}
