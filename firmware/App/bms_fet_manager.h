#ifndef BMS_FET_MANAGER_H
#define BMS_FET_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_recovery.h"
#include "bms_state.h"

typedef enum
{
    /* 已回读确认 CHG/DSG 均为 off。 */
    BMS_FET_TRANSACTION_CONFIRMED_SAFE = 0,
    /* 已回读且输入 revision 仍一致，requested enable 正式生效。 */
    BMS_FET_TRANSACTION_CONFIRMED_APPLIED,
    /* transport/readback 无法证明寄存器状态。 */
    BMS_FET_TRANSACTION_UNVERIFIED,
    /* enable 写入提交点或 readback 不确定，禁止再次 enable。 */
    BMS_FET_TRANSACTION_QUARANTINED
} BMS_FetTransactionState_t;

typedef struct
{
    BMS_FetTransactionState_t transaction_state;
    BQ76940_FetRequest_t requested; /* State 的运行意图，尚未应用安全 inhibit */
    BQ76940_FetRequest_t effective; /* 合并 Protect/State/Recovery inhibit 后的命令 */
    BQ76940_FetObserved_t observed; /* SYS_CTRL2 readback 解码出的寄存器观察值 */
    BMS_InhibitReasonBitmap_t inhibit_chg_reasons;
    BMS_InhibitReasonBitmap_t inhibit_dsg_reasons;
    uint32_t publication_revision; /* manager 自己的快照版本 */
    uint32_t protect_revision;     /* 本 transaction 捕获的安全输入版本 */
    uint32_t state_revision;
    uint32_t recovery_revision;
    uint8_t expected_sys_ctrl2;
    uint8_t observed_sys_ctrl2;
    BQ76940_Status_t last_transport_status;
    bool register_state_confirmed;
    bool enable_denied_by_quarantine;
} BMS_FetManagerSnapshot_t;

void BMS_FetManager_Init(BQ76940_t *device);

/*
 * StateTask 是唯一正式调用者，FET Manager 是调度器启动后 SYS_CTRL2 CHG/DSG
 * sole writer。任何 Task 都只能发布 request/inhibit，不能直接打开 MOS。
 */
void BMS_FetManager_Service(void);

BMS_FetManagerSnapshot_t BMS_FetManager_GetSnapshot(void);

#if defined(TEST_PHASE9_IMAGE)
typedef void (*BMS_FetManagerTestHook_t)(void);
void BMS_FetManager_TestSetAfterReadHook(BMS_FetManagerTestHook_t hook);
void BMS_FetManager_TestSetAfterWriteHook(BMS_FetManagerTestHook_t hook);
#endif

#endif /* BMS_FET_MANAGER_H：include guard */
