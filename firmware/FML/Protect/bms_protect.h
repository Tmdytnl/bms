#ifndef BMS_PROTECT_H
#define BMS_PROTECT_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_fault.h"
#include "bms_policy.h"
#include "bms_safety.h"
#include "bq76940.h"
#include "bq76940_control.h"

/*
 * BMS V1 ProtectTask / ALERT 路径。
 *
 * TI BQ769x0 datasheet SLUSBK2I Rev.I §8.3.1.3 定义 SYS_STAT 为 W1C：
 * 写 1 只清对应事件位，写 0 保持不变。CC_READY(bit7) 表示新库仑采样；
 * DEVICE_XREADY(bit5) 表示器件异常或 SHIP→NORMAL，并由 AFE 自动清 CHG/DSG；
 * OVRD_ALERT(bit4) 是外部 ALERT override；bit3..0 为 UV/OV/SCD/OCD。
 *
 * 本模块把“寄存器事件”与“安全源生命周期”分开：
 * - H-01：OVRD_ALERT 独立捕获 fault、双向 inhibit 与 W1C，不被其他分支吞掉；
 * - H-02：CC_READY 只有在 CC read 与 newest-wins queue publish 成功后才清除；
 * - H-03：XREADY active+latched 且双向 inhibit，完整恢复成功后才允许 W1C；
 * - H-05：ALERT 采用有界 drain/retry，电平持续为高时不依赖下一次边沿。
 *
 * SYS_STAT=0 只表示当前寄存器位低，不证明电压/电流条件已经恢复。HW_OV、
 * HW_UV、HW_OCD 的 active 只能由带 identity 的 recovery request/ack 清除；
 * HW_SCD、AFE_XREADY、AFE_OVRD_ALERT 还具有独立 latched 生命周期。
 * ProtectTask 是这些 HW/AFE source 的唯一发布 owner，也是运行期 XREADY W1C
 * 唯一执行者；其他任务只能读取快照或提交请求，不能从寄存器低电平推断授权。
 *
 * ALERT ISR 只投递最小事件通知，复杂 I2C、W1C、fault 与恢复判断全部留在
 * ProtectTask 任务上下文，避免 ISR 持锁、阻塞或破坏 owner 顺序。
 */

/* SYS_STAT bit mask；寄存器地址在 regs.h，事件语义保持在 Protect owner 内。 */
#define BMS_PROTECT_STAT_CC_READY       ((uint8_t)0x80U)
#define BMS_PROTECT_STAT_DEVICE_XREADY  ((uint8_t)0x20U)
#define BMS_PROTECT_STAT_OVRD_ALERT     ((uint8_t)0x10U)
#define BMS_PROTECT_STAT_UV             ((uint8_t)0x08U)
#define BMS_PROTECT_STAT_OV             ((uint8_t)0x04U)
#define BMS_PROTECT_STAT_SCD            ((uint8_t)0x02U)
#define BMS_PROTECT_STAT_OCD            ((uint8_t)0x01U)

/* ALERT 路径获取 I2C mutex 的最长等待；超时后保留 pending 并重试。 */
#define BMS_PROTECT_I2C_TIMEOUT_MS      (20U)

/* 每次 ALERT 唤醒的 drain retry 上限，防止最高优先级任务无限占用 CPU。 */
#define BMS_PROTECT_DRAIN_MAX_ITER      (4U)

/*
 * 任务级 retry 间隔：即使 ALERT stuck-high 或 I2C mutex 忙，也不会形成最高
 * 优先级 busy loop；同时 retry 不依赖另一个 EXTI edge 才能继续。
 */
#define BMS_PROTECT_RETRY_DELAY_MS      (10U)
#define BMS_PROTECT_HEALTH_WAIT_MS       (100U)

typedef enum
{
    BMS_PROTECT_DRAIN_COMPLETE = 0, /* 本轮读到稳定低电平，暂无已知未处理工作。 */
    BMS_PROTECT_DRAIN_RETRY_REQUIRED /* 仍高、锁忙或事务失败，必须短延时后续跑。 */
} BMS_ProtectDrainResult_t;

typedef enum
{
    BMS_PROTECT_SERVICE_IDLE = 0,          /* ALERT 低且 drain 完成，可回到 semaphore 等待。 */
    BMS_PROTECT_SERVICE_RETRY_REQUIRED     /* 保持 task-level pending，不依赖新边沿。 */
} BMS_ProtectServiceResult_t;

typedef struct
{
    uint32_t cc_queue_overflow_count; /* CC 队列曾满且触发 newest-wins 的次数。 */
    /* queue 满时为 newest sample 腾位而不可恢复丢弃的 oldest sample 数。 */
    uint32_t cc_sample_missed_count;
    /* overflow recovery 后 newest enqueue 仍失败的次数；此时保留 CC_READY 供重试。 */
    uint32_t cc_enqueue_failure_count;
    /*
     * SYS_STAT W1C 的 payload/CRC 已 ACK、但最终 STOP 失败的 transaction 数。
     * 这是“提交点未知”的历史，不能解释为寄存器已经写入。
     */
    uint32_t w1c_finalization_ambiguous_count;
    /*
     * 上述 ambiguous transaction 中包含 CC_READY 的子集；非零表示事件 identity
     * 可能合并，SOC 不能跨过该边界宣称精确无损积分。
     */
    uint32_t cc_event_identity_ambiguous_count;
    /*
     * 当前 quarantine 的 SYS_STAT 位。观察到低电平前既不 replay W1C，也不把
     * 持续高位当成新事件；软件无法区分“旧位未清”与“刚到达的新同类事件”。
     */
    uint8_t w1c_finalization_ambiguous_mask;
    bool cc_queue_overflow_latched;          /* 曾发生队列拥塞，供精度降级诊断。 */
    bool w1c_finalization_ambiguous_latched; /* 曾无法确认 W1C 最终提交状态。 */
} BMS_ProtectDiagnostics_t;

/*
 * xCcSampleQueue 已接受的最新 current-epoch CC sample 的 scheduler-coherent
 * mirror。SampleTask 只读此 mailbox，绝不 receive/peek SOC-owned queue，也不
 * 进行第二次 CC register read。sequence 是 mailbox generation tag；
 * xready_generation 把读数绑定到 AFE epoch，XREADY 转移会立即使旧 mailbox
 * 对 SampleTask 失效，即使 SOC queue 仍按自身契约处理已入队记录。
 */
typedef struct
{
    int16_t raw;                 /* Protect 从当前 CC_READY 事件读取的原始值。 */
    uint32_t sample_ms;          /* APL 传入的接纳时刻，已经是毫秒域。 */
    uint32_t xready_generation;  /* 样本所属 AFE 生命周期。 */
    uint32_t transport_id;       /* FML/APL 两阶段交接的单调身份。 */
} BMS_CcSample_t;

typedef struct
{
    int16_t raw;                 /* 已被 Protect 接纳的有符号 CC 原始值。 */
    uint32_t sample_ms;          /* 读取/入队时刻，Sample 用于形成 timestamp。 */
    uint32_t sequence;           /* mailbox 每次接受新样本递增，避免重复发布。 */
    uint32_t xready_generation;  /* 样本所属 AFE 生命周期。 */
    bool valid;                  /* false 表示尚无样本或已被 XREADY 立即失效。 */
} BMS_ProtectLatestCc_t;

/*
 * scheduler-coherent XREADY epoch。generation 只在第一次 inactive→active
 * 观察时推进并自然回绕；恢复后清 active 不会倒退代号。相等检查可以拒绝跨过
 * 一次已观察转移的 binding，包括 UINT32_MAX→0；完整 2^32 事件 alias 由
 * watchdog 与及时 rebinding 的系统约束覆盖。
 */
typedef struct
{
    uint32_t xready_generation; /* 每次首次观察 inactive→active 时递增。 */
    bool active;                /* 当前 XREADY 恢复链是否仍未完成。 */
} BMS_ProtectXreadyState_t;

typedef enum
{
    BMS_PROTECT_SOURCE_HW_OV = 0, /* AFE 单体过压 source identity。 */
    BMS_PROTECT_SOURCE_HW_UV,     /* AFE 单体欠压 source identity。 */
    BMS_PROTECT_SOURCE_HW_OCD,    /* AFE 放电过流 source identity。 */
    BMS_PROTECT_SOURCE_HW_SCD,    /* AFE 放电短路 source identity。 */
    BMS_PROTECT_SOURCE_COUNT      /* source_generation 数组边界。 */
} BMS_ProtectSourceId_t;

typedef struct
{
    BMS_FaultSummary_t faults;            /* active=当前未恢复；latched=需独立动作释放。 */
    BMS_InhibitReasonBitmap_t inhibit_chg_reasons; /* Protect 权威充电禁止原因。 */
    BMS_InhibitReasonBitmap_t inhibit_dsg_reasons; /* Protect 权威放电禁止原因。 */
    uint32_t publication_revision;        /* 任一安全可见字段改变时递增。 */
    uint32_t source_generation[BMS_PROTECT_SOURCE_COUNT]; /* 各硬件源事件身份。 */
    uint32_t xready_generation;           /* 当前/最近 XREADY epoch。 */
    bool xready_active;                   /* 恢复未完成时始终 BOTH inhibit。 */
} BMS_ProtectSafetySnapshot_t;

typedef struct
{
    uint32_t xready_generation; /* 只授权清当前 generation。 */
    uint32_t recovery_revision; /* 发起授权的 Recovery transaction。 */
    bool valid;                 /* 单次消费授权，Protect W1C 后作废。 */
} BMS_ProtectXreadyClearAuthorization_t;

typedef struct
{
    uint32_t xready_generation;   /* ack 对应的 AFE epoch。 */
    uint32_t recovery_revision;   /* ack 对应的恢复事务。 */
    uint32_t protect_revision;    /* clear 结果进入安全快照后的版本。 */
    bool accepted;                /* W1C 提交明确且 active 已清。 */
    bool finalization_ambiguous;  /* STOP 失败导致提交点未知，禁止盲目重放。 */
} BMS_ProtectXreadyClearAck_t;

typedef struct
{
    BMS_FaultId_t fault_id;              /* 只允许 HW_OV/HW_UV/HW_OCD。 */
    uint32_t request_id;                 /* 每次资格完成形成的新请求身份。 */
    uint32_t expected_source_generation; /* 必须仍是被评估的那次硬件事件。 */
    uint32_t evaluated_sample_sequence;  /* 恢复证据读取的完整测量序号。 */
    uint32_t evaluated_afe_generation;   /* 恢复证据所属 AFE 生命周期。 */
    uint32_t qualification_revision;     /* HwRecovery 连续资格窗口版本。 */
    uint32_t expiry_ms;                  /* 超时后即使字段相同也不再接受。 */
    bool valid;                          /* false 表示没有可消费请求。 */
} BMS_ProtectHwRecoveryRequest_t;

typedef struct
{
    BMS_FaultId_t fault_id; /* 本次确认针对的硬件故障类型。 */
    uint32_t request_id; /* 与提交请求精确匹配的身份。 */
    uint32_t source_generation; /* 被释放的硬件事件世代。 */
    uint32_t qualification_revision; /* State 形成的恢复资格版本。 */
    uint32_t protect_revision; /* 接纳结果发布后的 Protect 版本。 */
    bool accepted; /* 请求身份与当前证据均匹配且已由 owner 接纳。 */
} BMS_ProtectHwRecoveryAck_t;

typedef enum
{
    BMS_SERVICE_RESET_HW_SCD = 0,       /* 释放短路动作锁存的受限服务请求。 */
    BMS_SERVICE_RESET_AFE_OVRD_ALERT,   /* 释放 ALERT override 锁存。 */
    BMS_SERVICE_RESET_AFE_COMM,         /* 释放满足策略条件的通信历史锁存。 */
    BMS_SERVICE_RESET_SOURCE_COUNT      /* 服务源枚举边界。 */
} BMS_ServiceResetSource_t;

typedef struct
{
    BMS_ServiceResetSource_t source; /* 仅允许指定受限服务源。 */
    uint32_t request_id; /* 本次服务请求的单调身份。 */
    uint32_t evaluated_sample_sequence; /* 请求资格对应的完整测量序号。 */
    uint32_t evaluated_afe_generation; /* 请求资格对应的 AFE 世代。 */
    uint32_t qualification_revision; /* 连续资格窗口的修订号。 */
    uint32_t expiry_ms; /* 超过此时刻的请求不得接纳。 */
    bool valid; /* false 表示没有可消费的请求。 */
} BMS_ServiceResetRequest_t;

typedef struct
{
    BMS_ServiceResetSource_t source; /* 被处理的受限服务源。 */
    uint32_t request_id; /* 与提交请求精确匹配的身份。 */
    uint32_t protect_revision; /* 应答发布后的 Protect 快照版本。 */
    bool accepted; /* owner 已接纳并处理请求。 */
} BMS_ServiceResetAck_t;

/* 正式代码与 production-C wrap regression 共用的模加 generation 推进。 */
#define BMS_PROTECT_CC_SEQUENCE_NEXT(sequence_) \
    ((uint32_t)((uint32_t)(sequence_) + 1UL))
#define BMS_PROTECT_XREADY_GENERATION_NEXT(generation_) \
    ((uint32_t)((uint32_t)(generation_) + 1UL))

/*
 * XREADY recovery hook 只有在 device re-init、settle、calibration reload、
 * protection/config re-apply+readback 与 status-group verify 全部完成后才能
 * 返回 true。hook 调用时持有 I2C mutex，因此每一步必须短小、非阻塞；需要等待
 * 的阶段返回 false，由外部 state machine 下次推进，绝不能持锁 delay。
 */
typedef bool (*BMS_ProtectXreadyRecoveryHook_t)(BQ76940_t *device);

/* 调度器启动前初始化 fault、双向 inhibit、mailbox 与 recovery identity。 */
void BMS_Protect_Init(void);

/* 绑定 ALERT drain 共用的 BQ transport handle；指向对象必须覆盖任务生命周期。 */
void BMS_Protect_SetDevice(BQ76940_t *device);
/* 绑定已验证的不可变硬件保护策略。 */
void BMS_Protect_SetPolicy(const BMS_Policy_t *policy, uint32_t now_ms);

/* 绑定 XREADY 恢复通知回调，不交出 W1C 所有权。 */
void BMS_Protect_SetXreadyRecoveryHook(
    BMS_ProtectXreadyRecoveryHook_t recovery_hook);

/*
 * 返回同一 publication generation 的 active+latched 快照。任务上下文通过
 * runtime critical region 防止 torn read，ISR 不得调用；返回值只读，不能由
 * SYS_STAT=0 推导恢复授权。
 */
BMS_FaultSummary_t BMS_Protect_GetFaultSummary(void);

/*
 * FET Manager 直接消费的 Protect-owned 权威方向性 inhibit 快照。API 在 scheduler
 * exclusion 中复制 owner state，再在局部构造 inhibit；调用者只读且无需 I2C mutex。
 * active 表示当前条件未解决，latched 表示事件历史仍要求特定恢复/服务动作，两者
 * 不能因为一次 SYS_STAT 读低就一起清除。
 */
BMS_ProtectSafetySnapshot_t BMS_Protect_GetSafetySnapshot(void);

/* H-02 诊断快照在任务上下文一致读取；计数器在 UINT32_MAX 饱和而不回绕。 */
BMS_ProtectDiagnostics_t BMS_Protect_GetDiagnostics(void);

/*
 * 纯 decision function（无 I2C/RTOS），把同一 SYS_STAT snapshot 映射为 fault、
 * FET request 与 W1C mask，便于 production-C test image 直接覆盖每一 bit：
 * stat=原始寄存器；faults/request=输入输出状态；clear_mask=已完成处理的 W1C 位。
 */
void BMS_Protect_Decide(uint8_t stat,
                        BMS_FaultSummary_t *faults,
                        BQ76940_FetRequest_t *request,
                        uint8_t *clear_mask);

/* 纯判断 SYS_STAT 是否含 fault-class bit，而非只有 CC_READY，用于紧急唤醒。 */
bool BMS_Protect_HasFaultBits(uint8_t stat);

/*
 * 以 H-05 有界策略 drain 一轮 SYS_STAT。测试与任务驱动同一正式路径；每个
 * set bit 独立处理，只把已成功接纳/捕获的事件加入 W1C mask。
 */
BMS_ProtectDrainResult_t BMS_Protect_Drain(BQ76940_t *device,
                                           uint32_t now_ms);

/*
 * 执行一次有界任务级 service。RETRY 表示保留 pending、短暂 delay 后直接再调，
 * 不等待另一个 semaphore edge；这样 ALERT 持续为高也不会丢失工作。
 */
BMS_ProtectServiceResult_t BMS_Protect_ServicePending(BQ76940_t *device,
                                                      uint32_t now_ms);
/* 在 ALERT 之外继续推进待决 W1C、通信资格和请求应答。 */
void BMS_Protect_ServiceMaintenance(uint32_t now_ms);

/*
 * CC_READY 的两阶段交接。FML 先暂存带 transport identity 的 domain sample；
 * APL Protect task 执行 newest-wins queue transport，再回报结果。只有确认 newest
 * 已进入 APL queue，Protect 才授权后续 W1C CC_READY，保持 H-02 提交顺序。
 */
bool BMS_Protect_GetPendingCcSample(BMS_CcSample_t *sample);
/* 接纳 APL 的队列交接结果，成功后才允许清除当前 CC_READY。 */
bool BMS_Protect_CompleteCcTransport(uint32_t transport_id,
                                     bool inserted,
                                     bool overflowed,
                                     bool oldest_was_dropped);

/*
 * 在 runtime critical region 内复制 current-epoch latest CC。NULL、XREADY active、
 * 尚无本代 mailbox 或 epoch 不匹配时返回 false；非 NULL 输出会明确 valid=false。
 */
bool BMS_Protect_GetLatestCc(BMS_ProtectLatestCc_t *snapshot);

/* 任务上下文一致复制 XREADY generation/active；仅 NULL 输出失败，ISR 不得调用。 */
bool BMS_Protect_GetXreadyState(BMS_ProtectXreadyState_t *snapshot);

/* SampleTask 与 generation-wrap test 共用的纯 binding 判断。 */
bool BMS_Protect_XreadyBindingIsCurrent(
    const BMS_ProtectXreadyState_t *state,
    uint32_t bound_generation);

/*
 * 先完成 H-03 权威 recovery contract，之后才 W1C XREADY。只有最终 STOP 明确
 * 成功，或先前 ambiguous transaction 经 observed-low 消歧后才清 active；
 * latched 生命周期独立保留，不能随 active 一起顺手清除。
 */
bool BMS_Protect_RecoverXready(BQ76940_t *device, uint32_t now_ms);

/* Recovery Coordinator 只发 request；Protect 仍是运行期 W1C sole owner。 */
bool BMS_Protect_AuthorizeXreadyClear(uint32_t xready_generation,
                                     uint32_t recovery_revision);
/* 读取 Protect 对指定 XREADY 清除请求的身份绑定应答。 */
bool BMS_Protect_GetXreadyClearAck(BMS_ProtectXreadyClearAck_t *ack);

/* 按当前策略执行 source-specific XREADY action-latch release。 */
bool BMS_Protect_ReleaseXreadyActionLatch(uint32_t xready_generation,
                                         uint32_t recovery_revision);

/* State qualification 与 Protect fresh-status 组成两方 HW recovery 证据。 */
bool BMS_Protect_SubmitHwRecoveryRequest(
    const BMS_ProtectHwRecoveryRequest_t *request,
    uint32_t now_ms);
/* 读取 Protect 对硬件故障释放请求的身份绑定应答。 */
bool BMS_Protect_GetHwRecoveryAck(BMS_ProtectHwRecoveryAck_t *ack);

/* source-specific service reset request；不存在通用 bitmap clear command。 */
bool BMS_Protect_SubmitServiceResetRequest(
    const BMS_ServiceResetRequest_t *request);
/* 读取 Protect 对服务重置请求的身份绑定应答。 */
bool BMS_Protect_GetServiceResetAck(BMS_ServiceResetAck_t *ack);

#if defined(TEST_PHASE7_IMAGE) || defined(TEST_PHASE9_IMAGE)
/* 测试镜像直接推进 AFE 通信故障策略窗口。 */
void BMS_Protect_TestUpdateAfeCommPolicy(uint32_t now_ms);
#endif
#if defined(TEST_PHASE7_IMAGE)
/* 测试镜像暂存一条 CC 样本以覆盖交接边界。 */
bool BMS_Protect_TestStageCcSample(int16_t raw, uint32_t sample_ms);
#endif

#endif /* BMS_PROTECT_H：头文件防重复包含 */
