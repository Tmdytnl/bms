#include "test_phase7.h"

#include <stdbool.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bms_protect.h"
#include "bms_data.h"
#include "bms_health.h"
#include "bq76940_measurement.h"
#include "bq76940_regs.h"
#include "Task/apl_tasks.h"
#include "stm32f10x_exti.h"

#define TEST_P7_QUEUE_CAPACITY       (APL_RTOS_CC_SAMPLE_QUEUE_DEPTH)
#define TEST_P7_SCRIPT_CAPACITY      (16U)
#define TEST_P7_WRITE_CAPACITY       (16U)

void EXTI1_IRQHandler(void);

/* FreeRTOS public header 保持类型 opaque；harness 只提供 Protect 正式路径所需的
 * 最小确定性行为。 */
struct QueueDefinition
{
    uint8_t kind;
};

struct EventGroupDef_t
{
    EventBits_t bits;
};

enum
{
    TEST_P7_KIND_QUEUE = 1,
    TEST_P7_KIND_MUTEX,
    TEST_P7_KIND_SEMAPHORE
};

static struct QueueDefinition s_cc_queue_object;
static struct QueueDefinition s_mutex_object;
static struct QueueDefinition s_alert_sem_object;
static struct EventGroupDef_t s_event_group_object;
static BQ76940_t s_device;

SemaphoreHandle_t xI2CMutex;
SemaphoreHandle_t xDataMutex;
SemaphoreHandle_t xAfeAlertSem;
QueueHandle_t xCanTxQueue;
QueueHandle_t xCanRxQueue;
QueueHandle_t xCcSampleQueue;
EventGroupHandle_t xSysEvents;

void APL_Rtos_NotifyStateUrgent(void)
{
}

void APL_Rtos_RequestProtectService(void)
{
}

bool BMS_Data_GetIdentity(BMS_DataIdentity_t *identity)
{
    if (identity == NULL)
    {
        return false;
    }
    identity->sample_sequence = 0UL;
    identity->afe_generation = 0UL;
    return true;
}

void BMS_Health_Heartbeat(BMS_HealthTaskId_t task_id)
{
    (void)task_id;
}

bool BMS_Policy_Validate(const BMS_Policy_t *policy)
{
    return policy != NULL;
}

static BMS_CcSample_t s_queue[TEST_P7_QUEUE_CAPACITY];
static uint8_t s_queue_head;
static uint8_t s_queue_count;
static bool s_mutex_available;
static uint8_t s_mutex_failures;
static bool s_alert_active;
static bool s_alert_active_once;
static bool s_scheduler_suspended;
static uint32_t s_scheduler_suspend_count;
static uint32_t s_scheduler_resume_count;
static bool s_queue_ops_protected;
static bool s_drop_just_occurred;
static uint8_t s_replacement_failures;
static TickType_t s_tick;
static bool s_alert_sem_available;
static uint8_t s_alert_take_count;
static uint8_t s_task_delay_count;
static TickType_t s_last_task_delay;
static bool s_task_escape_armed;
static bool s_task_guard_failed;
static jmp_buf s_task_escape;
static bool s_exti_pending;
static uint8_t s_exti_clear_count;
static uint8_t s_isr_give_count;
static uint8_t s_exti_init_count;

static uint8_t s_stat_values[TEST_P7_SCRIPT_CAPACITY];
static BQ76940_Status_t s_stat_statuses[TEST_P7_SCRIPT_CAPACITY];
static uint8_t s_stat_count;
static uint8_t s_stat_index;

static int16_t s_cc_values[TEST_P7_SCRIPT_CAPACITY];
static BQ76940_Status_t s_cc_statuses[TEST_P7_SCRIPT_CAPACITY];
static uint8_t s_cc_count;
static uint8_t s_cc_index;

static uint8_t s_write_values[TEST_P7_WRITE_CAPACITY];
static uint8_t s_write_count;
static BQ76940_Status_t s_write_failure_status;
static uint8_t s_write_failures;

static bool s_recovery_result;
static uint8_t s_recovery_calls;

void TestP7_StubReset(void)
{
    memset(s_queue, 0, sizeof(s_queue));
    memset(s_stat_values, 0, sizeof(s_stat_values));
    memset(s_stat_statuses, 0, sizeof(s_stat_statuses));
    memset(s_cc_values, 0, sizeof(s_cc_values));
    memset(s_cc_statuses, 0, sizeof(s_cc_statuses));
    memset(s_write_values, 0, sizeof(s_write_values));

    s_cc_queue_object.kind = TEST_P7_KIND_QUEUE;
    s_mutex_object.kind = TEST_P7_KIND_MUTEX;
    s_alert_sem_object.kind = TEST_P7_KIND_SEMAPHORE;
    s_event_group_object.bits = 0U;

    xI2CMutex = &s_mutex_object;
    xDataMutex = NULL;
    xAfeAlertSem = &s_alert_sem_object;
    xCanTxQueue = NULL;
    xCanRxQueue = NULL;
    xCcSampleQueue = &s_cc_queue_object;
    xSysEvents = &s_event_group_object;

    s_queue_head = 0U;
    s_queue_count = 0U;
    s_mutex_available = true;
    s_mutex_failures = 0U;
    s_alert_active = false;
    s_alert_active_once = false;
    s_scheduler_suspended = false;
    s_scheduler_suspend_count = 0UL;
    s_scheduler_resume_count = 0UL;
    s_queue_ops_protected = true;
    s_drop_just_occurred = false;
    s_replacement_failures = 0U;
    s_tick = 0U;
    s_alert_sem_available = false;
    s_alert_take_count = 0U;
    s_task_delay_count = 0U;
    s_last_task_delay = 0U;
    s_task_escape_armed = false;
    s_task_guard_failed = false;
    s_exti_pending = false;
    s_exti_clear_count = 0U;
    s_isr_give_count = 0U;
    s_exti_init_count = 0U;

    s_stat_count = 0U;
    s_stat_index = 0U;
    s_cc_count = 0U;
    s_cc_index = 0U;
    s_write_count = 0U;
    s_write_failure_status = BQ76940_STATUS_OK;
    s_write_failures = 0U;

    s_recovery_result = false;
    s_recovery_calls = 0U;
    s_device.bus = NULL;
    s_device.initialized = true;

    BMS_Protect_Init();
    BMS_Protect_SetDevice(&s_device);
}

BQ76940_t *TestP7_Device(void)
{
    return &s_device;
}

void TestP7_SetMutexFailures(uint8_t failures)
{
    s_mutex_failures = failures;
}

void TestP7_SetAlertActive(bool active)
{
    s_alert_active = active;
    s_alert_active_once = false;
}

void TestP7_SetStatScript(const uint8_t *values,
                          const BQ76940_Status_t *statuses,
                          uint8_t count)
{
    uint8_t index;

    if (count > TEST_P7_SCRIPT_CAPACITY)
    {
        count = TEST_P7_SCRIPT_CAPACITY;
    }
    s_stat_count = count;
    s_stat_index = 0U;
    for (index = 0U; index < count; ++index)
    {
        s_stat_values[index] = (values != NULL) ? values[index] : 0U;
        s_stat_statuses[index] =
            (statuses != NULL) ? statuses[index] : BQ76940_STATUS_OK;
    }
}

void TestP7_SetCcScript(const int16_t *values,
                        const BQ76940_Status_t *statuses,
                        uint8_t count)
{
    uint8_t index;

    if (count > TEST_P7_SCRIPT_CAPACITY)
    {
        count = TEST_P7_SCRIPT_CAPACITY;
    }
    s_cc_count = count;
    s_cc_index = 0U;
    for (index = 0U; index < count; ++index)
    {
        s_cc_values[index] = (values != NULL) ? values[index] : 0;
        s_cc_statuses[index] =
            (statuses != NULL) ? statuses[index] : BQ76940_STATUS_OK;
    }
}

void TestP7_SetWriteFailure(BQ76940_Status_t status, uint8_t failures)
{
    s_write_failure_status = status;
    s_write_failures = failures;
}

void TestP7_SetReplacementFailures(uint8_t failures)
{
    s_replacement_failures = failures;
}

uint8_t TestP7_StatReadCount(void)
{
    return s_stat_index;
}

uint8_t TestP7_CcReadCount(void)
{
    return s_cc_index;
}

uint8_t TestP7_WriteCount(void)
{
    return s_write_count;
}

uint8_t TestP7_WriteValue(uint8_t index)
{
    if (index >= s_write_count)
    {
        return 0U;
    }
    return s_write_values[index];
}

uint8_t TestP7_QueueCount(void)
{
    return s_queue_count;
}

bool TestP7_QueuePop(BMS_CcSample_t *sample)
{
    if ((sample == NULL) || (s_queue_count == 0U))
    {
        return false;
    }
    *sample = s_queue[s_queue_head];
    s_queue_head = (uint8_t)((s_queue_head + 1U) % TEST_P7_QUEUE_CAPACITY);
    --s_queue_count;
    return true;
}

EventBits_t TestP7_EventBits(void)
{
    return s_event_group_object.bits;
}

bool TestP7_QueueOpsProtected(void)
{
    return s_queue_ops_protected;
}

bool TestP7_MutexAvailable(void)
{
    return s_mutex_available;
}

uint32_t TestP7_SchedulerSuspendCount(void)
{
    return s_scheduler_suspend_count;
}

uint32_t TestP7_SchedulerResumeCount(void)
{
    return s_scheduler_resume_count;
}

bool TestP7_SchedulerProtectionBalanced(void)
{
    return !s_scheduler_suspended &&
           (s_scheduler_suspend_count == s_scheduler_resume_count);
}

static bool TestP7_RunProtectTaskScenario(bool seed_semaphore,
                                          bool active_once,
                                          uint8_t expected_alert_takes)
{
    int escape_reason;

    s_alert_sem_available = seed_semaphore;
    s_alert_active = active_once;
    s_alert_active_once = active_once;
    s_alert_take_count = 0U;
    s_task_delay_count = 0U;
    s_last_task_delay = 0U;
    s_task_guard_failed = false;
    s_task_escape_armed = true;
    escape_reason = setjmp(s_task_escape);
    if (escape_reason == 0)
    {
        /* 正式 task 完成 retry/drain 并回到下一次 blocking ALERT wait 时，fake semaphore 退出。 */
        APL_TaskProtect(NULL);
        s_task_guard_failed = true;
    }
    s_task_escape_armed = false;
    return (escape_reason == 1) && !s_task_guard_failed &&
           (s_alert_take_count == expected_alert_takes) &&
           (s_exti_init_count == 1U);
}

bool TestP7_RunProtectTaskRetryScenario(void)
{
    return TestP7_RunProtectTaskScenario(true, false, 2U);
}

bool TestP7_RunProtectTaskAlreadyHighScenario(void)
{
    return TestP7_RunProtectTaskScenario(false, true, 1U);
}

uint8_t TestP7_TaskDelayCount(void)
{
    return s_task_delay_count;
}

TickType_t TestP7_LastTaskDelay(void)
{
    return s_last_task_delay;
}

uint8_t TestP7_ExtiInitCount(void)
{
    return s_exti_init_count;
}

bool TestP7_ExerciseAlertIsr(void)
{
    s_exti_pending = true;
    s_exti_clear_count = 0U;
    s_isr_give_count = 0U;
    s_alert_sem_available = false;
    EXTI1_IRQHandler();
    return !s_exti_pending && (s_exti_clear_count == 1U) &&
           (s_isr_give_count == 1U) && s_alert_sem_available;
}

BQ76940_t *APL_SystemAfeDevice(void)
{
    return &s_device;
}

uint32_t APL_TimeMs(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

bool TestP7_PushCcSample(int16_t raw)
{
    BMS_CcSample_t sample;
    bool inserted;
    bool overflowed;
    bool oldest_was_dropped;

    if (!BMS_Protect_TestStageCcSample(raw, APL_TimeMs()) ||
        !BMS_Protect_GetPendingCcSample(&sample))
    {
        return false;
    }
    inserted = APL_Rtos_TransportCcSample(
        &sample, &overflowed, &oldest_was_dropped);
    return BMS_Protect_CompleteCcTransport(
        sample.transport_id, inserted, overflowed,
        oldest_was_dropped) && inserted;
}

BMS_ProtectDrainResult_t TestP7_ProtectDrain(void)
{
    BMS_ProtectDrainResult_t result;
    BMS_CcSample_t sample;
    bool inserted;
    bool overflowed;
    bool oldest_was_dropped;
    uint8_t attempt;

    for (attempt = 0U; attempt < 4U; ++attempt)
    {
        result = BMS_Protect_Drain(&s_device, APL_TimeMs());
        if (!BMS_Protect_GetPendingCcSample(&sample))
        {
            return result;
        }
        inserted = APL_Rtos_TransportCcSample(
            &sample, &overflowed, &oldest_was_dropped);
        (void)BMS_Protect_CompleteCcTransport(
            sample.transport_id, inserted, overflowed,
            oldest_was_dropped);
    }
    return BMS_PROTECT_DRAIN_RETRY_REQUIRED;
}

void TestP7_SetRecoveryResult(bool result)
{
    s_recovery_result = result;
}

bool TestP7_RecoveryHook(BQ76940_t *device)
{
    ++s_recovery_calls;
    return (device == &s_device) && s_recovery_result;
}

uint8_t TestP7_RecoveryCallCount(void)
{
    return s_recovery_calls;
}

BaseType_t xQueueGenericSend(QueueHandle_t queue,
                             const void *const item,
                             TickType_t ticks_to_wait,
                             const BaseType_t copy_position)
{
    struct QueueDefinition *object;
    uint8_t tail;

    (void)ticks_to_wait;
    (void)copy_position;
    object = (struct QueueDefinition *)queue;
    if (object == NULL)
    {
        return pdFAIL;
    }
    if (object->kind == TEST_P7_KIND_MUTEX)
    {
        s_mutex_available = true;
        return pdPASS;
    }
    if (object->kind == TEST_P7_KIND_SEMAPHORE)
    {
        return pdPASS;
    }
    if ((object->kind != TEST_P7_KIND_QUEUE) || (item == NULL))
    {
        return pdFAIL;
    }
    if (!s_scheduler_suspended)
    {
        s_queue_ops_protected = false;
    }
    if (s_drop_just_occurred)
    {
        s_drop_just_occurred = false;
        if (s_replacement_failures > 0U)
        {
            --s_replacement_failures;
            return pdFAIL;
        }
    }
    if (s_queue_count >= TEST_P7_QUEUE_CAPACITY)
    {
        return errQUEUE_FULL;
    }
    tail = (uint8_t)((s_queue_head + s_queue_count) %
                     TEST_P7_QUEUE_CAPACITY);
    s_queue[tail] = *(const BMS_CcSample_t *)item;
    ++s_queue_count;
    return pdPASS;
}

BaseType_t xQueueReceive(QueueHandle_t queue,
                         void *const item,
                         TickType_t ticks_to_wait)
{
    struct QueueDefinition *object;

    (void)ticks_to_wait;
    object = (struct QueueDefinition *)queue;
    if ((object == NULL) || (object->kind != TEST_P7_KIND_QUEUE) ||
        (item == NULL) || (s_queue_count == 0U))
    {
        return pdFAIL;
    }
    if (!s_scheduler_suspended)
    {
        s_queue_ops_protected = false;
    }
    *(BMS_CcSample_t *)item = s_queue[s_queue_head];
    s_queue_head = (uint8_t)((s_queue_head + 1U) % TEST_P7_QUEUE_CAPACITY);
    --s_queue_count;
    if (s_scheduler_suspended)
    {
        s_drop_just_occurred = true;
    }
    return pdPASS;
}

BaseType_t xQueueSemaphoreTake(QueueHandle_t queue,
                               TickType_t ticks_to_wait)
{
    (void)ticks_to_wait;
    if (queue == &s_alert_sem_object)
    {
        ++s_alert_take_count;
        if (s_alert_sem_available)
        {
            s_alert_sem_available = false;
            return pdTRUE;
        }
        if (s_task_escape_armed)
        {
            longjmp(s_task_escape, 1);
        }
        return pdFALSE;
    }
    if ((queue != &s_mutex_object) || !s_mutex_available)
    {
        return pdFALSE;
    }
    if (s_mutex_failures > 0U)
    {
        --s_mutex_failures;
        return pdFALSE;
    }
    s_mutex_available = false;
    return pdTRUE;
}

BaseType_t xQueueGiveFromISR(QueueHandle_t queue,
                             BaseType_t *higher_priority_task_woken)
{
    if (queue != &s_alert_sem_object)
    {
        return pdFAIL;
    }
    s_alert_sem_available = true;
    ++s_isr_give_count;
    if (higher_priority_task_woken != NULL)
    {
        *higher_priority_task_woken = pdFALSE;
    }
    return pdPASS;
}

void vTaskSuspendAll(void)
{
    ++s_scheduler_suspend_count;
    s_scheduler_suspended = true;
}

BaseType_t xTaskResumeAll(void)
{
    ++s_scheduler_resume_count;
    s_scheduler_suspended = false;
    return pdFALSE;
}

TickType_t xTaskGetTickCount(void)
{
    return s_tick++;
}

void vTaskDelay(const TickType_t ticks_to_delay)
{
    ++s_task_delay_count;
    s_last_task_delay = ticks_to_delay;
    s_tick += ticks_to_delay;
    if (s_task_escape_armed && (s_task_delay_count > 4U))
    {
        s_task_guard_failed = true;
        longjmp(s_task_escape, 2);
    }
}

EventBits_t xEventGroupSetBits(EventGroupHandle_t event_group,
                               const EventBits_t bits_to_set)
{
    struct EventGroupDef_t *object;

    object = (struct EventGroupDef_t *)event_group;
    if (object == NULL)
    {
        return 0U;
    }
    object->bits |= bits_to_set;
    return object->bits;
}

BQ76940_Status_t BQ76940_ReadByte(BQ76940_t *device,
                                  uint8_t register_address,
                                  uint8_t *value)
{
    BQ76940_Status_t status;

    if ((device != &s_device) ||
        (register_address != BQ76940_REG_SYS_STAT) || (value == NULL))
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    if (s_stat_index >= s_stat_count)
    {
        *value = 0U;
        return BQ76940_STATUS_OK;
    }
    status = s_stat_statuses[s_stat_index];
    if (status == BQ76940_STATUS_OK)
    {
        *value = s_stat_values[s_stat_index];
    }
    ++s_stat_index;
    return status;
}

BQ76940_Status_t BQ76940_WriteByte(BQ76940_t *device,
                                   uint8_t register_address,
                                   uint8_t value)
{
    if ((device != &s_device) ||
        (register_address != BQ76940_REG_SYS_STAT))
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    if (s_write_count < TEST_P7_WRITE_CAPACITY)
    {
        s_write_values[s_write_count] = value;
        ++s_write_count;
    }
    if (s_write_failures > 0U)
    {
        --s_write_failures;
        return s_write_failure_status;
    }
    return BQ76940_STATUS_OK;
}

BQ76940_Status_t BQ76940_ReadCcRaw(BQ76940_t *device, int16_t *cc_raw)
{
    BQ76940_Status_t status;

    if ((device != &s_device) || (cc_raw == NULL))
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    if (s_cc_index >= s_cc_count)
    {
        return BQ76940_STATUS_I2C_ERROR;
    }
    status = s_cc_statuses[s_cc_index];
    if (status == BQ76940_STATUS_OK)
    {
        *cc_raw = s_cc_values[s_cc_index];
    }
    ++s_cc_index;
    return status;
}

bool BSP_ALERT_PinActive(void)
{
    bool active;

    active = s_alert_active;
    if (s_alert_active_once)
    {
        s_alert_active = false;
        s_alert_active_once = false;
    }
    return active;
}

bool BSP_ALERT_EXTI_Init(void)
{
    ++s_exti_init_count;
    return true;
}

bool BSP_ALERT_EXTI_IsPending(void)
{
    return s_exti_pending;
}

void BSP_ALERT_EXTI_ClearPending(void)
{
    s_exti_pending = false;
    ++s_exti_clear_count;
}

ITStatus EXTI_GetITStatus(uint32_t exti_line)
{
    return ((exti_line == EXTI_Line1) && s_exti_pending) ? SET : RESET;
}

void EXTI_ClearITPendingBit(uint32_t exti_line)
{
    if (exti_line == EXTI_Line1)
    {
        s_exti_pending = false;
        ++s_exti_clear_count;
    }
}
