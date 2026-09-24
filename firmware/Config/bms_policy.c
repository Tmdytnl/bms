#include "bms_policy.h"

#include <stddef.h>
#include <string.h>

#include "bms_config.h"
#include "bq76940_control.h"

/* 仿真 V1 的固定 NTC 电阻与温度插值节点，仅供策略只读引用。 */
static const BMS_NtcPoint_t s_sim_ntc_points[BMS_POLICY_NTC_POINT_COUNT] =
{
    {200204UL, -300}, {105385UL, -200}, {58246UL, -100},
    {33621UL, 0},     {20175UL, 100},   {12535UL, 200},
    {10000UL, 250},   {8037UL, 300},    {5301UL, 400},
    {3588UL, 500},    {2486UL, 600},    {1760UL, 700},
    {1270UL, 800},    {934UL, 900},     {698UL, 1000}
};

/* 七个必需任务的 heartbeat 时效窗口，供 StateTask 看门狗判定。 */
static const BMS_TaskHealthPolicy_t
    s_required_task_roster[BMS_POLICY_REQUIRED_TASK_COUNT] =
{
    {BMS_HEALTH_TASK_PROTECT, 300UL},
    {BMS_HEALTH_TASK_SAMPLE, 750UL},
    {BMS_HEALTH_TASK_STATE, 300UL},
    {BMS_HEALTH_TASK_SOC, 2500UL},
    {BMS_HEALTH_TASK_BALANCE, 2500UL},
    {BMS_HEALTH_TASK_CAN_TX, 2500UL},
    {BMS_HEALTH_TASK_CAN_RX, 500UL}
};

/* 六帧周期诊断所用的标准 CAN ID，编码顺序与帧格式一致。 */
static const uint16_t s_can_tx_ids[BMS_POLICY_CAN_TX_ID_COUNT] =
{
    0x180U, 0x181U, 0x182U, 0x183U, 0x184U, 0x185U
};

/* 启动期验证后绑定的完整 V1 不可变策略。 */
static const BMS_Policy_t s_sim_policy =
{
    BMS_POLICY_PROFILE_ID_SIM_V1,
    13U,
    20000UL,
    4000UL,
    1,
    {
        true, 4250U, 2800U,
        true, false, 2U, 6U,
        true, 5U, 12U,
        true, 1U, 1U
    },
    BMS_POLICY_NTC_MODEL_ID_SIM_V1,
    s_sim_ntc_points,
    BMS_POLICY_NTC_POINT_COUNT,
    {4200, 4100, 500UL, 2000UL},
    {3000, 3200, 500UL, 2000UL},
    {5000, 4500, 500UL, 2000UL},
    {-10000, -9000, 500UL, 2000UL},
    {0, 50, 500, 450, 1000UL, 2000UL},
    {-200, -150, 600, 550, 1000UL, 2000UL},
    {1000UL, 1000UL, 5000UL, 2U},
    {3U, 1000UL, 5000UL, 3U},
    {3U, 60000UL},
    2000UL,
    {
        5000UL,
        100, 1000UL,
        300, 500UL,
        150, 1000UL,
        -300, 500UL,
        -150, 1000UL,
        100UL
    },
    {s_required_task_roster, BMS_POLICY_REQUIRED_TASK_COUNT,
     4000UL, 5000UL},
    {
        20000UL, 500U, 995U, 1000U, 1000UL,
        4180U, 0, 500, 60000UL,
        3050U, 1000, 30000UL
    },
    {
        1000UL, 4100U, 20U, 10U, 4050U, 2U, false,
        0, 450, 3000, 5000UL
    },
    {500000UL, true, s_can_tx_ids, BMS_POLICY_CAN_TX_ID_COUNT,
     0x280U, 1000UL, false},
    {0x0800F800UL, 0x0800FC00UL, 1024U, 60000UL, 10U}
};

const BMS_Policy_t *BMS_Policy_Get(void)
{
    return &s_sim_policy;
}

/* 核对 AFE 保护阈值、延时和采样电阻的可编码范围。 */
static bool BMS_Policy_ValidateAfe(const BMS_AfeStartupConfig_t *afe)
{
    BQ76940_Status_t status;
    uint8_t protect1;
    uint8_t protect2;
    uint8_t protect3;

    if ((afe == NULL) || !afe->ov_uv_trip_present ||
        !afe->protect1_present || !afe->protect2_present ||
        !afe->protect3_present ||
        (afe->ov_trip_mv != 4250U) || (afe->uv_trip_mv != 2800U))
    {
        return false;
    }
    status = BQ76940_Control_ComposeProtect1(
        afe->protect1_rsns,
        afe->protect1_scd_delay_code,
        afe->protect1_scd_threshold_code,
        &protect1);
    if ((status != BQ76940_STATUS_OK) || (protect1 != 0x16U))
    {
        return false;
    }
    status = BQ76940_Control_ComposeProtect2(
        afe->protect2_ocd_delay_code,
        afe->protect2_ocd_threshold_code,
        &protect2);
    if ((status != BQ76940_STATUS_OK) || (protect2 != 0x5CU))
    {
        return false;
    }
    status = BQ76940_Control_ComposeProtect3(
        afe->protect3_uv_delay_code,
        afe->protect3_ov_delay_code,
        &protect3);
    return (status == BQ76940_STATUS_OK) && (protect3 == 0x50U);
}

/* 核对任务健康窗口和看门狗策略的时序约束。 */
static bool BMS_Policy_ValidateHealth(const BMS_HealthPolicy_t *health)
{
    uint8_t index;

    if ((health == NULL) || (health->roster == NULL) ||
        (health->roster_count != BMS_POLICY_REQUIRED_TASK_COUNT) ||
        (health->iwdg_nominal_timeout_ms != 4000UL) ||
        (health->startup_grace_ms < health->iwdg_nominal_timeout_ms))
    {
        return false;
    }
    for (index = 0U; index < health->roster_count; ++index)
    {
        if ((health->roster[index].task_id != (BMS_HealthTaskId_t)index) ||
            (health->roster[index].max_liveness_ms == 0UL) ||
            (health->roster[index].max_liveness_ms >=
             health->iwdg_nominal_timeout_ms))
        {
            return false;
        }
    }
    return true;
}

/* 核对整份不可变策略的数值范围及字段间约束。 */
bool BMS_Policy_Validate(const BMS_Policy_t *policy)
{
    if ((policy == NULL) || (policy->profile_id == NULL) ||
        (strcmp(policy->profile_id, BMS_POLICY_PROFILE_ID_SIM_V1) != 0) ||
        (policy->cell_count != BMS_CELL_COUNT) ||
        (policy->capacity_mah != 20000UL) ||
        (policy->rsense_uohm != 4000UL) ||
        (policy->current_polarity != 1) ||
        (policy->ntc_model_id == NULL) ||
        (strcmp(policy->ntc_model_id,
                BMS_POLICY_NTC_MODEL_ID_SIM_V1) != 0) ||
        (policy->ntc_point_count != BMS_POLICY_NTC_POINT_COUNT) ||
        !BMS_Ntc_ValidateTable(policy->ntc_points,
                               policy->ntc_point_count) ||
        !BMS_Policy_ValidateAfe(&policy->afe_startup))
    {
        return false;
    }

    if ((policy->sw_ov.trigger <= policy->sw_ov.recovery) ||
        (policy->sw_uv.trigger >= policy->sw_uv.recovery) ||
        (policy->sw_oc_charge.trigger <= policy->sw_oc_charge.recovery) ||
        (policy->sw_oc_discharge.trigger >=
         policy->sw_oc_discharge.recovery) ||
        (policy->charge_temperature.low_trigger_decic >=
         policy->charge_temperature.low_recovery_decic) ||
        (policy->charge_temperature.high_trigger_decic <=
         policy->charge_temperature.high_recovery_decic) ||
        (policy->discharge_temperature.low_trigger_decic >=
         policy->discharge_temperature.low_recovery_decic) ||
        (policy->discharge_temperature.high_trigger_decic <=
         policy->discharge_temperature.high_recovery_decic) ||
        (policy->freshness.recovery_fresh_frames == 0U) ||
        (policy->afe_comm.consecutive_failures_to_active == 0U) ||
        (policy->afe_comm.consecutive_successes_to_recover == 0U) ||
        (policy->state.period_ms == 0UL) ||
        !BMS_Policy_ValidateHealth(&policy->health))
    {
        return false;
    }

    if ((policy->soc.capacity_mah != policy->capacity_mah) ||
        (policy->soc.initial_soc_permille > 1000U) ||
        (policy->soc.charge_efficiency_permille > 1000U) ||
        (policy->soc.discharge_efficiency_permille > 1000U) ||
        (policy->balance.max_parallel_cells == 0U) ||
        (policy->balance.max_parallel_cells > 2U) ||
        (policy->balance.start_delta_mv <= policy->balance.stop_delta_mv) ||
        (policy->balance.minimum_cell_mv <=
         policy->balance.low_voltage_stop_mv) ||
        (policy->can.bitrate != BMS_CAN_BITRATE) ||
        !policy->can.standard_11_bit_ids ||
        (policy->can.tx_ids == NULL) ||
        (policy->can.tx_id_count != BMS_POLICY_CAN_TX_ID_COUNT) ||
        policy->can.fault_has_direct_fet_effect ||
        (policy->flash.page_size_bytes != 1024U) ||
        ((policy->flash.slot_b_address - policy->flash.slot_a_address) !=
         (uint32_t)policy->flash.page_size_bytes))
    {
        return false;
    }
    return true;
}
