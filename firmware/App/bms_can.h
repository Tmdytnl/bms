#ifndef BMS_CAN_H
#define BMS_CAN_H

#include <stdbool.h>
#include <stdint.h>

#include "app_rtos.h"
#include "bms_data.h"
#include "bms_fet_manager.h"
#include "bms_policy.h"
#include "bms_protect.h"
#include "bms_recovery.h"
#include "bms_state.h"

#define BMS_CAN_PROTOCOL_VERSION                  (1U)
#define BMS_CAN_TX_FRAME_COUNT                    (6U)
#define BMS_CAN_SERVICE_MAGIC                     (0xA5U)
#define BMS_CAN_SERVICE_RESET_COMMAND             (0x01U)

typedef struct
{
    uint32_t tx_cycle_count;       /* 完成一次六帧编码尝试。 */
    uint32_t tx_enqueued_count;    /* 成功进入软件 TX queue 的帧数。 */
    uint32_t tx_drop_count;        /* 软件队列满导致的诊断帧丢弃。 */
    uint32_t rx_valid_count;       /* 通过 0x280 格式/时效/identity 解码。 */
    uint32_t rx_invalid_count;     /* 非法、过期或无法绑定当前数据的接收帧。 */
    uint32_t service_request_count;/* Protect owner 接受的受限服务请求。 */
    uint32_t service_reject_count; /* decode 合法但 owner 因当前状态拒绝。 */
    uint32_t target_init_failure_count;
    uint32_t target_tx_count;
    uint32_t target_tx_drop_count;
    uint32_t target_rx_fifo_overrun_count;
    uint32_t target_rx_queue_drop_count;
    uint32_t target_bus_off_recovery_count;
} BMS_CanDiagnostics_t;

/*
 * 明确逐字节 wire encoding，禁止直接序列化 shared C snapshot。六帧职责：
 * `0x180` 运行状态、FET/recovery 标志、fault-present 与 sample identity；
 * `0x181` pack、电流、SOC 与剩余容量；`0x182` 最低/最高单体、温度与位置；
 * `0x183` active/latched fault bitmap；`0x184` cell1..7 压缩值；
 * `0x185` cell8..13 压缩值、AFE generation 与 sample sequence。
 * encoder 只生成诊断帧，不拥有其中任何源状态。
 */
uint8_t BMS_Can_BuildTxFrames(
    const BMS_DataSnapshot_t *measurement,
    const BMS_StateSafetySnapshot_t *state,
    const BMS_ProtectSafetySnapshot_t *protect,
    const BMS_RecoverySnapshot_t *recovery,
    const BMS_FetManagerSnapshot_t *fet,
    BMS_CanFrame_t frames[BMS_CAN_TX_FRAME_COUNT]);

/*
 * `0x280` 只解码为带 freshness/identity 的 source-specific Protect request。
 * CAN 接收不能写 FET、CELLBAL、fault bitmap 或 IWDG，服务帧也必须通过原 owner。
 * 返回 true 只表示生成 identity-bound request，并不表示服务动作已获批准或完成。
 */
bool BMS_Can_DecodeServiceReset(
    const BMS_CanFrame_t *frame,
    uint32_t received_ms,
    uint32_t now_ms,
    const BMS_Policy_t *policy,
    const BMS_DataIdentity_t *identity,
    uint32_t qualification_revision,
    BMS_ServiceResetRequest_t *request);

void BMS_Can_Init(const BMS_Policy_t *policy);
bool BMS_Can_BindTarget(const BMS_Policy_t *policy);
bool BMS_Can_EnableTargetRx(void);
void BMS_Can_TxHardwareService(uint32_t now_ms);
void BMS_Can_TxRunOnce(uint32_t now_ms);
void BMS_Can_RxProcess(const BMS_CanFrame_t *frame,
                       uint32_t received_ms,
                       uint32_t now_ms);
BMS_CanDiagnostics_t BMS_Can_GetDiagnostics(void);

/* bxCAN FIFO0 ISR 只把硬件 frame 放入 xCanRxQueue；decode/request 留在 CANRxTask。 */
void USB_LP_CAN1_RX0_IRQHandler(void);

#endif /* BMS_CAN_H：头文件防重复包含 */
