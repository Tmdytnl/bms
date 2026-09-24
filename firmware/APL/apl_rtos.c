#include "apl_rtos_internal.h"

#include <stddef.h>

#include "Task/apl_tasks.h"
#include "bms_can.h"

/* AFE 访问的跨任务互斥锁；APL 创建，FML 仅经 runtime port 获取。 */
SemaphoreHandle_t xI2CMutex;
/* 完整测量快照发布与复制的跨任务互斥锁。 */
SemaphoreHandle_t xDataMutex;
/* ALERT ISR 通知 ProtectTask 处理 SYS_STAT 的二值信号量。 */
SemaphoreHandle_t xAfeAlertSem;
/* CANTxTask 待发送的诊断帧队列。 */
QueueHandle_t xCanTxQueue;
/* CAN RX ISR 向 CANRxTask 交接服务帧的队列。 */
QueueHandle_t xCanRxQueue;
/* ProtectTask 向 SOCTask 交接同代 CC 样本的队列。 */
QueueHandle_t xCcSampleQueue;
/* APL 任务间系统事件位，生命周期覆盖调度器运行。 */
EventGroupHandle_t xSysEvents;
/* 仅供 APL 向 StateTask 发紧急唤醒的私有 task handle。 */
static TaskHandle_t s_state_task_handle;

/* 创建七任务需要的队列、锁、信号量和事件组；失败时清理已建对象。 */
BaseType_t APL_Rtos_CreateObjects(void)
{
    xI2CMutex = xSemaphoreCreateMutex();
    xDataMutex = xSemaphoreCreateMutex();
    xAfeAlertSem = xSemaphoreCreateBinary();
    xCanTxQueue = xQueueCreate(APL_RTOS_CAN_TX_QUEUE_DEPTH,
                               sizeof(BMS_CanFrame_t));
    xCanRxQueue = xQueueCreate(APL_RTOS_CAN_RX_QUEUE_DEPTH,
                               sizeof(BMS_CanFrame_t));
    xCcSampleQueue = xQueueCreate(APL_RTOS_CC_SAMPLE_QUEUE_DEPTH,
                                  sizeof(BMS_CcSample_t));
    xSysEvents = xEventGroupCreate();

    if ((xI2CMutex == NULL) || (xDataMutex == NULL) ||
        (xAfeAlertSem == NULL) || (xCanTxQueue == NULL) ||
        (xCanRxQueue == NULL) || (xCcSampleQueue == NULL) ||
        (xSysEvents == NULL))
    {
        if (xI2CMutex != NULL)
        {
            vSemaphoreDelete(xI2CMutex);
            xI2CMutex = NULL;
        }
        if (xDataMutex != NULL)
        {
            vSemaphoreDelete(xDataMutex);
            xDataMutex = NULL;
        }
        if (xAfeAlertSem != NULL)
        {
            vSemaphoreDelete(xAfeAlertSem);
            xAfeAlertSem = NULL;
        }
        if (xCanTxQueue != NULL)
        {
            vQueueDelete(xCanTxQueue);
            xCanTxQueue = NULL;
        }
        if (xCanRxQueue != NULL)
        {
            vQueueDelete(xCanRxQueue);
            xCanRxQueue = NULL;
        }
        if (xCcSampleQueue != NULL)
        {
            vQueueDelete(xCcSampleQueue);
            xCcSampleQueue = NULL;
        }
        if (xSysEvents != NULL)
        {
            vEventGroupDelete(xSysEvents);
            xSysEvents = NULL;
        }
        return pdFALSE;
    }
    return pdTRUE;
}

/* 按冻结的名称、栈与优先级创建一个任务；失败交由上层清理已建对象。 */
static BaseType_t APL_Rtos_CreateOne(TaskFunction_t function,
                                     const char *name,
                                     uint16_t stack_words,
                                     void *argument,
                                     UBaseType_t priority,
                                     TaskHandle_t *created_handle)
{
    TaskHandle_t handle;
    BaseType_t result;

    result = xTaskCreate(function, name, stack_words, argument,
                         priority, &handle);
    if ((result == pdPASS) && (created_handle != NULL))
    {
        *created_handle = handle;
    }
    return result;
}

/* 按冻结优先级创建七个任务，并把 AFE 依赖直接交给 ProtectTask。 */
BaseType_t APL_Rtos_CreateTasks(BQ76940_t *afe_device)
{
    if ((afe_device == NULL) ||
        (APL_Rtos_CreateOne(APL_TaskProtect, "Protect",
                           APL_RTOS_STACK_PROTECT,
                           afe_device, APL_RTOS_PRIO_PROTECT,
                           NULL) != pdPASS) ||
        APL_Rtos_CreateOne(APL_TaskSample, "Sample",
                           APL_RTOS_STACK_SAMPLE,
                           NULL, APL_RTOS_PRIO_SAMPLE, NULL) != pdPASS ||
        APL_Rtos_CreateOne(APL_TaskState, "State",
                           APL_RTOS_STACK_STATE,
                           NULL, APL_RTOS_PRIO_STATE,
                           &s_state_task_handle) != pdPASS ||
        APL_Rtos_CreateOne(APL_TaskSoc, "SOC",
                           APL_RTOS_STACK_SOC,
                           NULL, APL_RTOS_PRIO_SOC, NULL) != pdPASS ||
        APL_Rtos_CreateOne(APL_TaskBalance, "Balance",
                           APL_RTOS_STACK_BALANCE,
                           NULL, APL_RTOS_PRIO_BALANCE, NULL) != pdPASS ||
        APL_Rtos_CreateOne(APL_TaskCanTx, "CANTx",
                           APL_RTOS_STACK_CAN_TX,
                           NULL, APL_RTOS_PRIO_CAN_TX, NULL) != pdPASS ||
        APL_Rtos_CreateOne(APL_TaskCanRx, "CANRx",
                           APL_RTOS_STACK_CAN_RX,
                           NULL, APL_RTOS_PRIO_CAN_RX, NULL) != pdPASS)
    {
        return pdFALSE;
    }
    return pdTRUE;
}

/* 唤醒 StateTask 尽快处理新增安全状态。 */
void APL_Rtos_NotifyStateUrgent(void)
{
    if (s_state_task_handle != NULL)
    {
        xTaskNotifyGive(s_state_task_handle);
    }
}

/* 通过任务通知要求 ProtectTask 尽快处理待决硬件事件。 */
void APL_Rtos_RequestProtectService(void)
{
    if (xAfeAlertSem != NULL)
    {
        (void)xSemaphoreGive(xAfeAlertSem);
    }
}

/* 把当前 RTOS tick 转换为领域模块使用的毫秒时间。 */
uint32_t APL_TimeMs(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/* 在 ISR 上下文读取 tick 并转换为毫秒时间。 */
uint32_t APL_TimeMsFromISR(void)
{
    return (uint32_t)(xTaskGetTickCountFromISR() * portTICK_PERIOD_MS);
}
