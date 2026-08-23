#ifndef BMS_STATE_H
#define BMS_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_build_assert.h"
#include "bms_policy.h"
#include "bms_safety.h"
#include "bq76940_control.h"

struct BMS_DataSnapshot;
typedef struct BMS_DataSnapshot BMS_DataSnapshot_t;

/* BMS application states only. BQ SHIP/NORMAL are AFE device modes. */
typedef enum
{
    BMS_STATE_INIT = 0,
    BMS_STATE_STANDBY = 1,
    BMS_STATE_CHARGE = 2,
    BMS_STATE_DISCHARGE = 3,
    BMS_STATE_FAULT = 4,
    BMS_STATE_COUNT = 5
} BMS_State_t;

BMS_BUILD_ASSERT(BMS_STATE_INIT == 0,
                 state_init_value_is_stable);
BMS_BUILD_ASSERT(BMS_STATE_FAULT == 4,
                 state_fault_value_is_stable);
BMS_BUILD_ASSERT(BMS_STATE_COUNT == 5,
                 state_count_is_five);

typedef struct
{
    bool active;
    bool assert_tracking;
    bool recovery_tracking;
    uint32_t assert_started_ms;
    uint32_t recovery_started_ms;
} BMS_StateCondition_t;

typedef struct
{
    BMS_StateCondition_t sw_ov;
    BMS_StateCondition_t sw_uv;
    BMS_StateCondition_t sw_oc_charge;
    BMS_StateCondition_t sw_oc_discharge;
    BMS_StateCondition_t charge_temp_low;
    BMS_StateCondition_t charge_temp_high;
    BMS_StateCondition_t discharge_temp_low;
    BMS_StateCondition_t discharge_temp_high;
    BMS_StateCondition_t state_transition;
    BMS_State_t state;
    BMS_State_t transition_candidate;
    uint32_t init_started_ms;
    uint32_t last_fresh_sequence;
    uint8_t fresh_frame_count;
    bool data_stale_active;
    bool initialized;
} BMS_StateEngine_t;

typedef struct
{
    BMS_FaultSummary_t faults;
    BMS_InhibitReasonBitmap_t inhibit_chg_reasons;
    BMS_InhibitReasonBitmap_t inhibit_dsg_reasons;
    uint32_t evaluated_sample_sequence;
    uint32_t evaluated_afe_generation;
    uint32_t publication_revision;
    BMS_State_t state;
    BQ76940_FetRequest_t operational_intent;
    bool technical_ready;
} BMS_StateSafetySnapshot_t;

void BMS_State_Init(const BMS_Policy_t *policy, uint32_t now_ms);

/* Pure engine step used by StateTask and host/simulator tests. */
bool BMS_State_Evaluate(BMS_StateEngine_t *engine,
                        const BMS_Policy_t *policy,
                        const BMS_DataSnapshot_t *measurement,
                        uint32_t now_ms,
                        bool technical_ready,
                        bool rtos_health_fault,
                        BMS_StateSafetySnapshot_t *decision);

/* Publish only if the measurement identity is still current. */
bool BMS_State_PublishIfCurrent(BMS_StateSafetySnapshot_t *decision);

/* One bounded production service attempt using the module-owned engine. */
bool BMS_State_RunOnce(uint32_t now_ms,
                       bool technical_ready,
                       bool rtos_health_fault,
                       BMS_StateSafetySnapshot_t *published);

BMS_StateSafetySnapshot_t BMS_State_GetSafetySnapshot(void);

#if defined(TEST_PHASE9_IMAGE)
typedef void (*BMS_StatePrePublishHook_t)(void);
void BMS_State_TestSetPrePublishHook(BMS_StatePrePublishHook_t hook);
#endif

#endif /* BMS_STATE_H */
