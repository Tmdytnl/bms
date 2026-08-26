#ifndef BMS_BALANCE_H
#define BMS_BALANCE_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_data.h"
#include "bms_policy.h"
#include "bms_protect.h"
#include "bms_recovery.h"
#include "bms_state.h"
#include "bq76940.h"

typedef struct
{
    uint16_t active_bitmap;       /* 当前 engine 选择的一节电芯 */
    uint8_t rotation_cursor;      /* 同电压候选间轮换，避免长期偏向低 index */
    uint32_t rotation_started_ms;
    bool rotation_started;
} BMS_BalanceEngine_t;

typedef struct
{
    uint16_t requested_bitmap;
    uint16_t confirmed_bitmap;
    uint32_t publication_revision;
    uint32_t evaluated_sample_sequence;
    uint32_t evaluated_afe_generation;
    uint32_t confirmed_afe_generation;
    BQ76940_Status_t last_transport_status;
    bool register_state_confirmed;
    bool confirmed_all_off;
} BMS_BalanceSnapshot_t;

/*
 * 纯 eligibility/selection engine。只有 CHARGE、measurement fresh/in-range、
 * temperature 合格、无相关 fault/inhibit、recovery complete 且最高/最低 cell
 * delta 达到门限时才选择最高电芯；一次最多一节并按周期轮换。任何停止条件
 * 立即请求 all-off。真正 CELLBAL 写入只由独立 BalanceTask 完成。
 */
uint16_t BMS_Balance_Evaluate(
    BMS_BalanceEngine_t *engine,
    const BMS_BalancePolicy_t *policy,
    const BMS_DataSnapshot_t *measurement,
    const BMS_StateSafetySnapshot_t *state,
    const BMS_ProtectSafetySnapshot_t *protect,
    const BMS_RecoverySnapshot_t *recovery,
    uint32_t now_ms);

void BMS_Balance_Init(BQ76940_t *device,
                      const BMS_Policy_t *policy,
                      bool startup_all_off_confirmed);
void BMS_Balance_RunOnce(uint32_t now_ms);
BMS_BalanceSnapshot_t BMS_Balance_GetSnapshot(void);

#endif /* BMS_BALANCE_H：include guard */
