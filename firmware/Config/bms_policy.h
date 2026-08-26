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
    int32_t trigger;
    int32_t recovery;
    uint32_t debounce_ms;
    uint32_t recovery_qualify_ms;
} BMS_ThresholdPolicy_t;

typedef struct
{
    int16_t low_trigger_decic;
    int16_t low_recovery_decic;
    int16_t high_trigger_decic;
    int16_t high_recovery_decic;
    uint32_t debounce_ms;
    uint32_t recovery_qualify_ms;
} BMS_TemperaturePolicy_t;

typedef struct
{
    uint32_t startup_timeout_ms;
    int32_t standby_enter_abs_current_ma;
    uint32_t standby_enter_qualify_ms;
    int32_t charge_enter_current_ma;
    uint32_t charge_enter_qualify_ms;
    int32_t charge_exit_current_ma;
    uint32_t charge_exit_qualify_ms;
    int32_t discharge_enter_current_ma;
    uint32_t discharge_enter_qualify_ms;
    int32_t discharge_exit_current_ma;
    uint32_t discharge_exit_qualify_ms;
    uint32_t period_ms;
} BMS_StatePolicy_t;

typedef struct
{
    uint32_t voltage_fresh_ms;
    uint32_t current_fresh_ms;
    uint32_t temperature_fresh_ms;
    uint8_t recovery_fresh_frames;
} BMS_FreshnessPolicy_t;

typedef struct
{
    uint8_t consecutive_failures_to_active;
    uint32_t no_success_timeout_ms;
    uint32_t continuous_latch_ms;
    uint8_t consecutive_successes_to_recover;
} BMS_AfeCommPolicy_t;

typedef struct
{
    uint8_t event_count_to_latch;
    uint32_t event_window_ms;
} BMS_OcdEscalationPolicy_t;

typedef enum
{
    BMS_HEALTH_TASK_PROTECT = 0,
    BMS_HEALTH_TASK_SAMPLE,
    BMS_HEALTH_TASK_STATE,
    BMS_HEALTH_TASK_SOC,
    BMS_HEALTH_TASK_BALANCE,
    BMS_HEALTH_TASK_CAN_TX,
    BMS_HEALTH_TASK_CAN_RX,
    BMS_HEALTH_TASK_COUNT
} BMS_HealthTaskId_t;

typedef struct
{
    BMS_HealthTaskId_t task_id;
    uint32_t max_liveness_ms;
} BMS_TaskHealthPolicy_t;

typedef struct
{
    const BMS_TaskHealthPolicy_t *roster;
    uint8_t roster_count;
    uint32_t iwdg_nominal_timeout_ms;
    uint32_t startup_grace_ms;
} BMS_HealthPolicy_t;

typedef struct
{
    uint32_t capacity_mah;
    uint16_t initial_soc_permille;
    uint16_t charge_efficiency_permille;
    uint16_t discharge_efficiency_permille;
    uint32_t period_ms;
    uint16_t full_cell_mv;
    int32_t full_current_min_ma;
    int32_t full_current_max_ma;
    uint32_t full_qualify_ms;
    uint16_t empty_cell_mv;
    int32_t empty_discharge_abs_current_max_ma;
    uint32_t empty_qualify_ms;
} BMS_SocPolicy_t;

typedef struct
{
    uint32_t period_ms;
    uint16_t minimum_cell_mv;
    uint16_t start_delta_mv;
    uint16_t stop_delta_mv;
    uint16_t low_voltage_stop_mv;
    uint8_t max_parallel_cells;
    bool adjacent_cells_permitted;
    int16_t minimum_temperature_decic;
    int16_t maximum_temperature_decic;
    int32_t max_abs_current_ma;
    uint32_t rotation_ms;
} BMS_BalancePolicy_t;

typedef struct
{
    uint32_t bitrate;
    bool standard_11_bit_ids;
    const uint16_t *tx_ids;
    uint8_t tx_id_count;
    uint16_t service_rx_id;
    uint32_t rx_command_timeout_ms;
    bool fault_has_direct_fet_effect;
} BMS_CanPolicy_t;

typedef struct
{
    uint32_t slot_a_address;
    uint32_t slot_b_address;
    uint16_t page_size_bytes;
    uint32_t minimum_save_interval_ms;
    uint16_t soc_change_trigger_permille;
} BMS_FlashPolicy_t;

typedef struct
{
    const char *profile_id;
    uint8_t cell_count;
    uint32_t capacity_mah;
    uint32_t rsense_uohm;
    int8_t current_polarity;

    BMS_AfeStartupConfig_t afe_startup;
    const char *ntc_model_id;
    const BMS_NtcPoint_t *ntc_points;
    uint16_t ntc_point_count;

    BMS_ThresholdPolicy_t sw_ov;
    BMS_ThresholdPolicy_t sw_uv;
    BMS_ThresholdPolicy_t sw_oc_charge;
    BMS_ThresholdPolicy_t sw_oc_discharge;
    BMS_TemperaturePolicy_t charge_temperature;
    BMS_TemperaturePolicy_t discharge_temperature;
    BMS_FreshnessPolicy_t freshness;
    BMS_AfeCommPolicy_t afe_comm;
    BMS_OcdEscalationPolicy_t ocd_escalation;
    uint32_t service_reset_qualify_ms;

    BMS_StatePolicy_t state;
    BMS_HealthPolicy_t health;
    BMS_SocPolicy_t soc;
    BMS_BalancePolicy_t balance;
    BMS_CanPolicy_t can;
    BMS_FlashPolicy_t flash;
} BMS_Policy_t;

/* 返回的 profile 与其引用 table 在整个运行期保持 immutable。 */
const BMS_Policy_t *BMS_Policy_Get(void);

/* false 表示 fail-closed configuration error，启动层不得继续创建任务。 */
bool BMS_Policy_Validate(const BMS_Policy_t *policy);

#endif /* BMS_POLICY_H：include guard */
