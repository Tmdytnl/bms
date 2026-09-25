#ifndef FML_CAN_CODEC_H
#define FML_CAN_CODEC_H

#include <stdint.h>

#include "fml_can.h"
#include "fml_data.h"
#include "fml_fet_manager.h"
#include "fml_protect.h"
#include "fml_recovery.h"
#include "fml_state.h"

/*
 * 把权威 owner 的只读快照按字节编码为六帧诊断报文；禁止直接序列化 C struct。
 * 0x180/181/182/183/184/185 分别承载状态、包量、极值、故障和两组电芯值。
 * 本函数只读取输入，不取得任何源状态或 FET 写权限。
 */
uint8_t FML_Can_BuildTxFrames(
    const BMS_DataSnapshot_t *measurement,
    const BMS_StateSafetySnapshot_t *state,
    const BMS_ProtectSafetySnapshot_t *protect,
    const BMS_RecoverySnapshot_t *recovery,
    const BMS_FetManagerSnapshot_t *fet,
    BMS_CanFrame_t frames[BMS_CAN_TX_FRAME_COUNT]);

/*
 * 校验 0x280 服务帧的格式、时效和测量身份，仅生成 Protect 可审核的请求。
 * true 不表示 owner 已接纳或执行重置；函数不写 FET、fault 或硬件寄存器。
 */
bool FML_Can_DecodeServiceReset(
    const BMS_CanFrame_t *frame,
    uint32_t received_ms,
    uint32_t now_ms,
    const BMS_Policy_t *policy,
    const BMS_DataIdentity_t *identity,
    uint32_t qualification_revision,
    BMS_ServiceResetRequest_t *request);

#endif /* FML_CAN_CODEC_H */
