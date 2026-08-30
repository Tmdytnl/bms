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
    BMS_FetTransactionState_t transaction_state; /* 最近一次事务可证明到什么程度。 */
    BQ76940_FetRequest_t requested; /* State 的运行意图，尚未应用安全 inhibit */
    BQ76940_FetRequest_t effective; /* 合并 Protect/State/Recovery inhibit 后的命令 */
    BQ76940_FetObserved_t observed; /* SYS_CTRL2 readback 解码出的寄存器观察值 */
    BMS_InhibitReasonBitmap_t inhibit_chg_reasons; /* 三个安全 owner 合并后的充电禁止位。 */
    BMS_InhibitReasonBitmap_t inhibit_dsg_reasons; /* 三个安全 owner 合并后的放电禁止位。 */
    uint32_t publication_revision; /* manager 自己的快照版本 */
    uint32_t protect_revision;     /* 本 transaction 捕获的安全输入版本 */
    uint32_t state_revision;       /* 本 transaction 捕获的 State 版本。 */
    uint32_t recovery_revision;    /* 本 transaction 捕获的 Recovery 版本。 */
    uint8_t expected_sys_ctrl2;    /* 合成后希望寄存器呈现的完整字节。 */
    uint8_t observed_sys_ctrl2;    /* 写后实际 readback 的完整字节。 */
    BQ76940_Status_t last_transport_status; /* 最近一次 BQ 传输/提交结果。 */
    bool register_state_confirmed; /* readback 与 expected 完全一致。 */
    bool enable_denied_by_quarantine; /* 已有 enable 意图被不确定状态隔离。 */
} BMS_FetManagerSnapshot_t;

/* 启动上下文绑定 BQ handle，并以 UNVERIFIED+BOTH inhibit 建立 fail-closed 初值。 */
void BMS_FetManager_Init(BQ76940_t *device);

/*
 * StateTask 是唯一正式调用者，FET Manager 是调度器启动后 SYS_CTRL2 CHG/DSG
 * sole writer。任何 Task 都只能发布 request/inhibit，不能直接打开 MOS。函数
 * 内部获取 runtime bus port，并在读/写前后复核三个 owner revision 与 measurement
 * identity；无返回值，事务结果通过一致诊断快照发布。
 */
void BMS_FetManager_Service(void);

/* 执行上下文只读 API；短 critical region 防止 transaction 字段被撕裂。 */
BMS_FetManagerSnapshot_t BMS_FetManager_GetSnapshot(void);

#if defined(TEST_PHASE9_IMAGE)
typedef void (*BMS_FetManagerTestHook_t)(void);
void BMS_FetManager_TestSetAfterReadHook(BMS_FetManagerTestHook_t hook);
void BMS_FetManager_TestSetAfterWriteHook(BMS_FetManagerTestHook_t hook);
#endif

#endif /* BMS_FET_MANAGER_H：头文件防重复包含 */
