#include "fml_health.h"
#include "os_runtime.h"

#include <stddef.h>

/*
 * 自然对齐的单调 generation；每个任务是自身 slot sole writer。StateTask 在
 * health window 两端 snapshot 比较：全部任务至少推进一次后才 arm watchdog，
 * 任一必需任务超过 stale 窗口未推进就置 RTOS_HEALTH 并令 feed_allowed=false。
 */
/* 各必需任务各自递增的 heartbeat generation；StateTask 只读比较。 */
static volatile uint32_t s_generation[BMS_HEALTH_TASK_COUNT];

/* 按回绕安全的毫秒差判断任务健康观察窗口结束。 */
static bool FML_Health_TimeElapsed(uint32_t now_ms,
                                   uint32_t started_ms,
                                   uint32_t duration_ms)
{
    return ((uint32_t)(now_ms - started_ms) > duration_ms);
}

/* 初始化七任务 heartbeat generation 与只读健康快照。 */
void FML_Health_Init(void)
{
    /* 当前检查的任务健康名单索引。 */
    uint8_t index;

    for (index = 0U; index < (uint8_t)BMS_HEALTH_TASK_COUNT; ++index)
    {
        s_generation[index] = 0UL;
    }
}

/* 只推进调用任务自己的单调 heartbeat generation。 */
void FML_Health_Heartbeat(BMS_HealthTaskId_t task_id)
{
    if ((uint32_t)task_id < (uint32_t)BMS_HEALTH_TASK_COUNT)
    {
        s_generation[task_id] =
            (uint32_t)(s_generation[task_id] + 1UL);
    }
}

/* 在短临界区复制七任务进度与喂狗资格快照。 */
BMS_HealthSnapshot_t FML_Health_GetSnapshot(void)
{
    /* 本次读取的一致状态快照。 */
    BMS_HealthSnapshot_t snapshot;
    /* 当前检查的任务健康名单索引。 */
    uint8_t index;

    OS_CriticalEnter();
    for (index = 0U; index < (uint8_t)BMS_HEALTH_TASK_COUNT; ++index)
    {
        snapshot.generation[index] = s_generation[index];
    }
    OS_CriticalExit();
    return snapshot;
}

/* 捕获七任务初始 generation，建立健康比较窗口。 */
void FML_Health_MonitorInit(BMS_HealthMonitor_t *monitor,
                            uint32_t now_ms)
{
    /* 本轮任务健康比较的上次快照。 */
    BMS_HealthSnapshot_t baseline;
    /* 当前检查的任务健康名单索引。 */
    uint8_t index;

    if (monitor == NULL)
    {
        return;
    }
    baseline = FML_Health_GetSnapshot();
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

/* 比较必需任务在连续窗口内是否推进，并形成 IWDG 喂狗决定。 */
BMS_HealthDecision_t FML_Health_Evaluate(
    BMS_HealthMonitor_t *monitor,
    const BMS_HealthPolicy_t *policy,
    uint32_t now_ms)
{
    /* 本轮健康或状态评估生成的决策。 */
    BMS_HealthDecision_t decision;
    /* 当前计算或读取的值。 */
    BMS_HealthSnapshot_t current;
    /* 当前遍历的任务健康名单条目。 */
    uint8_t roster_index;
    /* 当前任务的索引。 */
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
        FML_Health_MonitorInit(monitor, now_ms);
    }
    current = FML_Health_GetSnapshot();
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
        if (FML_Health_TimeElapsed(
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
        !FML_Health_TimeElapsed(now_ms, monitor->baseline_ms,
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
