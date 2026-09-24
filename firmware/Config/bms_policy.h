#ifndef BMS_POLICY_H
#define BMS_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_afe_startup.h"
#include "bms_ntc.h"

#define BMS_POLICY_PROFILE_ID_SIM_V1            "SIM_POLICY_V1"
#define BMS_POLICY_NTC_MODEL_ID_SIM_V1          "SIM_NTC_10K_B3950"
#define BMS_POLICY_NTC_POINT_COUNT              (15U)
#define BMS_POLICY_REQUIRED_TASK_COUNT          (7U)
#define BMS_POLICY_CAN_TX_ID_COUNT              (6U)

/*
 * 所有 threshold、debounce、hysteresis、task liveness、SOC、balance、CAN 与
 * Flash 参数集中为 immutable profile。业务模块只读取 const policy，不在运行期
 * 原地修改，从而使一次 decision 所依赖的配置稳定且可被 verifier 复现。
 */

typedef struct
{
    int32_t trigger;              /* 故障触发阈值；单位由对应测量策略确定。 */
    int32_t recovery;             /* 故障释放阈值；与 trigger 形成滞回。 */
    uint32_t debounce_ms;        /* 触发条件连续成立的最短时间。 */
    uint32_t recovery_qualify_ms; /* 释放条件连续成立的最短时间。 */
} BMS_ThresholdPolicy_t;

typedef struct
{
    int16_t low_trigger_decic;    /* 低温故障触发点，单位 0.1 °C。 */
    int16_t low_recovery_decic;   /* 低温故障释放点，单位 0.1 °C。 */
    int16_t high_trigger_decic;   /* 高温故障触发点，单位 0.1 °C。 */
    int16_t high_recovery_decic;  /* 高温故障释放点，单位 0.1 °C。 */
    uint32_t debounce_ms;        /* 温度故障触发的连续资格时间。 */
    uint32_t recovery_qualify_ms; /* 温度故障释放的连续资格时间。 */
} BMS_TemperaturePolicy_t;

typedef struct
{
    uint32_t startup_timeout_ms;          /* 启动状态等待有效数据的最长时间。 */
    int32_t standby_enter_abs_current_ma; /* 进入待机所允许的电流绝对值。 */
    uint32_t standby_enter_qualify_ms;    /* 待机条件连续成立的时间。 */
    int32_t charge_enter_current_ma;      /* 进入充电状态的正向电流门限。 */
    uint32_t charge_enter_qualify_ms;     /* 充电进入条件的连续资格时间。 */
    int32_t charge_exit_current_ma;       /* 退出充电状态的电流滞回门限。 */
    uint32_t charge_exit_qualify_ms;      /* 充电退出条件的连续资格时间。 */
    int32_t discharge_enter_current_ma;   /* 进入放电状态的负向电流门限。 */
    uint32_t discharge_enter_qualify_ms;  /* 放电进入条件的连续资格时间。 */
    int32_t discharge_exit_current_ma;    /* 退出放电状态的电流滞回门限。 */
    uint32_t discharge_exit_qualify_ms;   /* 放电退出条件的连续资格时间。 */
    uint32_t period_ms;                   /* StateTask 最大有界等待周期。 */
} BMS_StatePolicy_t;

typedef struct
{
    uint32_t voltage_fresh_ms;     /* 电芯/包电压允许的最大年龄。 */
    uint32_t current_fresh_ms;     /* CC 电流允许的最大年龄。 */
    uint32_t temperature_fresh_ms; /* 温度允许的最大年龄。 */
    uint8_t recovery_fresh_frames; /* 解除 DATA_STALE 前要求的连续新帧数。 */
} BMS_FreshnessPolicy_t;

typedef struct
{
    uint8_t consecutive_failures_to_active;  /* 激活通信故障的连续失败次数。 */
    uint32_t no_success_timeout_ms;          /* 无成功访问时的故障超时。 */
    uint32_t continuous_latch_ms;            /* 持续故障升级为锁存的时间。 */
    uint8_t consecutive_successes_to_recover; /* 释放 active 所需连续成功次数。 */
} BMS_AfeCommPolicy_t;

typedef struct
{
    uint8_t event_count_to_latch; /* 一个窗口内触发 OCD 锁存的事件次数。 */
    uint32_t event_window_ms;     /* OCD 升级计数窗口长度。 */
} BMS_OcdEscalationPolicy_t;

typedef enum
{
    BMS_HEALTH_TASK_PROTECT = 0, /* ALERT 与 HW/AFE 故障 owner 的 heartbeat。 */
    BMS_HEALTH_TASK_SAMPLE,      /* 完整测量帧发布任务的 heartbeat。 */
    BMS_HEALTH_TASK_STATE,       /* 状态与 IWDG 协调任务的 heartbeat。 */
    BMS_HEALTH_TASK_SOC,         /* CC 积分任务的 heartbeat。 */
    BMS_HEALTH_TASK_BALANCE,     /* CELLBAL 执行任务的 heartbeat。 */
    BMS_HEALTH_TASK_CAN_TX,      /* 诊断发送任务的 heartbeat。 */
    BMS_HEALTH_TASK_CAN_RX,      /* 服务接收任务的 heartbeat。 */
    BMS_HEALTH_TASK_COUNT        /* heartbeat generation 数组边界。 */
} BMS_HealthTaskId_t;

typedef struct
{
    BMS_HealthTaskId_t task_id; /* 此条健康约束对应的唯一任务。 */
    uint32_t max_liveness_ms;   /* 该任务 heartbeat 最长可停滞时间。 */
} BMS_TaskHealthPolicy_t;

typedef struct
{
    const BMS_TaskHealthPolicy_t *roster; /* 必需任务及各自 liveness 窗口。 */
    uint8_t roster_count;                 /* roster 中的有效任务数。 */
    uint32_t iwdg_nominal_timeout_ms;     /* IWDG 标称超时配置。 */
    uint32_t startup_grace_ms;            /* 任务启动后首次健康判断的宽限期。 */
} BMS_HealthPolicy_t;

typedef struct
{
    uint32_t capacity_mah;                 /* 电池标称容量，单位 mAh。 */
    uint16_t initial_soc_permille;         /* 无可信恢复值时使用的 SOC 初值。 */
    uint16_t charge_efficiency_permille;   /* 充电积分效率，千分比。 */
    uint16_t discharge_efficiency_permille;/* 放电积分效率，千分比。 */
    uint32_t period_ms;                    /* SOC task 运行周期。 */
    uint16_t full_cell_mv;                 /* 满端资格的最低单体电压门限。 */
    int32_t full_current_min_ma;           /* 满端资格电流区间下界。 */
    int32_t full_current_max_ma;           /* 满端资格电流区间上界。 */
    uint32_t full_qualify_ms;              /* 满端条件连续成立时间。 */
    uint16_t empty_cell_mv;                /* 空端资格的最低单体电压门限。 */
    int32_t empty_discharge_abs_current_max_ma; /* 空端允许的放电电流绝对值上限。 */
    uint32_t empty_qualify_ms;             /* 空端条件连续成立时间。 */
} BMS_SocPolicy_t;

typedef struct
{
    uint32_t period_ms;                    /* BalanceTask 运行周期。 */
    uint16_t minimum_cell_mv;              /* 开始均衡前的最低单体电压。 */
    uint16_t start_delta_mv;               /* 未激活电芯开始均衡的压差。 */
    uint16_t stop_delta_mv;                /* 已激活电芯停止均衡的滞回压差。 */
    uint16_t low_voltage_stop_mv;          /* 低于此电压时强制停止均衡。 */
    uint8_t max_parallel_cells;            /* 同时允许激活的最大电芯数。 */
    bool adjacent_cells_permitted;         /* 是否允许相邻电芯同时均衡。 */
    int16_t minimum_temperature_decic;     /* 均衡允许的温度下界。 */
    int16_t maximum_temperature_decic;     /* 均衡允许的温度上界。 */
    int32_t max_abs_current_ma;            /* 均衡允许的电流绝对值上限。 */
    uint32_t rotation_ms;                  /* 同电压候选间轮换的窗口长度。 */
} BMS_BalancePolicy_t;

typedef struct
{
    uint32_t bitrate;                    /* bxCAN 总线速率，单位 bit/s。 */
    bool standard_11_bit_ids;            /* 当前协议只接受 11-bit 标准帧。 */
    const uint16_t *tx_ids;              /* 周期诊断帧的只读 ID 表。 */
    uint8_t tx_id_count;                 /* 诊断帧 ID 表有效元素数。 */
    uint16_t service_rx_id;              /* 受限服务命令接收 ID。 */
    uint32_t rx_command_timeout_ms;      /* 服务帧从接收到处理的最长时效。 */
    bool fault_has_direct_fet_effect;    /* 必须为 false；诊断 fault 不直接控制 FET。 */
} BMS_CanPolicy_t;

typedef struct
{
    uint32_t slot_a_address;            /* A 页起始 Flash 地址。 */
    uint32_t slot_b_address;            /* B 页起始 Flash 地址。 */
    uint16_t page_size_bytes;           /* 每个持久化页的字节容量。 */
    uint32_t minimum_save_interval_ms;  /* 相邻两次写入的最短时间。 */
    uint16_t soc_change_trigger_permille; /* 触发写入的 SOC 最小变化量。 */
} BMS_FlashPolicy_t;

typedef struct
{
    const char *profile_id;      /* 当前不可变策略集合的身份字符串。 */
    uint8_t cell_count;          /* 串联电芯数量，必须与目标 AFE 拓扑一致。 */
    uint32_t capacity_mah;       /* 电池标称容量，供 SOC 等模块共用。 */
    uint32_t rsense_uohm;        /* 电流采样电阻，单位 µΩ。 */
    int8_t current_polarity;     /* CC 码到充电正电流的符号映射。 */

    BMS_AfeStartupConfig_t afe_startup; /* 启动期寄存器与校准计划。 */
    const char *ntc_model_id;           /* NTC 表的可追溯模型标识。 */
    const BMS_NtcPoint_t *ntc_points;   /* 只读电阻/温度插值节点。 */
    uint16_t ntc_point_count;           /* NTC 表有效节点数。 */

    BMS_ThresholdPolicy_t sw_ov;           /* 软件单体过压阈值与资格时间。 */
    BMS_ThresholdPolicy_t sw_uv;           /* 软件单体欠压阈值与资格时间。 */
    BMS_ThresholdPolicy_t sw_oc_charge;    /* 软件充电过流阈值与资格时间。 */
    BMS_ThresholdPolicy_t sw_oc_discharge; /* 软件放电过流阈值与资格时间。 */
    BMS_TemperaturePolicy_t charge_temperature; /* 充电方向温度保护窗口。 */
    BMS_TemperaturePolicy_t discharge_temperature; /* 放电方向温度保护窗口。 */
    BMS_FreshnessPolicy_t freshness;        /* 安全输入时效与恢复新帧门槛。 */
    BMS_AfeCommPolicy_t afe_comm;           /* AFE 通信故障触发和释放门槛。 */
    BMS_OcdEscalationPolicy_t ocd_escalation; /* OCD 重复事件升级规则。 */
    uint32_t service_reset_qualify_ms;      /* 服务重置前的连续安全资格时间。 */

    BMS_StatePolicy_t state;       /* 运行状态分类与时序策略。 */
    BMS_HealthPolicy_t health;     /* 任务健康和 IWDG 资格策略。 */
    BMS_SocPolicy_t soc;           /* 容量估计、效率和端点校正策略。 */
    BMS_BalancePolicy_t balance;   /* 均衡资格、滞回和轮换策略。 */
    BMS_CanPolicy_t can;           /* 诊断 CAN 标识、时效及权限约束。 */
    BMS_FlashPolicy_t flash;       /* A/B 页地址及节流存储策略。 */
} BMS_Policy_t;

/* 返回的 profile 与其引用 table 在整个运行期保持 immutable。 */
const BMS_Policy_t *BMS_Policy_Get(void);

/* false 表示 fail-closed configuration error，启动层不得继续创建任务。 */
bool BMS_Policy_Validate(const BMS_Policy_t *policy);

#endif /* BMS_POLICY_H：include guard */
