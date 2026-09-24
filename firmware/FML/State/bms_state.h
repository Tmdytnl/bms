#ifndef BMS_STATE_H
#define BMS_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_build_assert.h"
#include "bms_policy.h"
#include "bms_safety.h"
#include "bq76940_control.h"

struct BMS_DataSnapshot;
typedef struct BMS_DataSnapshot BMS_DataSnapshot_t;

/*
 * BMS application state 只做运行分类，不是 FET 最终安全许可：
 * INIT 等待初始有效证据；STANDBY 表示充放电意图都未稳定成立；CHARGE /
 * DISCHARGE 表示经 qualify 的运行方向；FAULT 表示至少一个状态层安全源 active。
 * 即使 state=FAULT，也必须由 directional inhibit + FET Manager 决定 CHG/DSG；
 * BQ SHIP/NORMAL 则是 AFE device mode，不能与本 enum 混用。
 */
typedef enum
{
    BMS_STATE_INIT = 0,       /* 启动证据尚未齐备，不能据此允许 MOS。 */
    BMS_STATE_STANDBY = 1,    /* 电流未持续满足充电或放电方向判据。 */
    BMS_STATE_CHARGE = 2,     /* 充电方向已经通过进入资格计时。 */
    BMS_STATE_DISCHARGE = 3,  /* 放电方向已经通过进入资格计时。 */
    BMS_STATE_FAULT = 4,      /* 存在状态层 fault；具体 FET 动作仍看方向 inhibit。 */
    BMS_STATE_COUNT = 5       /* 枚举边界，只用于编译期/遍历检查。 */
} BMS_State_t;

BMS_BUILD_ASSERT(BMS_STATE_INIT == 0,
                 state_init_value_is_stable);
BMS_BUILD_ASSERT(BMS_STATE_FAULT == 4,
                 state_fault_value_is_stable);
BMS_BUILD_ASSERT(BMS_STATE_COUNT == 5,
                 state_count_is_five);

typedef struct
{
    bool active;                /* 已通过触发 debounce 的权威活动状态。 */
    bool assert_tracking;       /* 触发条件正在连续计时，尚未成为 active。 */
    bool recovery_tracking;     /* 恢复条件正在连续计时，尚未解除 active。 */
    uint32_t assert_started_ms; /* 当前连续触发窗口的起点。 */
    uint32_t recovery_started_ms; /* 当前连续恢复资格窗口的起点。 */
} BMS_StateCondition_t;

typedef struct
{
    BMS_StateCondition_t sw_ov;              /* 软件单体过压计时器。 */
    BMS_StateCondition_t sw_uv;              /* 软件单体欠压计时器。 */
    BMS_StateCondition_t sw_oc_charge;       /* 充电方向软件过流计时器。 */
    BMS_StateCondition_t sw_oc_discharge;    /* 放电方向软件过流计时器。 */
    BMS_StateCondition_t charge_temp_low;    /* 充电低温保护计时器。 */
    BMS_StateCondition_t charge_temp_high;   /* 充电高温保护计时器。 */
    BMS_StateCondition_t discharge_temp_low; /* 放电低温保护计时器。 */
    BMS_StateCondition_t discharge_temp_high;/* 放电高温保护计时器。 */
    BMS_StateCondition_t state_transition;   /* CHARGE/DSG/STANDBY 分类资格窗口。 */
    BMS_State_t state;                       /* 当前已提交的运行分类。 */
    BMS_State_t transition_candidate;        /* 正在计时、尚未提交的候选分类。 */
    uint32_t init_started_ms;                 /* INIT 超时判断的起点。 */
    uint32_t last_fresh_sequence;             /* 上一帧计入恢复资格的完整序号。 */
    uint8_t fresh_frame_count;                /* stale 后连续不同 fresh 帧数量。 */
    bool data_stale_active;                   /* 任一安全数据过期后锁存，连续 fresh 才解除。 */
    bool initialized;                         /* engine 已绑定策略并建立安全初值。 */
} BMS_StateEngine_t;

typedef struct
{
    BMS_FaultSummary_t faults;                /* State owner 的软件/数据/健康 fault。 */
    BMS_InhibitReasonBitmap_t inhibit_chg_reasons; /* 禁止充电方向的原因集合。 */
    BMS_InhibitReasonBitmap_t inhibit_dsg_reasons; /* 禁止放电方向的原因集合。 */
    uint32_t evaluated_sample_sequence; /* 决策实际读取的完整 measurement */
    uint32_t evaluated_afe_generation;  /* 决策绑定的 AFE 生命周期 */
    uint32_t publication_revision;      /* 每次权威发布递增，供 FET transaction 确认 */
    BMS_State_t state;                        /* 运行分类，不是 SYS_CTRL2 命令。 */
    BQ76940_FetRequest_t operational_intent;  /* State 希望的方向，仍需 FET Manager 仲裁。 */
    bool technical_ready;                     /* Recovery 是否完成全部技术恢复阶段。 */
} BMS_StateSafetySnapshot_t;

/* 启动上下文调用；建立 DATA_STALE+BOTH inhibit 的 fail-closed 初始快照。 */
void BMS_State_Init(const BMS_Policy_t *policy, uint32_t now_ms);

/*
 * StateTask 与 production-C test 共用的纯 engine step。软件 OV/UV/OC/temperature
 * 先经 assert debounce；恢复必须跨回 hysteresis 门限并持续 recovery qualify。
 * 任一必需数据失效或 stale 都设置 DATA_STALE 并双向 inhibit。
 */
bool BMS_State_Evaluate(BMS_StateEngine_t *engine,
                        const BMS_Policy_t *policy,
                        const BMS_DataSnapshot_t *measurement,
                        uint32_t now_ms,
                        bool technical_ready,
                        bool rtos_health_fault,
                        BMS_StateSafetySnapshot_t *decision);

/*
 * compare-and-publish：只有当前 BMS_Data identity 仍等于 evaluated identity 才
 * 发布，防止 Evaluate 与 Publish 之间新采样到达后把旧决策覆盖到新数据上。
 */
bool BMS_State_PublishIfCurrent(BMS_StateSafetySnapshot_t *decision);

/*
 * StateTask 的正式单步入口。读取一次完整 measurement，计算 decision，并仅在
 * measurement identity 未变化时发布。返回 false 表示输入/策略无效、快照读取
 * 失败或 compare-and-publish 发现竞态；失败不会覆盖上一份权威快照。
 */
bool BMS_State_RunOnce(uint32_t now_ms,
                       bool technical_ready,
                       bool rtos_health_fault,
                       BMS_StateSafetySnapshot_t *published);

/* 任务上下文可并发读取；调度器临界区保证 struct 不会被撕裂。调用者只读。 */
BMS_StateSafetySnapshot_t BMS_State_GetSafetySnapshot(void);

#if defined(TEST_PHASE9_IMAGE)
typedef void (*BMS_StatePrePublishHook_t)(void);
/* 测试镜像设置发布前竞态注入点。 */
void BMS_State_TestSetPrePublishHook(BMS_StatePrePublishHook_t hook);
#endif

#endif /* BMS_STATE_H：头文件防重复包含 */
