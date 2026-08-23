#ifndef BMS_RECOVERY_H
#define BMS_RECOVERY_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_policy.h"
#include "bms_safety.h"
#include "bq76940.h"

typedef enum
{
    BMS_RECOVERY_PHASE_IDLE = 0,
    BMS_RECOVERY_PHASE_PRE_CLEAR_PREPARE,
    BMS_RECOVERY_PHASE_PRE_CLEAR_READY,
    BMS_RECOVERY_PHASE_WAIT_CLEAR_ACK,
    BMS_RECOVERY_PHASE_POST_CLEAR_CONFIG,
    BMS_RECOVERY_PHASE_POST_CLEAR_SETTLE,
    BMS_RECOVERY_PHASE_POST_CLEAR_VERIFY,
    BMS_RECOVERY_PHASE_CALIBRATION_HANDOFF,
    BMS_RECOVERY_PHASE_WAIT_FIRST_VALID_SAMPLE,
    BMS_RECOVERY_PHASE_COMPLETE,
    BMS_RECOVERY_PHASE_FAILED
} BMS_RecoveryPhase_t;

typedef struct
{
    BMS_RecoveryPhase_t phase;
    uint32_t xready_generation;
    uint32_t recovery_revision;
    uint32_t publication_revision;
    uint32_t first_valid_sample_sequence;
    uint32_t first_valid_afe_generation;
    BMS_InhibitReasonBitmap_t inhibit_chg_reasons;
    BMS_InhibitReasonBitmap_t inhibit_dsg_reasons;
    BQ76940_Calibration_t calibration;
    BQ76940_Status_t last_transport_status;
    bool recovery_in_progress;
    bool post_clear_verified;
    bool calibration_handed_off;
    bool first_valid_sample_accepted;
    bool technical_ready;
} BMS_RecoverySnapshot_t;

/* Startup calibration is already installed before this runtime owner starts. */
void BMS_Recovery_Init(BQ76940_t *device,
                       const BMS_Policy_t *policy);

/* One bounded StateTask-owned phase step; at most one I2C transaction. */
void BMS_Recovery_Service(uint32_t now_ms);

BMS_RecoverySnapshot_t BMS_Recovery_GetSnapshot(void);

#if defined(TEST_PHASE9_IMAGE)
typedef void (*BMS_RecoveryTestHook_t)(void);
void BMS_Recovery_TestSetPreHandoffHook(BMS_RecoveryTestHook_t hook);
#endif

#endif /* BMS_RECOVERY_H */
