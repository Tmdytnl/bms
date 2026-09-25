#ifndef FML_BALANCE_EVALUATE_H
#define FML_BALANCE_EVALUATE_H

#include <stdbool.h>
#include <stdint.h>

#include "fml_data.h"
#include "fml_policy.h"
#include "fml_protect.h"
#include "fml_recovery.h"
#include "fml_state.h"

/* 纯选择算法的历史与轮换状态；不持有硬件写权限。 */
typedef struct
{
    uint16_t active_bitmap; /* 上轮选择的电芯位图，供滞回判断。 */
    uint8_t rotation_cursor; /* 同电压候选之间轮换的下一个起点。 */
    uint32_t rotation_started_ms; /* 当前轮换窗口的起始毫秒时刻。 */
    bool rotation_started; /* 是否已经建立轮换时间基线。 */
} BMS_BalanceEngine_t;

/*
 * 根据传入的测量、安全快照和策略选择目标位图，并更新 engine 的上一轮位图
 * 与轮换状态；不读写 CELLBAL。
 * 任一资格不满足时返回全关；已激活电芯用 stop_delta，未激活电芯用
 * start_delta，形成滞回，并在候选间按轮换窗口切换。
 */
uint16_t FML_Balance_Evaluate(
    BMS_BalanceEngine_t *engine,
    const BMS_BalancePolicy_t *policy,
    const BMS_DataSnapshot_t *measurement,
    const BMS_StateSafetySnapshot_t *state,
    const BMS_ProtectSafetySnapshot_t *protect,
    const BMS_RecoverySnapshot_t *recovery,
    uint32_t now_ms);

#endif /* FML_BALANCE_EVALUATE_H */
