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

/*
 * BMS application state 只做运行分类，不是 FET 最终安全许可：
 * INIT 等待初始有效证据；STANDBY 表示充放电意图都未稳定成立；CHARGE /
 * DISCHARGE 表示经 qualify 的运行方向；FAULT 表示至少一个状态层安全源 active。
 * 即使 state=FAULT，也必须由 directional inhibit + FET Manager 决定 CHG/DSG；
 * BQ SHIP/NORMAL 则是 AFE device mode，不能与本 enum 混用。
 */
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
    /* 每个软件保护源分别保存 assert debounce 与 recovery hysteresis/delay。 */
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
    uint32_t evaluated_sample_sequence; /* 决策实际读取的完整 measurement */
    uint32_t evaluated_afe_generation;  /* 决策绑定的 AFE 生命周期 */
    uint32_t publication_revision;      /* 每次权威发布递增，供 FET transaction 确认 */
    BMS_State_t state;
    BQ76940_FetRequest_t operational_intent;
    bool technical_ready;
} BMS_StateSafetySnapshot_t;

void BMS_State_Init(const BMS_Policy_t *policy, uint32_t now_ms);

/*
 * StateTask 与 production-C test 共用的纯 engine step。软件 OV/UV/OC/temperature
 * 先经 assert debounce；恢复必须跨回 hysteresis 门限并持续 recovery qualify。
 * 任一必需数据失效或 stale 都设置 DATA_STALE 并双向 inhibit。
 */
bool BMS_State_Evaluate(BMS_StateEngine_t *engine,
                        const BMS_Policy_t *policy,
                        const BMS_DataSnapshot_t *measurement,
                        uint32_t now_ms,
                        bool technical_ready,
                        bool rtos_health_fault,
                        BMS_StateSafetySnapshot_t *decision);

/*
 * compare-and-publish：只有当前 BMS_Data identity 仍等于 evaluated identity 才
 * 发布，防止 Evaluate 与 Publish 之间新采样到达后把旧决策覆盖到新数据上。
 */
bool BMS_State_PublishIfCurrent(BMS_StateSafetySnapshot_t *decision);

/* 使用模块私有 engine 执行一次有界正式 service，并返回实际发布快照。 */
bool BMS_State_RunOnce(uint32_t now_ms,
                       bool technical_ready,
                       bool rtos_health_fault,
                       BMS_StateSafetySnapshot_t *published);

BMS_StateSafetySnapshot_t BMS_State_GetSafetySnapshot(void);

#if defined(TEST_PHASE9_IMAGE)
typedef void (*BMS_StatePrePublishHook_t)(void);
void BMS_State_TestSetPrePublishHook(BMS_StatePrePublishHook_t hook);
#endif

#endif /* BMS_STATE_H：include guard */
