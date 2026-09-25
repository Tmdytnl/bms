#ifndef FML_RECOVERY_H
#define FML_RECOVERY_H

#include <stdbool.h>
#include <stdint.h>

#include "fml_policy.h"
#include "fml_safety.h"
#include "bsp_bq76940.h"

typedef enum
{
    BMS_RECOVERY_PHASE_IDLE = 0, /* 启动稳态占位；尚未进入运行期 XREADY 恢复。 */
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
    BMS_RecoveryPhase_t phase;         /* 当前证据链阶段，不能跳步。 */
    uint32_t xready_generation;        /* 本轮恢复绑定的 Protect XREADY epoch。 */
    uint32_t recovery_revision;        /* 每次 reset evidence 递增的恢复事务身份。 */
    uint32_t publication_revision;     /* 任一对外可见恢复字段改变时递增。 */
    uint32_t first_valid_sample_sequence; /* handoff 后首个被接纳的完整帧。 */
    uint32_t first_valid_afe_generation;  /* 首帧必须与 xready_generation 相同。 */
    BMS_InhibitReasonBitmap_t inhibit_chg_reasons; /* 非 COMPLETE 全程含 RECOVERY。 */
    BMS_InhibitReasonBitmap_t inhibit_dsg_reasons; /* 非 COMPLETE 全程含 RECOVERY。 */
    BQ76940_Calibration_t calibration; /* clear 后重新读取、尚需 provenance handoff。 */
    BQ76940_Status_t last_transport_status; /* 最近失败点，供诊断而非自动跳阶段。 */
    bool recovery_in_progress;         /* 已观察 XREADY 且尚未完成整条证据链。 */
    bool post_clear_verified;          /* settle 后 SYS_STAT 与 blocking 位均通过复核。 */
    bool calibration_handed_off;       /* Sample owner 已接受同代 calibration。 */
    bool first_valid_sample_accepted;  /* 数据面已证明新配置真正可用。 */
    bool technical_ready;              /* 唯一允许上层解除 recovery inhibit 的结论。 */
} BMS_RecoverySnapshot_t;

/* runtime owner 启动时 startup calibration 已安装；后续 XREADY 走新 generation。 */
void FML_Recovery_Init(BQ76940_t *device,
                       const BMS_Policy_t *policy);

/*
 * StateTask 每次被周期等待或 urgent notification 唤醒时调用；最大有界等待取
 * policy->state.period_ms，当前为 100 ms。每次只推进一个有界 phase step，
 * 最多一次 I2C transaction，绝不持锁等待。新 generation 到达会丢弃旧
 * calibration/sample 证据并从 PRE_CLEAR_PREPARE 重启；FAILED 保持双向禁止，
 * 不自动伪装成 ready。
 */
/* 返回 true 表示 APL 应立即唤醒 ProtectTask 完成已授权的唯一写入。 */
bool FML_Recovery_Service(uint32_t now_ms);

/* FET/State/诊断只读；runtime critical region 保证 phase 与 identity 来自同一版。 */
BMS_RecoverySnapshot_t FML_Recovery_GetSnapshot(void);

#if defined(TEST_PHASE9_IMAGE)
typedef void (*BMS_RecoveryTestHook_t)(void);
/* 测试镜像设置校准交接前的竞态注入点。 */
void FML_Recovery_TestSetPreHandoffHook(BMS_RecoveryTestHook_t hook);
#endif

#endif /* FML_RECOVERY_H：头文件防重复包含 */
