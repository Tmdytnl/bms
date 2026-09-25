#include "test_phase8_sample_stub.h"

#include "FreeRTOS.h"
#include "event_groups.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "fml_health.h"

#include <stddef.h>
#include <string.h>

#include "fml_sample.h"

typedef enum
{
    TEST_P8_I2C_GROUP_NONE = 0,
    TEST_P8_I2C_GROUP_CELL,
    TEST_P8_I2C_GROUP_PACK,
    TEST_P8_I2C_GROUP_TS
} TestPhase8I2cGroup_t;

struct QueueDefinition
{
    uint8_t kind;
};

struct EventGroupDef_t
{
    EventBits_t bits;
};

#define TEST_P8_OBJECT_I2C_MUTEX       ((uint8_t)1U)
#define TEST_P8_OBJECT_DATA_MUTEX      ((uint8_t)2U)

static struct QueueDefinition s_i2c_mutex_object;
static struct QueueDefinition s_data_mutex_object;
static struct EventGroupDef_t s_event_group_object;
static BQ76940_t s_device;
static TestPhase8I2cGroup_t s_i2c_group;
static uint32_t s_scheduler_depth;
static uint32_t s_scheduler_zero_depth_epoch;
static TickType_t s_tick;
static bool s_protect_take_context;
static bool s_protect_owns_i2c;
static bool s_pending_xready_transition;
static const BMS_NtcPoint_t s_aba_ntc_table_a[3] =
{
    { 20000UL, (BMS_TemperatureDeciC_t)0 },
    { 10000UL, (BMS_TemperatureDeciC_t)250 },
    {  5000UL, (BMS_TemperatureDeciC_t)500 }
};
static const BMS_NtcPoint_t s_aba_ntc_table_b[3] =
{
    { 21000UL, (BMS_TemperatureDeciC_t)-10 },
    { 11000UL, (BMS_TemperatureDeciC_t)240 },
    {  6000UL, (BMS_TemperatureDeciC_t)490 }
};

static void TestPhase8SampleStub_RunProtectI2cAttempt(void);
static void TestPhase8SampleStub_ActivateXready(void);

SemaphoreHandle_t xI2CMutex;
SemaphoreHandle_t xDataMutex;
SemaphoreHandle_t xAfeAlertSem;
QueueHandle_t xCanTxQueue;
QueueHandle_t xCanRxQueue;
QueueHandle_t xCcSampleQueue;
EventGroupHandle_t xSysEvents;

TestPhase8SampleStubControl_t g_phase8_sample_stub_control;
TestPhase8SampleStubObservation_t g_phase8_sample_stub_observation;

static uint32_t TestPhase8SampleStub_NextEvent(void)
{
    ++g_phase8_sample_stub_observation.event_serial;
    return g_phase8_sample_stub_observation.event_serial;
}

void TestPhase8SampleStub_ClearObservation(void)
{
    (void)memset(&g_phase8_sample_stub_observation, 0,
                 sizeof(g_phase8_sample_stub_observation));
    s_scheduler_depth = 0UL;
    s_scheduler_zero_depth_epoch = 0UL;
    s_i2c_group = TEST_P8_I2C_GROUP_NONE;
    s_protect_take_context = false;
    s_protect_owns_i2c = false;
    s_pending_xready_transition = false;
    s_event_group_object.bits = (EventBits_t)0U;
}

void TestPhase8SampleStub_Reset(void)
{
    uint32_t index;

    (void)memset(&g_phase8_sample_stub_control, 0,
                 sizeof(g_phase8_sample_stub_control));
    s_i2c_mutex_object.kind = TEST_P8_OBJECT_I2C_MUTEX;
    s_data_mutex_object.kind = TEST_P8_OBJECT_DATA_MUTEX;
    s_event_group_object.bits = (EventBits_t)0U;
    xI2CMutex = &s_i2c_mutex_object;
    xDataMutex = &s_data_mutex_object;
    xAfeAlertSem = NULL;
    xCanTxQueue = NULL;
    xCanRxQueue = NULL;
    xCcSampleQueue = NULL;
    xSysEvents = &s_event_group_object;

    s_device.bus = NULL;
    s_device.initialized = true;
    s_tick = (TickType_t)0U;

    g_phase8_sample_stub_control.cell_status = BQ76940_STATUS_OK;
    g_phase8_sample_stub_control.pack_status = BQ76940_STATUS_OK;
    g_phase8_sample_stub_control.current_status = BQ76940_STATUS_OK;
    g_phase8_sample_stub_control.ts_read_status = BQ76940_STATUS_OK;
    g_phase8_sample_stub_control.ts_convert_status = BQ76940_STATUS_OK;
    for (index = 0UL; index < (uint32_t)BMS_CELL_COUNT; ++index)
    {
        g_phase8_sample_stub_control.cell_voltage_mv[index] =
            (BMS_CellVoltageMv_t)(3500U + index);
    }
    g_phase8_sample_stub_control.pack_voltage_mv =
        (BMS_PackVoltageMv_t)45600UL;
    g_phase8_sample_stub_control.ts1_raw14 = (uint16_t)4000U;
    g_phase8_sample_stub_control.ts1_resistance_ohm = 15000UL;
    g_phase8_sample_stub_control.latest_cc.valid = false;
    g_phase8_sample_stub_control.latest_cc.xready_generation = 0UL;
    g_phase8_sample_stub_control.replacement_cc.valid = false;
    g_phase8_sample_stub_control.replacement_cc.xready_generation = 0UL;
    g_phase8_sample_stub_control.xready_state.xready_generation = 0UL;
    g_phase8_sample_stub_control.xready_state.active = false;
    g_phase8_sample_stub_control.scheduler_state =
        taskSCHEDULER_NOT_STARTED;
    TestPhase8SampleStub_ClearObservation();
}

BQ76940_t *TestPhase8SampleStub_Device(void)
{
    return &s_device;
}

const BMS_NtcPoint_t *TestPhase8SampleStub_AbaNtcTableA(void)
{
    return s_aba_ntc_table_a;
}

uint16_t TestPhase8SampleStub_AbaNtcPointCount(void)
{
    return (uint16_t)3U;
}

bool TestPhase8SampleStub_LocksBalanced(void)
{
    return !g_phase8_sample_stub_observation.i2c_locked &&
           !g_phase8_sample_stub_observation.data_locked &&
           (g_phase8_sample_stub_observation.i2c_take_success_count ==
            g_phase8_sample_stub_observation.i2c_give_count) &&
           (g_phase8_sample_stub_observation.data_take_success_count ==
            g_phase8_sample_stub_observation.data_give_count) &&
           (g_phase8_sample_stub_observation.scheduler_suspend_count ==
            g_phase8_sample_stub_observation.scheduler_resume_count) &&
           (s_scheduler_depth == 0UL) &&
           !s_pending_xready_transition;
}

static void TestPhase8SampleStub_ActivateXready(void)
{
    if (!g_phase8_sample_stub_control.xready_state.active)
    {
        g_phase8_sample_stub_control.xready_state.xready_generation =
            BMS_PROTECT_XREADY_GENERATION_NEXT(
                g_phase8_sample_stub_control.xready_state
                    .xready_generation);
        g_phase8_sample_stub_control.latest_cc.valid = false;
    }
    g_phase8_sample_stub_control.xready_state.active = true;
    ++g_phase8_sample_stub_observation.xready_transition_count;
}

BaseType_t xQueueSemaphoreTake(QueueHandle_t queue,
                               TickType_t ticks_to_wait)
{
    struct QueueDefinition *object;
    uint32_t order;

    object = (struct QueueDefinition *)queue;
    order = TestPhase8SampleStub_NextEvent();
    if ((object != NULL) &&
        (object->kind == TEST_P8_OBJECT_I2C_MUTEX))
    {
        ++g_phase8_sample_stub_observation.i2c_take_attempt_count;
        if (s_protect_take_context)
        {
            ++g_phase8_sample_stub_observation
                  .protect_i2c_take_attempt_count;
            g_phase8_sample_stub_observation.protect_last_wait_ticks =
                ticks_to_wait;
            if (g_phase8_sample_stub_observation
                    .protect_i2c_take_attempt_count == 1UL)
            {
                g_phase8_sample_stub_observation
                    .protect_first_take_order = order;
            }
            else
            {
                g_phase8_sample_stub_observation
                    .protect_retry_take_order = order;
            }
            if (ticks_to_wait !=
                pdMS_TO_TICKS(BMS_PROTECT_I2C_TIMEOUT_MS))
            {
                ++g_phase8_sample_stub_observation
                      .lock_protocol_violation_count;
            }
        }
        else if (ticks_to_wait !=
                 pdMS_TO_TICKS(BMS_I2C_MUTEX_TIMEOUT_MS))
        {
            ++g_phase8_sample_stub_observation.lock_protocol_violation_count;
        }
        if (!s_protect_take_context &&
            (g_phase8_sample_stub_control.fail_i2c_take_ordinal != 0UL) &&
            (g_phase8_sample_stub_observation.i2c_take_attempt_count ==
             g_phase8_sample_stub_control.fail_i2c_take_ordinal))
        {
            g_phase8_sample_stub_control.fail_i2c_take_ordinal = 0UL;
            return pdFALSE;
        }
        if (g_phase8_sample_stub_observation.i2c_locked)
        {
            if (s_protect_take_context)
            {
                ++g_phase8_sample_stub_observation
                      .protect_i2c_timeout_count;
                ++g_phase8_sample_stub_observation
                      .protect_i2c_while_sample_count;
            }
            else
            {
                ++g_phase8_sample_stub_observation
                      .lock_protocol_violation_count;
            }
            return pdFALSE;
        }
        if (g_phase8_sample_stub_observation.data_locked)
        {
            ++g_phase8_sample_stub_observation.i2c_data_nesting_count;
        }
        g_phase8_sample_stub_observation.i2c_locked = true;
        ++g_phase8_sample_stub_observation.i2c_take_success_count;
        if (s_protect_take_context)
        {
            ++g_phase8_sample_stub_observation
                  .protect_i2c_take_success_count;
            s_protect_owns_i2c = true;
        }
        return pdTRUE;
    }
    if ((object != NULL) &&
        (object->kind == TEST_P8_OBJECT_DATA_MUTEX))
    {
        ++g_phase8_sample_stub_observation.data_take_attempt_count;
        if (ticks_to_wait != (TickType_t)0U)
        {
            ++g_phase8_sample_stub_observation.lock_protocol_violation_count;
        }
        if (g_phase8_sample_stub_observation.data_take_attempt_count == 1UL)
        {
            g_phase8_sample_stub_observation.first_data_take_order = order;
        }
        else if (g_phase8_sample_stub_observation.data_take_attempt_count ==
                 2UL)
        {
            g_phase8_sample_stub_observation.second_data_take_order = order;
            g_phase8_sample_stub_observation.publish_take_order = order;
            g_phase8_sample_stub_observation.publish_scheduler_epoch =
                s_scheduler_zero_depth_epoch;
            g_phase8_sample_stub_observation.publish_outer_depth =
                s_scheduler_depth;
        }
        if ((g_phase8_sample_stub_control.fail_data_take_ordinal != 0UL) &&
            (g_phase8_sample_stub_observation.data_take_attempt_count ==
             g_phase8_sample_stub_control.fail_data_take_ordinal))
        {
            g_phase8_sample_stub_control.fail_data_take_ordinal = 0UL;
            return pdFALSE;
        }
        if (g_phase8_sample_stub_observation.data_locked)
        {
            ++g_phase8_sample_stub_observation.lock_protocol_violation_count;
            return pdFALSE;
        }
        if (g_phase8_sample_stub_observation.i2c_locked)
        {
            ++g_phase8_sample_stub_observation.i2c_data_nesting_count;
        }
        g_phase8_sample_stub_observation.data_locked = true;
        ++g_phase8_sample_stub_observation.data_take_success_count;
        return pdTRUE;
    }

    ++g_phase8_sample_stub_observation.lock_protocol_violation_count;
    return pdFALSE;
}

BaseType_t xQueueGenericSend(QueueHandle_t queue,
                             const void *const item,
                             TickType_t ticks_to_wait,
                             BaseType_t copy_position)
{
    struct QueueDefinition *object;
    TestPhase8I2cGroup_t completed_group;
    bool protect_give;
    bool run_protect_retry;

    (void)item;
    (void)ticks_to_wait;
    (void)copy_position;
    object = (struct QueueDefinition *)queue;
    (void)TestPhase8SampleStub_NextEvent();
    if ((object != NULL) &&
        (object->kind == TEST_P8_OBJECT_I2C_MUTEX))
    {
        if (!g_phase8_sample_stub_observation.i2c_locked)
        {
            ++g_phase8_sample_stub_observation.lock_protocol_violation_count;
            return pdFALSE;
        }
        completed_group = s_i2c_group;
        protect_give = s_protect_owns_i2c;
        run_protect_retry =
            !protect_give &&
            (completed_group == TEST_P8_I2C_GROUP_CELL) &&
            g_phase8_sample_stub_control.inject_protect_i2c_contention;
        g_phase8_sample_stub_observation.i2c_locked = false;
        ++g_phase8_sample_stub_observation.i2c_give_count;
        if (protect_give)
        {
            ++g_phase8_sample_stub_observation.protect_i2c_give_count;
            g_phase8_sample_stub_observation.protect_retry_give_order =
                g_phase8_sample_stub_observation.event_serial;
            s_protect_owns_i2c = false;
        }
        else if (completed_group == TEST_P8_I2C_GROUP_CELL)
        {
            g_phase8_sample_stub_observation.cell_give_order =
                g_phase8_sample_stub_observation.event_serial;
            if (g_phase8_sample_stub_control
                    .inject_xready_after_cell_give)
            {
                g_phase8_sample_stub_control
                    .inject_xready_after_cell_give = false;
                TestPhase8SampleStub_ActivateXready();
            }
        }
        else if (completed_group == TEST_P8_I2C_GROUP_PACK)
        {
            g_phase8_sample_stub_observation.pack_give_order =
                g_phase8_sample_stub_observation.event_serial;
            if (g_phase8_sample_stub_control.replace_latest_cc_on_pack_give)
            {
                g_phase8_sample_stub_control.latest_cc =
                    g_phase8_sample_stub_control.replacement_cc;
                g_phase8_sample_stub_control.replace_latest_cc_on_pack_give =
                    false;
                ++g_phase8_sample_stub_observation
                      .latest_cc_replacement_count;
            }
            if (g_phase8_sample_stub_control
                    .inject_xready_after_pack_give)
            {
                g_phase8_sample_stub_control
                    .inject_xready_after_pack_give = false;
                TestPhase8SampleStub_ActivateXready();
            }
            if (g_phase8_sample_stub_control
                    .inject_ntc_configuration_aba_after_pack_give)
            {
                g_phase8_sample_stub_control
                    .inject_ntc_configuration_aba_after_pack_give = false;
                ++g_phase8_sample_stub_observation
                      .configuration_aba_injection_count;
                if (FML_Sample_SetNtcTable(s_aba_ntc_table_b, 3U))
                {
                    ++g_phase8_sample_stub_observation
                          .configuration_aba_set_success_count;
                }
                if (FML_Sample_SetNtcTable(s_aba_ntc_table_a, 3U))
                {
                    ++g_phase8_sample_stub_observation
                          .configuration_aba_set_success_count;
                }
            }
            if (g_phase8_sample_stub_control
                    .inject_ntc_configuration_wrap_after_pack_give)
            {
                g_phase8_sample_stub_control
                    .inject_ntc_configuration_wrap_after_pack_give = false;
                ++g_phase8_sample_stub_observation
                      .configuration_wrap_injection_count;
                if (FML_Sample_SetNtcTable(s_aba_ntc_table_a, 3U))
                {
                    ++g_phase8_sample_stub_observation
                          .configuration_wrap_set_success_count;
                }
            }
        }
        s_i2c_group = TEST_P8_I2C_GROUP_NONE;
        if (run_protect_retry)
        {
            g_phase8_sample_stub_control.inject_protect_i2c_contention =
                false;
            TestPhase8SampleStub_RunProtectI2cAttempt();
        }
        return pdTRUE;
    }
    if ((object != NULL) &&
        (object->kind == TEST_P8_OBJECT_DATA_MUTEX))
    {
        if (!g_phase8_sample_stub_observation.data_locked)
        {
            ++g_phase8_sample_stub_observation.lock_protocol_violation_count;
            return pdFALSE;
        }
        g_phase8_sample_stub_observation.data_locked = false;
        ++g_phase8_sample_stub_observation.data_give_count;
        return pdTRUE;
    }

    ++g_phase8_sample_stub_observation.lock_protocol_violation_count;
    return pdFALSE;
}

static void TestPhase8SampleStub_RunProtectI2cAttempt(void)
{
    BaseType_t taken;

    s_protect_take_context = true;
    taken = xSemaphoreTake(
        xI2CMutex,
        pdMS_TO_TICKS(BMS_PROTECT_I2C_TIMEOUT_MS));
    s_protect_take_context = false;
    if (taken == pdTRUE)
    {
        (void)xSemaphoreGive(xI2CMutex);
    }
}

void vTaskSuspendAll(void)
{
    ++s_scheduler_depth;
    ++g_phase8_sample_stub_observation.scheduler_suspend_count;
    if (g_phase8_sample_stub_observation.scheduler_max_depth <
        s_scheduler_depth)
    {
        g_phase8_sample_stub_observation.scheduler_max_depth =
            s_scheduler_depth;
    }
}

BaseType_t xTaskResumeAll(void)
{
    ++g_phase8_sample_stub_observation.scheduler_resume_count;
    if (s_scheduler_depth == 0UL)
    {
        ++g_phase8_sample_stub_observation.lock_protocol_violation_count;
    }
    else
    {
        --s_scheduler_depth;
        if (s_scheduler_depth == 0UL)
        {
            ++s_scheduler_zero_depth_epoch;
            if (s_pending_xready_transition)
            {
                s_pending_xready_transition = false;
                g_phase8_sample_stub_observation
                    .pending_transition_apply_order =
                    TestPhase8SampleStub_NextEvent();
                TestPhase8SampleStub_ActivateXready();
            }
        }
    }
    return pdFALSE;
}

BaseType_t xTaskGetSchedulerState(void)
{
    ++g_phase8_sample_stub_observation.scheduler_state_get_count;
    return g_phase8_sample_stub_control.scheduler_state;
}

TickType_t xTaskGetTickCount(void)
{
    return s_tick;
}

BaseType_t xTaskDelayUntil(TickType_t *const previous_wake_time,
                           const TickType_t increment)
{
    if (previous_wake_time != NULL)
    {
        *previous_wake_time += increment;
        s_tick = *previous_wake_time;
    }
    return pdTRUE;
}

EventBits_t xEventGroupSetBits(EventGroupHandle_t event_group,
                               const EventBits_t bits_to_set)
{
    struct EventGroupDef_t *object;

    object = (struct EventGroupDef_t *)event_group;
    (void)TestPhase8SampleStub_NextEvent();
    ++g_phase8_sample_stub_observation.event_set_call_count;
    if (g_phase8_sample_stub_observation.i2c_locked ||
        g_phase8_sample_stub_observation.data_locked)
    {
        ++g_phase8_sample_stub_observation.event_set_while_locked_count;
    }
    if (object != &s_event_group_object)
    {
        ++g_phase8_sample_stub_observation.lock_protocol_violation_count;
        return (EventBits_t)0U;
    }
    object->bits |= bits_to_set;
    g_phase8_sample_stub_observation.event_bits = object->bits;
    return object->bits;
}

BQ76940_Status_t BSP_BQ76940_ReadCellVoltages13(
    BQ76940_t *device,
    const BQ76940_Calibration_t *calibration,
    uint16_t cell_mv[BQ76940_MEASUREMENT_CELL_COUNT])
{
    uint32_t index;

    (void)calibration;
    (void)TestPhase8SampleStub_NextEvent();
    ++g_phase8_sample_stub_observation.cell_call_count;
    s_i2c_group = TEST_P8_I2C_GROUP_CELL;
    if (!g_phase8_sample_stub_observation.i2c_locked)
    {
        ++g_phase8_sample_stub_observation.driver_without_i2c_count;
    }
    if (g_phase8_sample_stub_control.inject_protect_i2c_contention)
    {
        TestPhase8SampleStub_RunProtectI2cAttempt();
    }
    if ((device != &s_device) || (cell_mv == NULL))
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    if (g_phase8_sample_stub_control.cell_status != BQ76940_STATUS_OK)
    {
        if (g_phase8_sample_stub_control.write_one_cell_before_cell_failure)
        {
            cell_mv[0] =
                g_phase8_sample_stub_control.cell_voltage_mv[0];
        }
        return g_phase8_sample_stub_control.cell_status;
    }
    for (index = 0UL; index < (uint32_t)BMS_CELL_COUNT; ++index)
    {
        cell_mv[index] =
            g_phase8_sample_stub_control.cell_voltage_mv[index];
    }
    return BQ76940_STATUS_OK;
}

BQ76940_Status_t BSP_BQ76940_ReadPackVoltageMv(
    BQ76940_t *device,
    const BQ76940_Calibration_t *calibration,
    uint32_t *pack_mv)
{
    (void)calibration;
    (void)TestPhase8SampleStub_NextEvent();
    ++g_phase8_sample_stub_observation.pack_call_count;
    g_phase8_sample_stub_observation.pack_call_order =
        g_phase8_sample_stub_observation.event_serial;
    s_i2c_group = TEST_P8_I2C_GROUP_PACK;
    if (!g_phase8_sample_stub_observation.i2c_locked)
    {
        ++g_phase8_sample_stub_observation.driver_without_i2c_count;
    }
    if ((device != &s_device) || (pack_mv == NULL))
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    if (g_phase8_sample_stub_control.pack_status != BQ76940_STATUS_OK)
    {
        return g_phase8_sample_stub_control.pack_status;
    }
    *pack_mv = g_phase8_sample_stub_control.pack_voltage_mv;
    return BQ76940_STATUS_OK;
}

BQ76940_Status_t BSP_BQ76940_ConvertCcRawToCurrentMa(
    int16_t cc_raw,
    uint32_t rsense_uohm,
    int8_t polarity,
    int32_t *current_ma)
{
    int64_t numerator;

    (void)TestPhase8SampleStub_NextEvent();
    ++g_phase8_sample_stub_observation.current_convert_call_count;
    g_phase8_sample_stub_observation.current_convert_order =
        g_phase8_sample_stub_observation.event_serial;
    g_phase8_sample_stub_observation.last_cc_raw = cc_raw;
    g_phase8_sample_stub_observation.last_rsense_uohm = rsense_uohm;
    g_phase8_sample_stub_observation.last_current_polarity = polarity;
    if (g_phase8_sample_stub_observation.i2c_locked)
    {
        ++g_phase8_sample_stub_observation.conversion_while_i2c_count;
    }
    if (g_phase8_sample_stub_control.current_status != BQ76940_STATUS_OK)
    {
        return g_phase8_sample_stub_control.current_status;
    }
    if ((current_ma == NULL) || (rsense_uohm == 0UL) ||
        ((polarity != (int8_t)1) && (polarity != (int8_t)-1)))
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    numerator = (int64_t)cc_raw * (int64_t)8440LL;
    numerator *= (int64_t)polarity;
    *current_ma = (int32_t)(numerator / (int64_t)rsense_uohm);
    return BQ76940_STATUS_OK;
}

BQ76940_Status_t BSP_BQ76940_ReadTs1Raw(BQ76940_t *device,
                                    uint16_t *ts1_raw14)
{
    (void)TestPhase8SampleStub_NextEvent();
    ++g_phase8_sample_stub_observation.ts_read_call_count;
    s_i2c_group = TEST_P8_I2C_GROUP_TS;
    if (!g_phase8_sample_stub_observation.i2c_locked)
    {
        ++g_phase8_sample_stub_observation.driver_without_i2c_count;
    }
    if ((device != &s_device) || (ts1_raw14 == NULL))
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    if (g_phase8_sample_stub_control.ts_read_status != BQ76940_STATUS_OK)
    {
        return g_phase8_sample_stub_control.ts_read_status;
    }
    *ts1_raw14 = g_phase8_sample_stub_control.ts1_raw14;
    return BQ76940_STATUS_OK;
}

BQ76940_Status_t BSP_BQ76940_ConvertTs1RawToResistanceOhm(
    uint16_t ts1_raw14,
    uint32_t *resistance_ohm)
{
    (void)ts1_raw14;
    (void)TestPhase8SampleStub_NextEvent();
    ++g_phase8_sample_stub_observation.ts_convert_call_count;
    if (g_phase8_sample_stub_observation.i2c_locked)
    {
        ++g_phase8_sample_stub_observation.conversion_while_i2c_count;
    }
    if (g_phase8_sample_stub_control.ts_convert_status !=
        BQ76940_STATUS_OK)
    {
        return g_phase8_sample_stub_control.ts_convert_status;
    }
    if (resistance_ohm == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    *resistance_ohm =
        g_phase8_sample_stub_control.ts1_resistance_ohm;
    return BQ76940_STATUS_OK;
}

bool FML_Protect_GetLatestCc(BMS_ProtectLatestCc_t *snapshot)
{
    bool available;

    (void)TestPhase8SampleStub_NextEvent();
    ++g_phase8_sample_stub_observation.protect_get_call_count;
    g_phase8_sample_stub_observation.protect_get_order =
        g_phase8_sample_stub_observation.event_serial;
    if (g_phase8_sample_stub_observation.i2c_locked ||
        g_phase8_sample_stub_observation.data_locked)
    {
        ++g_phase8_sample_stub_observation.protect_get_while_locked_count;
    }
    if (snapshot == NULL)
    {
        return false;
    }
    vTaskSuspendAll();
    *snapshot = g_phase8_sample_stub_control.latest_cc;
    available = snapshot->valid &&
        !g_phase8_sample_stub_control.xready_state.active &&
        (snapshot->xready_generation ==
         g_phase8_sample_stub_control.xready_state.xready_generation);
    if (!available)
    {
        snapshot->valid = false;
    }
    (void)xTaskResumeAll();
    return available;
}

bool FML_Protect_GetXreadyState(BMS_ProtectXreadyState_t *snapshot)
{
    uint32_t order;

    order = TestPhase8SampleStub_NextEvent();
    ++g_phase8_sample_stub_observation.xready_get_call_count;
    if ((snapshot == NULL) ||
        g_phase8_sample_stub_control.fail_xready_get)
    {
        return false;
    }
    if (g_phase8_sample_stub_observation.xready_get_call_count == 2UL)
    {
        g_phase8_sample_stub_observation.final_guard_get_order = order;
        g_phase8_sample_stub_observation.final_guard_scheduler_epoch =
            s_scheduler_zero_depth_epoch;
        g_phase8_sample_stub_observation.final_guard_outer_depth =
            s_scheduler_depth;
    }

    vTaskSuspendAll();
    *snapshot = g_phase8_sample_stub_control.xready_state;
    if ((g_phase8_sample_stub_observation.xready_get_call_count == 2UL) &&
        g_phase8_sample_stub_control
            .pend_xready_transition_on_final_guard)
    {
        g_phase8_sample_stub_control
            .pend_xready_transition_on_final_guard = false;
        s_pending_xready_transition = true;
        ++g_phase8_sample_stub_observation
              .pending_transition_deferred_count;
    }
    (void)xTaskResumeAll();
    return true;
}

bool FML_Protect_XreadyBindingIsCurrent(
    const BMS_ProtectXreadyState_t *state,
    uint32_t bound_generation)
{
    return (state != NULL) && !state->active &&
           (state->xready_generation == bound_generation);
}
void FML_Health_Heartbeat(BMS_HealthTaskId_t task_id)
{
    (void)task_id;
}
