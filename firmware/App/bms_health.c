#include "bms_health.h"

#include <stddef.h>

#include "app_rtos.h"

/*
 * 自然对齐的单调 generation；每个任务是自身 slot sole writer。StateTask 在
 * health window 两端 snapshot 比较：全部任务至少推进一次后才 arm watchdog，
 * 任一必需任务超过 stale 窗口未推进就置 RTOS_HEALTH 并令 feed_allowed=false。
 */
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

    /*
     * 默认 fail-closed：输入/roster 无效时直接给出 health fault 且不允许喂狗。
     * 正常路径比较每个 task 的 generation；变化会刷新 last_advance_ms，不变化
     * 则时间继续累积。只有全 roster 至少推进一次后才 arm IWDG，避免启动早期
     * 某些任务尚未获得运行机会就误判，也避免“只启动了 State 自己”便开始喂狗。
     */
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
