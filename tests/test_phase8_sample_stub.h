#ifndef TEST_PHASE8_SAMPLE_STUB_H

#include "FreeRTOS.h"
#include "event_groups.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#define TEST_PHASE8_SAMPLE_STUB_H

#include <stdbool.h>
#include <stdint.h>

#include "apl_rtos.h"
#include "fml_config.h"
#include "fml_ntc.h"
#include "fml_protect.h"
#include "fml_types.h"
#include "bsp_bq76940_measurement.h"

typedef struct
{
    BQ76940_Status_t cell_status;
    BQ76940_Status_t pack_status;
    BQ76940_Status_t current_status;
    BQ76940_Status_t ts_read_status;
    BQ76940_Status_t ts_convert_status;
    BMS_CellVoltageMv_t cell_voltage_mv[BMS_CELL_COUNT];
    bool write_one_cell_before_cell_failure;
    BMS_PackVoltageMv_t pack_voltage_mv;
    uint16_t ts1_raw14;
    uint32_t ts1_resistance_ohm;
    BMS_ProtectLatestCc_t latest_cc;
    bool replace_latest_cc_on_pack_give;
    BMS_ProtectLatestCc_t replacement_cc;
    BMS_ProtectXreadyState_t xready_state;
    bool fail_xready_get;
    bool inject_xready_after_cell_give;
    bool inject_xready_after_pack_give;
    /* pack mutex give 后执行合法 NTC table A→B→A setter。 */
    bool inject_ntc_configuration_aba_after_pack_give;
    /* 为 MAX→0 vector 在 pack give 后重装一次 table A。 */
    bool inject_ntc_configuration_wrap_after_pack_give;
    /* final guard 时 queue Protect transition；stub 只在外层 scheduler exclusion 结束后应用。 */
    bool pend_xready_transition_on_final_guard;
    /* 确定性调度模型：cell driver 内注入 bounded Protect mutex take，并在 Sample give 后 retry。 */
    bool inject_protect_i2c_contention;
    BaseType_t scheduler_state;
    uint32_t fail_i2c_take_ordinal;
    uint32_t fail_data_take_ordinal;
} TestPhase8SampleStubControl_t;

typedef struct
{
    uint32_t event_serial;
    uint32_t i2c_take_attempt_count;
    uint32_t i2c_take_success_count;
    uint32_t i2c_give_count;
    uint32_t data_take_attempt_count;
    uint32_t data_take_success_count;
    uint32_t data_give_count;
    uint32_t scheduler_suspend_count;
    uint32_t scheduler_resume_count;
    uint32_t scheduler_state_get_count;
    uint32_t scheduler_max_depth;
    uint32_t cell_call_count;
    uint32_t pack_call_count;
    uint32_t current_convert_call_count;
    uint32_t ts_read_call_count;
    uint32_t ts_convert_call_count;
    uint32_t protect_get_call_count;
    uint32_t xready_get_call_count;
    uint32_t xready_transition_count;
    uint32_t final_guard_get_order;
    uint32_t final_guard_scheduler_epoch;
    uint32_t final_guard_outer_depth;
    uint32_t publish_take_order;
    uint32_t publish_scheduler_epoch;
    uint32_t publish_outer_depth;
    uint32_t pending_transition_deferred_count;
    uint32_t pending_transition_apply_order;
    uint32_t event_set_call_count;
    uint32_t latest_cc_replacement_count;
    uint32_t configuration_aba_injection_count;
    uint32_t configuration_aba_set_success_count;
    uint32_t configuration_wrap_injection_count;
    uint32_t configuration_wrap_set_success_count;
    uint32_t protect_i2c_take_attempt_count;
    uint32_t protect_i2c_timeout_count;
    uint32_t protect_i2c_take_success_count;
    uint32_t protect_i2c_give_count;
    uint32_t protect_i2c_while_sample_count;
    uint32_t lock_protocol_violation_count;
    uint32_t i2c_data_nesting_count;
    uint32_t driver_without_i2c_count;
    uint32_t conversion_while_i2c_count;
    uint32_t protect_get_while_locked_count;
    uint32_t event_set_while_locked_count;
    uint32_t pack_give_order;
    uint32_t cell_give_order;
    uint32_t protect_first_take_order;
    uint32_t protect_retry_take_order;
    uint32_t protect_retry_give_order;
    uint32_t pack_call_order;
    uint32_t protect_get_order;
    uint32_t current_convert_order;
    uint32_t first_data_take_order;
    uint32_t second_data_take_order;
    uint32_t last_rsense_uohm;
    int8_t last_current_polarity;
    int16_t last_cc_raw;
    TickType_t protect_last_wait_ticks;
    EventBits_t event_bits;
    bool i2c_locked;
    bool data_locked;
} TestPhase8SampleStubObservation_t;

extern TestPhase8SampleStubControl_t g_phase8_sample_stub_control;
extern TestPhase8SampleStubObservation_t g_phase8_sample_stub_observation;

void TestPhase8SampleStub_Reset(void);
void TestPhase8SampleStub_ClearObservation(void);
BQ76940_t *TestPhase8SampleStub_Device(void);
const BMS_NtcPoint_t *TestPhase8SampleStub_AbaNtcTableA(void);
uint16_t TestPhase8SampleStub_AbaNtcPointCount(void);
bool TestPhase8SampleStub_LocksBalanced(void);

#endif /* TEST_PHASE8_SAMPLE_STUB_H */
