#ifndef BMS_HW_RECOVERY_H
#define BMS_HW_RECOVERY_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_data.h"
#include "bms_policy.h"
#include "bms_protect.h"

typedef struct
{
    bool tracking[BMS_PROTECT_SOURCE_COUNT];              /* source 是否正在恢复计时 */
    uint32_t started_ms[BMS_PROTECT_SOURCE_COUNT];        /* hysteresis 条件首次满足时刻 */
    uint32_t tracked_generation[BMS_PROTECT_SOURCE_COUNT];/* 被证明的 source 生命周期 */
    uint32_t request_id;                                  /* 每次 handshake 的唯一身份 */
    uint32_t qualification_revision;                      /* measurement qualification 版本 */
} BMS_HwRecoveryEngine_t;

/* StateTask 启动时初始化纯内存 engine；不读取寄存器、不清 fault。 */
void BMS_HwRecovery_Init(BMS_HwRecoveryEngine_t *engine);

/*
 * StateTask-owned 纯 qualification。SYS_STAT 变低只表示 event bit 低，不能证明
 * OV/UV/OCD 条件已恢复；必须用 fresh、同 AFE generation 的 measurement 持续
 * 满足 threshold+hysteresis+delay，生成含 source_generation/request_id/evidence
 * expiry 的 request。Protect 再以 fresh status read 返回匹配 ack。SCD 仍只允许
 * source-specific service reset。返回 true 只表示产出一个 request，不表示 fault
 * 已清；真正 owner 仍是 ProtectTask。
 */
bool BMS_HwRecovery_Evaluate(
    BMS_HwRecoveryEngine_t *engine,
    const BMS_Policy_t *policy,
    const BMS_ProtectSafetySnapshot_t *protect,
    const BMS_DataSnapshot_t *measurement,
    uint32_t now_ms,
    BMS_ProtectHwRecoveryRequest_t *request);

#endif /* BMS_HW_RECOVERY_H：头文件防重复包含 */
