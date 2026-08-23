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
    uint32_t tx_cycle_count;
    uint32_t tx_enqueued_count;
    uint32_t tx_drop_count;
    uint32_t rx_valid_count;
    uint32_t rx_invalid_count;
    uint32_t service_request_count;
    uint32_t service_reject_count;
} BMS_CanDiagnostics_t;

/* Explicit wire encoding; the shared C snapshot is never serialized raw. */
uint8_t BMS_Can_BuildTxFrames(
    const BMS_DataSnapshot_t *measurement,
    const BMS_StateSafetySnapshot_t *state,
    const BMS_ProtectSafetySnapshot_t *protect,
    const BMS_RecoverySnapshot_t *recovery,
    const BMS_FetManagerSnapshot_t *fet,
    BMS_CanFrame_t frames[BMS_CAN_TX_FRAME_COUNT]);

/* Decode to a source-specific Protect request. It cannot write FETs,
 * CELLBAL, fault bitmaps, or the watchdog. */
bool BMS_Can_DecodeServiceReset(
    const BMS_CanFrame_t *frame,
    uint32_t received_ms,
    uint32_t now_ms,
    const BMS_Policy_t *policy,
    const BMS_DataIdentity_t *identity,
    uint32_t qualification_revision,
    BMS_ServiceResetRequest_t *request);

void BMS_Can_Init(const BMS_Policy_t *policy);
void BMS_Can_TxRunOnce(uint32_t now_ms);
void BMS_Can_RxProcess(const BMS_CanFrame_t *frame,
                       uint32_t received_ms,
                       uint32_t now_ms);
BMS_CanDiagnostics_t BMS_Can_GetDiagnostics(void);

#endif /* BMS_CAN_H */
