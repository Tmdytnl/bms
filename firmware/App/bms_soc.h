#ifndef BMS_SOC_H
#define BMS_SOC_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_data.h"
#include "bms_policy.h"

typedef struct
{
    int64_t remaining_mams;
    uint32_t last_sample_ms;
    uint32_t afe_generation;
    uint32_t integrated_sample_count;
    uint32_t queue_gap_count;
    uint32_t generation_change_count;
    uint32_t full_correction_count;
    uint32_t empty_correction_count;
    uint32_t full_started_ms;
    uint32_t empty_started_ms;
    bool initialized;
    bool valid;
    bool have_sample_time;
    bool queue_gap_latched;
    bool full_tracking;
    bool empty_tracking;
} BMS_SocEngine_t;

typedef struct
{
    BMS_CapacityMah_t remaining_capacity_mah;
    BMS_SocPermille_t soc_permille;
    uint32_t integrated_sample_count;
    uint32_t queue_gap_count;
    uint32_t generation_change_count;
    uint32_t full_correction_count;
    uint32_t empty_correction_count;
    bool valid;
    bool queue_gap_latched;
} BMS_SocSnapshot_t;

/* Pure integer engine used by SOCTask and simulator tests. */
bool BMS_Soc_EngineInit(BMS_SocEngine_t *engine,
                        const BMS_SocPolicy_t *policy,
                        const BMS_DataSnapshot_t *measurement,
                        uint32_t now_ms);
bool BMS_Soc_IntegrateCurrent(BMS_SocEngine_t *engine,
                              const BMS_SocPolicy_t *policy,
                              int32_t current_ma,
                              uint32_t sample_ms,
                              uint32_t afe_generation);
void BMS_Soc_ObserveCorrection(BMS_SocEngine_t *engine,
                               const BMS_SocPolicy_t *policy,
                               const BMS_DataSnapshot_t *measurement,
                               uint32_t now_ms);
void BMS_Soc_MarkQueueGap(BMS_SocEngine_t *engine);
BMS_SocSnapshot_t BMS_Soc_GetEngineSnapshot(
    const BMS_SocEngine_t *engine,
    const BMS_SocPolicy_t *policy);

void BMS_Soc_Init(const BMS_Policy_t *policy);
void BMS_Soc_RunOnce(uint32_t now_ms);
BMS_SocSnapshot_t BMS_Soc_GetSnapshot(void);

#endif /* BMS_SOC_H */
