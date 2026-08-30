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
    uint32_t rotation_started_ms;/* 当前轮换窗口起点。 */
    bool rotation_started;       /* 首次评估后才开始轮换计时。 */
} BMS_BalanceEngine_t;

typedef struct
{
    uint16_t requested_bitmap;        /* engine 基于捕获证据选择的目标电芯。 */
    uint16_t confirmed_bitmap;        /* CELLBAL1..3 readback 解码结果。 */
    uint32_t publication_revision;    /* 每次任务级服务结果发布递增。 */
    uint32_t evaluated_sample_sequence; /* 选择依据的完整 measurement。 */
    uint32_t evaluated_afe_generation;  /* 选择依据的 AFE 生命周期。 */
    uint32_t confirmed_afe_generation;  /* readback 时 Protect 报告的 generation。 */
    BQ76940_Status_t last_transport_status; /* 最近写/回读结果。 */
    bool register_state_confirmed;    /* 三个 CELLBAL 字节都与目标一致。 */
    bool confirmed_all_off;           /* Recovery 可消费的“硬件均衡全关”证据。 */
} BMS_BalanceSnapshot_t;

/*
 * 纯 eligibility/selection engine。只有 CHARGE、measurement fresh/in-range、
 * temperature 合格、无相关 fault/inhibit、recovery complete 且最高/最低 cell
 * delta 达到门限时才选择最高电芯；一次最多策略限定数量并按周期轮换。任何停止条件
 * 立即请求 all-off。真正 CELLBAL 写入只由独立 BalanceTask 完成。
 * 已激活电芯使用 stop_delta，未激活电芯使用更大的 start_delta；这组 hysteresis
 * 避免电压差在单一阈值附近让均衡位频繁开关。
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
/* BalanceTask 唯一调用；内部获取 I2C mutex，写后三寄存器回读并复核全部 revision。 */
void BMS_Balance_RunOnce(uint32_t now_ms);
BMS_BalanceSnapshot_t BMS_Balance_GetSnapshot(void);

#endif /* BMS_BALANCE_H：头文件防重复包含 */
