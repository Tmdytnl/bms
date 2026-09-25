#ifndef FML_AFE_STARTUP_H
#define FML_AFE_STARTUP_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_bq76940.h"
#include "bsp_bq76940_regs.h"

#define BMS_AFE_STARTUP_MAX_PROBE_ATTEMPTS       (3U)
#define BMS_AFE_STARTUP_REGISTER_COUNT            (12U)
#define BMS_AFE_STARTUP_SYS_CTRL1_REQUIRED        ((uint8_t)0x18U)
#define BMS_AFE_STARTUP_SYS_CTRL2_FET_OFF         ((uint8_t)0x00U)
#define BMS_AFE_STARTUP_SYS_CTRL2_CC_FET_OFF      \
    BQ76940_SYS_CTRL2_CC_EN_MASK

/*
 * startup 对 SYS_STAT 的 ownership 刻意收窄：CC_READY 留给 ProtectTask；保护类
 * event 绝不在此 W1C，而是在 FET-off 与三组 CELLBAL-off 都回读确认后终止启动。
 * XREADY 是启动期唯一允许清除的位，而且必须先完成一遍配置与 settle；W1C 后
 * 旧 register/calibration evidence 全部失效，必须重新执行完整配置，不能复用。
 */
#define BMS_AFE_STARTUP_STAT_CC_READY              ((uint8_t)0x80U)
#define BMS_AFE_STARTUP_STAT_DEVICE_XREADY         ((uint8_t)0x20U)
#define BMS_AFE_STARTUP_STAT_BLOCKING_MASK         ((uint8_t)0x1FU)

/*
 * callback 只产生一次 board-specific PA8→TS1 rising edge，必须是有界 GPIO
 * 动作：无 delay、I2C transaction、retry loop 或 RTOS wait。
 */
typedef bool (*BMS_AfeStartupWakeFn_t)(void *context);

typedef struct
{
    bool ov_uv_trip_present;          /* 门限来自显式项目配置，而非零值默认 */
    uint16_t ov_trip_mv;              /* 过压硬件比较器目标电压 */
    uint16_t uv_trip_mv;              /* 欠压硬件比较器目标电压 */

    bool protect1_present;            /* SCD 配置组完整性证明 */
    bool protect1_rsns;               /* 采样电阻量程选择位 */
    uint8_t protect1_scd_delay_code;  /* 短路延迟编码，先做范围校验 */
    uint8_t protect1_scd_threshold_code; /* 短路阈值编码 */

    bool protect2_present;            /* OCD 配置组完整性证明 */
    uint8_t protect2_ocd_delay_code;  /* 放电过流延迟编码 */
    uint8_t protect2_ocd_threshold_code; /* 放电过流阈值编码 */

    /* 即使 exact code 为 0 也必须显式 present，防止缺失 PROTECT3 被零初始化伪装。 */
    bool protect3_present;
    uint8_t protect3_uv_delay_code; /* 欠压延时的 PROTECT3 编码。 */
    uint8_t protect3_ov_delay_code; /* 过压延时的 PROTECT3 编码。 */
} BMS_AfeStartupConfig_t;

typedef enum
{
    BMS_AFE_STARTUP_STATE_UNINITIALIZED = 0, /* 尚无可执行计划 */
    BMS_AFE_STARTUP_STATE_WAKE,              /* 产生一次 PA8→TS1 唤醒边沿 */
    BMS_AFE_STARTUP_STATE_WAIT_WAKE_SETTLE,  /* 无锁等待模拟前端稳定 */
    BMS_AFE_STARTUP_STATE_PROBE,             /* 读 SYS_STAT 证明通信可用 */
    BMS_AFE_STARTUP_STATE_WRITE_REGISTER,    /* 写当前计划项 */
    BMS_AFE_STARTUP_STATE_VERIFY_REGISTER,   /* 立即回读同一计划项 */
    BMS_AFE_STARTUP_STATE_READ_ADCGAIN1,     /* 获取校准证据第 1 段 */
    BMS_AFE_STARTUP_STATE_READ_ADCOFFSET,    /* 获取带符号 offset */
    BMS_AFE_STARTUP_STATE_READ_ADCGAIN2,     /* 获取校准证据第 2 段 */
    BMS_AFE_STARTUP_STATE_PREPARE_PROTECTION, /* 校准后才编码 OV/UV */
    BMS_AFE_STARTUP_STATE_WAIT_INITIAL_DATA, /* 等首轮测量/状态稳定 */
    BMS_AFE_STARTUP_STATE_READ_FINAL_STATUS, /* 当前 epoch 最终安全判定 */
    BMS_AFE_STARTUP_STATE_CLEAR_XREADY,      /* 唯一获准的启动期 W1C */
    BMS_AFE_STARTUP_STATE_SAFE_OFF_WRITE,    /* 一次紧急 FET-off 纠正 */
    BMS_AFE_STARTUP_STATE_SAFE_OFF_VERIFY,   /* 纠正后必须实读确认 */
    BMS_AFE_STARTUP_STATE_COMPLETE,          /* 配置与安全证据均有效 */
    BMS_AFE_STARTUP_STATE_FAILED             /* 终态；调用者保持系统禁能 */
} BMS_AfeStartupState_t;

typedef enum
{
    BMS_AFE_STARTUP_FAILURE_NONE = 0, /* 尚未失败 */
    BMS_AFE_STARTUP_FAILURE_INVALID_ARGUMENT, /* 指针或回调缺失 */
    BMS_AFE_STARTUP_FAILURE_INVALID_CONFIG, /* 门限/编码不完整或越界 */
    BMS_AFE_STARTUP_FAILURE_DEVICE_NOT_READY, /* BQ 对象尚未初始化 */
    BMS_AFE_STARTUP_FAILURE_WAKE, /* 有界次数内未能发出有效唤醒 */
    BMS_AFE_STARTUP_FAILURE_PROBE, /* 唤醒后仍无法读取器件 */
    BMS_AFE_STARTUP_FAILURE_TRANSPORT, /* 确定失败的 I2C 事务 */
    BMS_AFE_STARTUP_FAILURE_WRITE_FINALIZATION_AMBIGUOUS, /* STOP 结果未知 */
    BMS_AFE_STARTUP_FAILURE_READBACK_MISMATCH, /* 寄存器未保持期望值 */
    BMS_AFE_STARTUP_FAILURE_CALIBRATION, /* ADC 校准字段不可接受 */
    BMS_AFE_STARTUP_FAILURE_UNSAFE_STATUS, /* 保护事件或重复 XREADY */
    BMS_AFE_STARTUP_FAILURE_SAFE_OFF_UNCONFIRMED /* 纠正后仍不能证明 FET 低 */
} BMS_AfeStartupFailure_t;

typedef enum
{
    BMS_AFE_STARTUP_RESULT_PENDING = 0, /* 本步完成，稍后带新时间再推进 */
    BMS_AFE_STARTUP_RESULT_COMPLETE,    /* 可以读取校准并继续系统启动 */
    BMS_AFE_STARTUP_RESULT_FAILED       /* 终态；不得继续启用功率输出 */
} BMS_AfeStartupResult_t;

/*
 * state 由 caller 持有，不用 RTOS object、dynamic allocation 或 module global，
 * 因而可在 scheduler 前运行，也可被 production-C test 独立实例化。
 */
typedef struct
{
    BQ76940_t *device;                 /* 所有事务使用的已初始化设备 */
    BMS_AfeStartupWakeFn_t wake;       /* 板级唤醒边沿唯一入口 */
    void *wake_context;                /* 唤醒适配器私有上下文 */
    BMS_AfeStartupConfig_t config;     /* Init 时复制，后续不依赖调用者内存 */
    BQ76940_Calibration_t calibration; /* 当前配置 epoch 的校准证据 */
    BMS_AfeStartupState_t state;       /* 下一次 Step 的唯一分派依据 */
    BMS_AfeStartupFailure_t failure;   /* FAILED 的稳定原因 */
    BQ76940_Status_t last_transport_status; /* 最近一次底层事务证据 */
    uint32_t wait_started_ms;          /* wrap-safe elapsed-time 起点 */
    uint8_t attempt_count;             /* 唤醒/探测共享的有界尝试次数 */
    uint8_t register_index;            /* 当前 write-readback 计划位置 */
    uint8_t register_count;            /* 当前 epoch 需要执行的计划长度 */
    uint8_t register_addresses[BMS_AFE_STARTUP_REGISTER_COUNT]; /* 按顺序执行的寄存器地址计划。 */
    uint8_t register_values[BMS_AFE_STARTUP_REGISTER_COUNT]; /* 待逐项写入并回读的配置计划。 */
    uint8_t adc_gain1;  /* 校准寄存器 ADCGAIN1 的原始字节。 */
    uint8_t adc_offset; /* 校准寄存器 ADCOFFSET 的原始字节。 */
    uint8_t adc_gain2;  /* 校准寄存器 ADCGAIN2 的原始字节。 */
    uint8_t initial_sys_stat;          /* probe 时的历史观察，仅用于阻断 */
    uint8_t final_sys_stat;            /* settle 后的当前 W1C 授权观察 */
    uint8_t unsafe_sys_stat;           /* 供诊断保留的阻断位证据 */
    uint8_t failed_register_address; /* 首次写入或回读失败的寄存器地址。 */
    uint8_t failed_readback_value;   /* 首个配置回读不符时观察到的字节。 */
    uint8_t safe_off_readback_value; /* 安全关断回读的 SYS_CTRL2 完整字节。 */
    bool fet_off_confirmed;            /* 最新 SYS_CTRL2 实读证明 CHG/DSG=0 */
    bool safe_outputs_confirmed;       /* FET 与三组均衡输出均已确认安全 */
    bool abort_after_safe_outputs;     /* 先做安全关断，再发布 unsafe 失败 */
    bool safe_off_recovery_attempted;  /* 紧急纠正最多一次，禁止循环重放 */
    bool xready_clear_required;        /* 仅由 final_sys_stat 当前观察授权 */
    bool xready_clear_attempted;       /* W1C 已发出，结果未知也禁止重放 */
    bool xready_clear_completed;       /* 已清过一次，重复 XREADY 视为异常 */
} BMS_AfeStartup_t;

/*
 * 只验证并 staging startup plan，不执行 GPIO/I2C。每组 protection 都要求
 * explicit present flag，缺失 threshold 或 PROTECT3 delay 时整体拒绝，不猜默认值。
 */
bool FML_AfeStartup_Init(BMS_AfeStartup_t *startup,
                         BQ76940_t *device,
                         const BMS_AfeStartupConfig_t *config,
                         BMS_AfeStartupWakeFn_t wake,
                         void *wake_context);

/*
 * 每次只推进一个有界 state，最多一次 wake callback 或一次 BQ transaction。
 * 10 ms WAKE settle 与 800 ms initial-data settle 都按 elapsed time 等待，绝不
 * 持有 I2C transaction。只有当前 final-status read 看到 XREADY high 才允许一次
 * W1C；早期历史观察不能授权 blind clear。clear 成功后所有旧配置证据失效，
 * 必须重跑 safe-register/calibration/protection plan 与 settle。第二次 XREADY、
 * 或 ambiguous W1C finalization 都 fail closed，且绝不 replay。
 *
 * SYS_STAT 不提供 event identity；final-read authorization + no-replay 用于缩小
 * “读后新事件”竞态窗口，并确保软件不会自行选择一个无法证明的事件身份。
 */
BMS_AfeStartupResult_t FML_AfeStartup_Step(BMS_AfeStartup_t *startup,
                                           uint32_t now_ms);

/* 复制启动状态机当前阶段及已确认的证据。 */
BMS_AfeStartupState_t FML_AfeStartup_GetState(
    const BMS_AfeStartup_t *startup);
/* 返回启动状态机保留的首个失败原因。 */
BMS_AfeStartupFailure_t FML_AfeStartup_GetFailure(
    const BMS_AfeStartup_t *startup);
/* 仅在校准已验证后复制增益与偏移供 Sample 绑定。 */
bool FML_AfeStartup_GetCalibration(
    const BMS_AfeStartup_t *startup,
    BQ76940_Calibration_t *calibration);
/* 只返回最新 readback 证据；“曾经写过 FET-off”不等于已确认。 */
bool FML_AfeStartup_IsFetOffConfirmed(
    const BMS_AfeStartup_t *startup);

#endif /* FML_AFE_STARTUP_H：头文件防重复包含 */
