#ifndef BMS_FET_MANAGER_H
#define BMS_FET_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_recovery.h"
#include "bms_state.h"

typedef enum
{
    BMS_FET_TRANSACTION_CONFIRMED_SAFE = 0,
    BMS_FET_TRANSACTION_CONFIRMED_APPLIED,
    BMS_FET_TRANSACTION_UNVERIFIED,
    BMS_FET_TRANSACTION_QUARANTINED
} BMS_FetTransactionState_t;

typedef struct
{
    BMS_FetTransactionState_t transaction_state;
    BQ76940_FetRequest_t requested;
    BQ76940_FetRequest_t effective;
    BQ76940_FetObserved_t observed;
    BMS_InhibitReasonBitmap_t inhibit_chg_reasons;
    BMS_InhibitReasonBitmap_t inhibit_dsg_reasons;
    uint32_t publication_revision;
    uint32_t protect_revision;
    uint32_t state_revision;
    uint32_t recovery_revision;
    uint8_t expected_sys_ctrl2;
    uint8_t observed_sys_ctrl2;
    BQ76940_Status_t last_transport_status;
    bool register_state_confirmed;
    bool enable_denied_by_quarantine;
} BMS_FetManagerSnapshot_t;

void BMS_FetManager_Init(BQ76940_t *device);

/* StateTask is the only production caller of this scheduler-era writer. */
void BMS_FetManager_Service(void);

BMS_FetManagerSnapshot_t BMS_FetManager_GetSnapshot(void);

#if defined(TEST_PHASE9_IMAGE)
typedef void (*BMS_FetManagerTestHook_t)(void);
void BMS_FetManager_TestSetAfterReadHook(BMS_FetManagerTestHook_t hook);
void BMS_FetManager_TestSetAfterWriteHook(BMS_FetManagerTestHook_t hook);
#endif

#endif /* BMS_FET_MANAGER_H */
