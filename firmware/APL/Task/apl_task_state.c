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
    BMS_FaultSummary_t diagnostic_faults;
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
        now_ms = APL_TimeMs();
        BMS_Health_Heartbeat(BMS_HEALTH_TASK_STATE);
        health = BMS_Health_Evaluate(&health_monitor,
                                     &policy->health, now_ms);

        if (BMS_Recovery_Service(now_ms))
        {
            APL_Rtos_RequestProtectService();
        }
        recovery = BMS_Recovery_GetSnapshot();
        (void)BMS_State_RunOnce(now_ms,
                                recovery.technical_ready,
                                health.rtos_health_fault,
                                &state);

        protect = BMS_Protect_GetSafetySnapshot();
        if (BMS_Data_GetSnapshot(&measurement, now_ms) &&
            BMS_HwRecovery_Evaluate(&hw_recovery, policy,
                                    &protect, &measurement,
                                    now_ms, &request) &&
            BMS_Protect_SubmitHwRecoveryRequest(&request, now_ms))
        {
            APL_Rtos_RequestProtectService();
        }
        BMS_FetManager_Service();

        state = BMS_State_GetSafetySnapshot();
        protect = BMS_Protect_GetSafetySnapshot();
        diagnostic_faults.active = state.faults.active |
                                   protect.faults.active;
        diagnostic_faults.latched = state.faults.latched |
                                    protect.faults.latched;
        (void)BMS_Data_PublishStateDiagnostic(state.state,
                                               &diagnostic_faults);

        if (health.feed_allowed)
        {
            if (!iwdg_started)
            {
                iwdg_started = BSP_IWDG_StartNominal(
                    policy->health.iwdg_nominal_timeout_ms);
            }
            else
            {
                BSP_IWDG_Feed();
            }
        }
        (void)ulTaskNotifyTake(pdTRUE,
            pdMS_TO_TICKS(policy->state.period_ms));
    }
}
