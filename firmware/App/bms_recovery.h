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
    /* 等待 FET/CELLBAL 两个 sole writer 回读确认全关。 */
    BMS_RECOVERY_PHASE_PRE_CLEAR_PREPARE,
    /* 安全前置条件已满足，可以向 Protect 请求本 generation 的 W1C。 */
    BMS_RECOVERY_PHASE_PRE_CLEAR_READY,
    /* 等待 Protect 返回完全匹配 generation/revision 的 clear ack。 */
    BMS_RECOVERY_PHASE_WAIT_CLEAR_ACK,
    /* XREADY 清除后逐项重写并回读 AFE configuration。 */
    BMS_RECOVERY_PHASE_POST_CLEAR_CONFIG,
    /* 不持 I2C mutex 等待 AFE settle，避免阻塞 Protect/Sample。 */
    BMS_RECOVERY_PHASE_POST_CLEAR_SETTLE,
    /* 新读 SYS_STAT，确认 XREADY 与 blocking source 没有重新出现。 */
    BMS_RECOVERY_PHASE_POST_CLEAR_VERIFY,
    /* 把带 provenance 的新代 calibration 交给 Sample owner。 */
    BMS_RECOVERY_PHASE_CALIBRATION_HANDOFF,
    /* 等待新 generation 首个完整有效 sample，旧代 sample 不计入证据。 */
    BMS_RECOVERY_PHASE_WAIT_FIRST_VALID_SAMPLE,
    /* 完整证据链成立，释放 recovery BOTH inhibit。 */
    BMS_RECOVERY_PHASE_COMPLETE,
    /* 任一步失败；保持双向 inhibit，不能降级为“技术就绪”。 */
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

/* runtime owner 启动时 startup calibration 已安装；后续 XREADY 走新 generation。 */
void BMS_Recovery_Init(BQ76940_t *device,
                       const BMS_Policy_t *policy);

/* StateTask 每次只推进一个有界 phase step，最多一次 I2C transaction，绝不持锁等待。 */
void BMS_Recovery_Service(uint32_t now_ms);

BMS_RecoverySnapshot_t BMS_Recovery_GetSnapshot(void);

#if defined(TEST_PHASE9_IMAGE)
typedef void (*BMS_RecoveryTestHook_t)(void);
void BMS_Recovery_TestSetPreHandoffHook(BMS_RecoveryTestHook_t hook);
#endif

#endif /* BMS_RECOVERY_H：include guard */
