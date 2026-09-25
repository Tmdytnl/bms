#include "os_objects_internal.h"
#include "apl_rtos.h"

#include <stddef.h>

#include "apl_tasks.h"
#include "fml_can.h"

/* 仅供 APL 向 StateTask 发紧急唤醒的私有 task handle。 */
static OS_Task_t s_state_task_handle;

/* 创建七任务需要的队列、锁、信号量和事件组；失败时清理已建对象。 */
OS_Result_t APL_Rtos_CreateObjects(void)
{
    g_os_i2c_mutex = OS_MutexCreate();
    g_os_data_mutex = OS_MutexCreate();
    g_os_afe_alert_semaphore = OS_BinarySemaphoreCreate();
    g_os_can_tx_queue = OS_QueueCreate(APL_RTOS_CAN_TX_QUEUE_DEPTH,
                               sizeof(BMS_CanFrame_t));
    g_os_can_rx_queue = OS_QueueCreate(APL_RTOS_CAN_RX_QUEUE_DEPTH,
                               sizeof(BMS_CanFrame_t));
    g_os_cc_sample_queue = OS_QueueCreate(APL_RTOS_CC_SAMPLE_QUEUE_DEPTH,
                                  sizeof(BMS_CcSample_t));
    g_os_system_events = OS_EventGroupCreate();

    if ((g_os_i2c_mutex == NULL) || (g_os_data_mutex == NULL) ||
        (g_os_afe_alert_semaphore == NULL) || (g_os_can_tx_queue == NULL) ||
        (g_os_can_rx_queue == NULL) || (g_os_cc_sample_queue == NULL) ||
        (g_os_system_events == NULL))
    {
        if (g_os_i2c_mutex != NULL)
        {
            OS_SemaphoreDelete(g_os_i2c_mutex);
            g_os_i2c_mutex = NULL;
        }
        if (g_os_data_mutex != NULL)
        {
            OS_SemaphoreDelete(g_os_data_mutex);
            g_os_data_mutex = NULL;
        }
        if (g_os_afe_alert_semaphore != NULL)
        {
            OS_SemaphoreDelete(g_os_afe_alert_semaphore);
            g_os_afe_alert_semaphore = NULL;
        }
        if (g_os_can_tx_queue != NULL)
        {
            OS_QueueDelete(g_os_can_tx_queue);
            g_os_can_tx_queue = NULL;
        }
        if (g_os_can_rx_queue != NULL)
        {
            OS_QueueDelete(g_os_can_rx_queue);
            g_os_can_rx_queue = NULL;
        }
        if (g_os_cc_sample_queue != NULL)
        {
            OS_QueueDelete(g_os_cc_sample_queue);
            g_os_cc_sample_queue = NULL;
        }
        if (g_os_system_events != NULL)
        {
            OS_EventGroupDelete(g_os_system_events);
            g_os_system_events = NULL;
        }
        return OS_FAIL;
    }
    return OS_PASS;
}

/* 按冻结的名称、栈与优先级创建一个任务；失败交由上层清理已建对象。 */
static OS_Result_t APL_Rtos_CreateOne(OS_TaskFunction_t function,
                                     const char *name,
                                     uint16_t stack_words,
                                     void *argument,
                                     uint32_t priority,
                                     OS_Task_t *created_handle)
{
    /* 当前创建或操作的 OS 对象句柄。 */
    OS_Task_t handle;
    /* 任务创建结果，成功后才发布任务句柄。 */
    OS_Result_t result;

    result = OS_TaskCreate(function, name, stack_words, argument,
                         priority, &handle);
    if ((result == OS_PASS) && (created_handle != NULL))
    {
        *created_handle = handle;
    }
    return result;
}

/* 按冻结优先级创建七个任务，并把 AFE 依赖直接交给 ProtectTask。 */
OS_Result_t APL_Rtos_CreateTasks(BQ76940_t *afe_device)
{
    if ((afe_device == NULL) ||
        (APL_Rtos_CreateOne(APL_TaskProtect, "Protect",
                           APL_RTOS_STACK_PROTECT,
                           afe_device, APL_RTOS_PRIO_PROTECT,
                           NULL) != OS_PASS) ||
        APL_Rtos_CreateOne(APL_TaskSample, "Sample",
                           APL_RTOS_STACK_SAMPLE,
                           NULL, APL_RTOS_PRIO_SAMPLE, NULL) != OS_PASS ||
        APL_Rtos_CreateOne(APL_TaskState, "State",
                           APL_RTOS_STACK_STATE,
                           NULL, APL_RTOS_PRIO_STATE,
                           &s_state_task_handle) != OS_PASS ||
        APL_Rtos_CreateOne(APL_TaskSoc, "SOC",
                           APL_RTOS_STACK_SOC,
                           NULL, APL_RTOS_PRIO_SOC, NULL) != OS_PASS ||
        APL_Rtos_CreateOne(APL_TaskBalance, "Balance",
                           APL_RTOS_STACK_BALANCE,
                           NULL, APL_RTOS_PRIO_BALANCE, NULL) != OS_PASS ||
        APL_Rtos_CreateOne(APL_TaskCanTx, "CANTx",
                           APL_RTOS_STACK_CAN_TX,
                           NULL, APL_RTOS_PRIO_CAN_TX, NULL) != OS_PASS ||
        APL_Rtos_CreateOne(APL_TaskCanRx, "CANRx",
                           APL_RTOS_STACK_CAN_RX,
                           NULL, APL_RTOS_PRIO_CAN_RX, NULL) != OS_PASS)
    {
        return OS_FAIL;
    }
    return OS_PASS;
}

/* 唤醒 StateTask 尽快处理新增安全状态。 */
void APL_Rtos_NotifyStateUrgent(void)
{
    if (s_state_task_handle != NULL)
    {
        OS_TaskNotifyGive(s_state_task_handle);
    }
}

/* 通过任务通知要求 ProtectTask 尽快处理待决硬件事件。 */
void APL_Rtos_RequestProtectService(void)
{
    if (g_os_afe_alert_semaphore != NULL)
    {
        (void)OS_SemaphoreGive(g_os_afe_alert_semaphore);
    }
}

/* 把当前 RTOS tick 转换为领域模块使用的毫秒时间。 */
uint32_t APL_TimeMs(void)
{
    return (uint32_t)(OS_TicksToMs(OS_TickCount()));
}

/* 在 ISR 上下文读取 tick 并转换为毫秒时间。 */
uint32_t APL_TimeMsFromISR(void)
{
    return (uint32_t)(OS_TicksToMs(OS_TickCountFromISR()));
}
