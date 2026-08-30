#ifndef BMS_SOC_H
#define BMS_SOC_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_data.h"
#include "bms_policy.h"
#include "bms_protect.h"

#define BMS_SOC_MAX_CC_SAMPLES_PER_RUN           (8U)

typedef struct
{
    int64_t remaining_mams;            /* mA·ms：整数库仑积分的内部高分辨率容量 */
    uint32_t last_sample_ms;            /* 上一条已接受 CC sample 时间 */
    uint32_t afe_generation;            /* 积分链绑定的 AFE 生命周期 */
    uint32_t integrated_sample_count;   /* 成功连续积分的 sample 数 */
    uint32_t queue_gap_count;           /* newest-wins 覆盖造成的不连续次数 */
    uint32_t generation_change_count;   /* AFE epoch 切换次数 */
    uint32_t full_correction_count;    /* 满端连续资格完成次数。 */
    uint32_t empty_correction_count;   /* 空端连续资格完成次数。 */
    uint32_t full_started_ms;          /* 当前满端资格窗口起点。 */
    uint32_t empty_started_ms;         /* 当前空端资格窗口起点。 */
    bool initialized;                  /* 已由 restore/OCV/默认值建立容量。 */
    bool valid;                        /* 当前估计可作为有效诊断值。 */
    bool have_sample_time;             /* 已有同 generation 的积分时间基线。 */
    bool queue_gap_latched;            /* CC 丢样导致精度降级，端点校正后清除。 */
    bool full_tracking;                /* 满端条件正在连续计时。 */
    bool empty_tracking;               /* 空端条件正在连续计时。 */
} BMS_SocEngine_t;

typedef struct
{
    BMS_CapacityMah_t remaining_capacity_mah; /* clamp 后剩余容量。 */
    BMS_SocPermille_t soc_permille;           /* 0..1000 对应 0%..100%。 */
    uint32_t integrated_sample_count;
    uint32_t queue_gap_count;
    uint32_t generation_change_count;
    uint32_t full_correction_count;
    uint32_t empty_correction_count;
    bool valid;
    bool queue_gap_latched;
} BMS_SocSnapshot_t;

/*
 * SOCTask 与 production-C tests 共用的纯整数 SOC engine。初值来自合法 Flash
 * restore，否则由 fresh cell OCV 建立；CC sample 使用 ΔQ=I×Δt 的 mA·ms 整数
 * 积分，并按 charge/discharge efficiency permille 修正，始终 clamp 在
 * [0, capacity]。queue gap 会锁存精度降级诊断。AFE generation 改变时只重新
 * 建立时间基线，绝不把旧 epoch current 与新 epoch 时间间隔连续积分。queue gap
 * 表示时间区间内可能缺失电流证据，估计会标记无效，直到可信端点校正。
 */
bool BMS_Soc_EngineInit(BMS_SocEngine_t *engine,
                        const BMS_SocPolicy_t *policy,
                        const BMS_DataSnapshot_t *measurement,
                        uint32_t now_ms);
bool BMS_Soc_IntegrateCurrent(BMS_SocEngine_t *engine,
                              const BMS_SocPolicy_t *policy,
                              int32_t current_ma,
                              uint32_t sample_ms,
                              uint32_t afe_generation);
void BMS_Soc_ObserveCorrection(BMS_SocEngine_t *engine,
                               const BMS_SocPolicy_t *policy,
                               const BMS_DataSnapshot_t *measurement,
                               uint32_t now_ms);
void BMS_Soc_MarkQueueGap(BMS_SocEngine_t *engine);
BMS_SocSnapshot_t BMS_Soc_GetEngineSnapshot(
    const BMS_SocEngine_t *engine,
    const BMS_SocPolicy_t *policy);

void BMS_Soc_Init(const BMS_Policy_t *policy);
bool BMS_Soc_Restore(uint16_t soc_permille,
                     uint32_t remaining_capacity_mah);
void BMS_Soc_RunOnce(uint32_t now_ms,
                     const BMS_CcSample_t *samples,
                     uint8_t sample_count,
                     bool queue_gap);
BMS_SocSnapshot_t BMS_Soc_GetSnapshot(void);

#endif /* BMS_SOC_H：头文件防重复包含 */
