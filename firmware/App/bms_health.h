#ifndef BMS_HEALTH_H
#define BMS_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_policy.h"

typedef struct
{
    uint32_t generation[BMS_HEALTH_TASK_COUNT];
} BMS_HealthSnapshot_t;

typedef struct
{
    uint32_t last_generation[BMS_HEALTH_TASK_COUNT];
    uint32_t last_advance_ms[BMS_HEALTH_TASK_COUNT];
    bool observed_advance[BMS_HEALTH_TASK_COUNT];
    uint32_t baseline_ms;
    bool baseline_captured;
    bool watchdog_armed;
    bool rtos_health_fault;
} BMS_HealthMonitor_t;

typedef struct
{
    bool all_tasks_advanced;
    bool watchdog_armed;
    bool feed_allowed;
    bool rtos_health_fault;
    uint32_t stale_task_bitmap;
} BMS_HealthDecision_t;

void BMS_Health_Init(void);

/* Each task calls only its own ID; there is deliberately no clear API. */
void BMS_Health_Heartbeat(BMS_HealthTaskId_t task_id);

BMS_HealthSnapshot_t BMS_Health_GetSnapshot(void);
void BMS_Health_MonitorInit(BMS_HealthMonitor_t *monitor,
                            uint32_t now_ms);
BMS_HealthDecision_t BMS_Health_Evaluate(
    BMS_HealthMonitor_t *monitor,
    const BMS_HealthPolicy_t *policy,
    uint32_t now_ms);

#endif /* BMS_HEALTH_H */
