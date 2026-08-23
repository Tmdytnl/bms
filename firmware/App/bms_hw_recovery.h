#ifndef BMS_HW_RECOVERY_H
#define BMS_HW_RECOVERY_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_data.h"
#include "bms_policy.h"
#include "bms_protect.h"

typedef struct
{
    bool tracking[BMS_PROTECT_SOURCE_COUNT];
    uint32_t started_ms[BMS_PROTECT_SOURCE_COUNT];
    uint32_t tracked_generation[BMS_PROTECT_SOURCE_COUNT];
    uint32_t request_id;
    uint32_t qualification_revision;
} BMS_HwRecoveryEngine_t;

void BMS_HwRecovery_Init(BMS_HwRecoveryEngine_t *engine);

/* StateTask-owned pure qualification. SCD remains service-reset-only. */
bool BMS_HwRecovery_Evaluate(
    BMS_HwRecoveryEngine_t *engine,
    const BMS_Policy_t *policy,
    const BMS_ProtectSafetySnapshot_t *protect,
    const BMS_DataSnapshot_t *measurement,
    uint32_t now_ms,
    BMS_ProtectHwRecoveryRequest_t *request);

#endif /* BMS_HW_RECOVERY_H */
