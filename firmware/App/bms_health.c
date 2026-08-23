#include "bms_health.h"

#include <stddef.h>

#include "app_rtos.h"

/* Naturally aligned monotonic generations; each task is its sole writer. */
static volatile uint32_t s_generation[BMS_HEALTH_TASK_COUNT];

static bool BMS_Health_TimeElapsed(uint32_t now_ms,
                                   uint32_t started_ms,
                                   uint32_t duration_ms)
{
    return ((uint32_t)(now_ms - started_ms) > duration_ms);
}

void BMS_Health_Init(void)
{
    uint8_t index;

    for (index = 0U; index < (uint8_t)BMS_HEALTH_TASK_COUNT; ++index)
    {
        s_generation[index] = 0UL;
    }
}

void BMS_Health_Heartbeat(BMS_HealthTaskId_t task_id)
{
    if ((uint32_t)task_id < (uint32_t)BMS_HEALTH_TASK_COUNT)
    {
        s_generation[task_id] =
            (uint32_t)(s_generation[task_id] + 1UL);
    }
}

BMS_HealthSnapshot_t BMS_Health_GetSnapshot(void)
{
    BMS_HealthSnapshot_t snapshot;
    uint8_t index;

    vTaskSuspendAll();
    for (index = 0U; index < (uint8_t)BMS_HEALTH_TASK_COUNT; ++index)
    {
        snapshot.generation[index] = s_generation[index];
    }
    (void)xTaskResumeAll();
    return snapshot;
}

void BMS_Health_MonitorInit(BMS_HealthMonitor_t *monitor,
                            uint32_t now_ms)
{
    BMS_HealthSnapshot_t baseline;
    uint8_t index;

    if (monitor == NULL)
    {
        return;
    }
    baseline = BMS_Health_GetSnapshot();
    for (index = 0U; index < (uint8_t)BMS_HEALTH_TASK_COUNT; ++index)
    {
        monitor->last_generation[index] = baseline.generation[index];
        monitor->last_advance_ms[index] = now_ms;
        monitor->observed_advance[index] = false;
    }
    monitor->baseline_ms = now_ms;
    monitor->baseline_captured = true;
    monitor->watchdog_armed = false;
    monitor->rtos_health_fault = false;
}

BMS_HealthDecision_t BMS_Health_Evaluate(
    BMS_HealthMonitor_t *monitor,
    const BMS_HealthPolicy_t *policy,
    uint32_t now_ms)
{
    BMS_HealthDecision_t decision;
    BMS_HealthSnapshot_t current;
    uint8_t roster_index;
    uint8_t task_index;

    decision.all_tasks_advanced = false;
    decision.watchdog_armed = false;
    decision.feed_allowed = false;
    decision.rtos_health_fault = true;
    decision.stale_task_bitmap = 0UL;
    if ((monitor == NULL) || (policy == NULL) ||
        (policy->roster == NULL) ||
        (policy->roster_count != (uint8_t)BMS_HEALTH_TASK_COUNT))
    {
        return decision;
    }
    if (!monitor->baseline_captured)
    {
        BMS_Health_MonitorInit(monitor, now_ms);
    }
    current = BMS_Health_GetSnapshot();
    decision.all_tasks_advanced = true;
    for (roster_index = 0U;
         roster_index < policy->roster_count;
         ++roster_index)
    {
        task_index = (uint8_t)policy->roster[roster_index].task_id;
        if (current.generation[task_index] !=
            monitor->last_generation[task_index])
        {
            monitor->last_generation[task_index] =
                current.generation[task_index];
            monitor->last_advance_ms[task_index] = now_ms;
            monitor->observed_advance[task_index] = true;
        }
        if (!monitor->observed_advance[task_index])
        {
            decision.all_tasks_advanced = false;
        }
        if (BMS_Health_TimeElapsed(
                now_ms,
                monitor->last_advance_ms[task_index],
                policy->roster[roster_index].max_liveness_ms))
        {
            decision.stale_task_bitmap |=
                (uint32_t)((uint32_t)1UL << task_index);
        }
    }
    if (decision.all_tasks_advanced)
    {
        monitor->watchdog_armed = true;
    }
    if (!monitor->watchdog_armed &&
        !BMS_Health_TimeElapsed(now_ms, monitor->baseline_ms,
                                policy->startup_grace_ms))
    {
        decision.stale_task_bitmap = 0UL;
    }
    monitor->rtos_health_fault =
        (decision.stale_task_bitmap != 0UL);
    decision.watchdog_armed = monitor->watchdog_armed;
    decision.rtos_health_fault = monitor->rtos_health_fault;
    decision.feed_allowed = monitor->watchdog_armed &&
        !monitor->rtos_health_fault;
    return decision;
}
