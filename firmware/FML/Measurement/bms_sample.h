#ifndef BMS_SAMPLE_H
#define BMS_SAMPLE_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_ntc.h"
#include "bms_types.h"
#include "bq76940.h"

/*
 * production-C wrap test 与正式代码共用的 configuration revision 推进规则。
 * uint32_t 自然回绕是设计的一部分：UINT32_MAX→0 仍与事务捕获值不同。
 * 完整 2^32 次配置变更后的 alias 不由相等比较解决，而由生命周期/watchdog
 * 约束保证一次采样事务不会跨越如此多次配置更新。
 */
#define BMS_SAMPLE_CONFIGURATION_REVISION_NEXT(revision_) \
    ((uint32_t)((uint32_t)(revision_) + 1UL))

/*
 * 完整 measurement 的唯一 owner。RunOnce 先在局部变量中依次完成 13-cell/BAT
 * mandatory core，全部成功后才经 bms_data 原子发布并推进 sample_sequence。
 * current 只能取自 ProtectTask latest-CC mailbox，不能由 SampleTask 再读一次
 * CC 寄存器；TS1 每八个周期采样一次，拥有独立 timestamp 与 validity。
 */
typedef struct
{
    uint32_t success_count;                 /* 完整 core frame 被原子发布的次数。 */
    uint32_t failure_count;                 /* 任一阶段失败、整帧未发布的总次数。 */
    uint32_t frame_reject_count;            /* staging 自相矛盾或 Data 层拒绝的次数。 */
    uint32_t i2c_timeout_count;             /* 未在有界时间取得共享 I2C ownership。 */
    uint32_t transport_failure_count;       /* BQ transaction/CRC/ACK 等传输失败。 */
    uint32_t calibration_invalid_count;     /* calibration 数值或 provenance 不可用。 */
    uint32_t configuration_not_ready_count; /* device/config/generation 尚未形成闭环。 */
    uint32_t cell_group_failure_count;
    uint32_t pack_group_failure_count;
    uint32_t current_group_failure_count;
    uint32_t temperature_group_failure_count;
    uint32_t temperature_conversion_unavailable_count;
    uint32_t ntc_curve_unavailable_count;
    uint32_t data_publish_failure_count;
    uint32_t cc_mailbox_unavailable_count;
    /* stale 表示至少一个 valid 组超过 freshness 门限；invalid 不等于 stale。 */
    uint32_t stale_sample_count;
    uint32_t stale_transition_count;
    uint32_t stale_check_failure_count;
    /* 首次 AFE 访问前，XREADY/calibration generation guard 已拒绝本轮。 */
    uint32_t xready_precheck_reject_count;
    /* staging 完成但发布前 generation 改变，本地测量必须整体作废。 */
    uint32_t xready_postcheck_reject_count;
    uint32_t consecutive_failure_count;     /* 当前连续未发布完整帧的周期数。 */
    uint32_t max_consecutive_failure_count; /* 运行以来最大连续失败长度。 */
} BMS_SampleDiagnostics_t;

typedef struct
{
    uint32_t xready_generation;       /* calibration 所属的 AFE 生命周期。 */
    uint32_t recovery_revision;       /* 产生本次 handoff 的 Recovery transaction。 */
    bool post_clear_verified;         /* clear 后配置/状态是否已经回读确认。 */
    BQ76940_Calibration_t calibration;/* 与上述 provenance 一起原子安装的增益/偏移。 */
} BMS_SampleCalibrationEvidence_t;

/*
 * 启动初始化保持 fail-closed，直到 device 与有效 AFE calibration 都已安装。
 * calibration 绑定当前 inactive XREADY generation；每次观察到 XREADY 转移后
 * 都必须由 recovery coordinator 以新 provenance 重新交接。
 */
void BMS_Sample_Init(void);

/*
 * 下列配置 API 只允许启动/任务上下文调用，绝不是 ISR API。调度器启动前属于
 * 单线程直接赋值；启动后用可嵌套 runtime critical region 保护多字段更新，而且
 * 不会提前退出调用者已经建立的外层 critical region。
 *
 * device 与 NTC table 的生命周期必须覆盖全部 sampling call，已安装 table 在
 * clear/replace 前保持 immutable。SetNtcTable(NULL, 0) 明确清空曲线，非法的
 * nonempty table 以 transaction 方式整体拒绝。XREADY active 时 SetCalibration
 * 会拒绝并清除 binding；一旦发生运行期 XREADY invalidation，只有带 generation
 * 与 recovery revision provenance 的 SetRecoveryCalibration 能恢复采样。
 */
void BMS_Sample_SetDevice(BQ76940_t *device);
bool BMS_Sample_SetCalibration(
    const BQ76940_Calibration_t *calibration);
bool BMS_Sample_SetRecoveryCalibration(
    const BMS_SampleCalibrationEvidence_t *evidence,
    uint32_t current_recovery_revision,
    bool handoff_permitted);
void BMS_Sample_InvalidateCalibrationForXready(
    uint32_t xready_generation);
bool BMS_Sample_SetNtcTable(const BMS_NtcPoint_t *points,
                             uint16_t point_count);

#if defined(TEST_PHASE8_SAMPLE_IMAGE)
/* 仅 test image 暴露，用于验证 revision 立即自然回绕；production image 无此符号。 */
void BMS_Sample_TestSeedConfigurationRevision(uint32_t revision);
#endif

/*
 * 调用者为 SampleTask（production-C test 也可直接调用），函数内部按短 transaction
 * 获取/释放共享 I2C mutex，调用者不得预持该 mutex。一个有界 250 ms cycle body。
 * 只有 bms_data 接受完整 core frame 才返回 true；
 * 发布时不持有 I2C mutex。device/XREADY epoch 改变后的首帧若没有同代 CC，
 * 会同时使上一代 current 失效，防止电压来自新 AFE 而电流仍来自旧 AFE。
 * false 表示本周期没有发布新完整帧；上一快照保持可诊断，但 freshness 会继续老化。
 */
bool BMS_Sample_RunOnce(BMS_TimestampMs_t now_ms);

/* 同 generation 的任务上下文诊断快照；ISR 不得调用。 */
BMS_SampleDiagnostics_t BMS_Sample_GetDiagnostics(void);

#endif /* BMS_SAMPLE_H：头文件防重复包含 */
