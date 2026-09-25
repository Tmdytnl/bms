#ifndef FML_HEALTH_H
#define FML_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

#include "fml_policy.h"

typedef struct
{
    uint32_t generation[BMS_HEALTH_TASK_COUNT]; /* 每任务独立、单调、sole-writer */
} BMS_HealthSnapshot_t;

typedef struct
{
    uint32_t last_generation[BMS_HEALTH_TASK_COUNT]; /* supervisor 上次观察的各任务值。 */
    uint32_t last_advance_ms[BMS_HEALTH_TASK_COUNT]; /* 最近一次确认变化的时间。 */
    bool observed_advance[BMS_HEALTH_TASK_COUNT];    /* 启动后至少见过一次推进。 */
    uint32_t baseline_ms;                            /* startup grace 计时起点。 */
    bool baseline_captured;                          /* monitor 已获取初始快照。 */
    bool watchdog_armed;                             /* 全 roster 至少推进一次后锁存。 */
    bool rtos_health_fault;                          /* 至少一个任务超过 liveness 窗口。 */
} BMS_HealthMonitor_t;

typedef struct
{
    bool all_tasks_advanced;   /* 启动以来所有 roster task 都至少变化过。 */
    bool watchdog_armed;       /* IWDG 是否已经具备启动资格。 */
    bool feed_allowed;         /* 本次 State 周期是否允许唯一喂狗点执行。 */
    bool rtos_health_fault;    /* 对 State owner 发布的健康 fault。 */
    uint32_t stale_task_bitmap;/* bit=1：对应 task generation 超时未推进。 */
} BMS_HealthDecision_t;

/* 初始化七任务 heartbeat generation 与只读健康快照。 */
void FML_Health_Init(void);

/*
 * 每个任务只推进自己的 ID。generation 而非 bool alive 可以证明“持续前进”：
 * bool 一旦写成 true，任务随后死锁也会永远保持 true；generation 10→11→12→13
 * 若连续健康窗口都停在 13，StateTask 就能证明任务没有继续执行。该模型也无需
 * supervisor 与任务竞争 clear bit，因此刻意不存在 clear API。
 */
void FML_Health_Heartbeat(BMS_HealthTaskId_t task_id);

/* 在短临界区复制七任务进度与喂狗资格快照。 */
BMS_HealthSnapshot_t FML_Health_GetSnapshot(void);
/* 捕获七任务初始 generation，建立健康比较窗口。 */
void FML_Health_MonitorInit(BMS_HealthMonitor_t *monitor,
                            uint32_t now_ms);
/* 比较必需任务在连续窗口内是否推进，并形成 IWDG 喂狗决定。 */
BMS_HealthDecision_t FML_Health_Evaluate(
    BMS_HealthMonitor_t *monitor,
    const BMS_HealthPolicy_t *policy,
    uint32_t now_ms);

#endif /* FML_HEALTH_H：头文件防重复包含 */
