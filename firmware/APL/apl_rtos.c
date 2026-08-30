#include "apl_rtos.h"

#include <stddef.h>

#include "Task/apl_tasks.h"

SemaphoreHandle_t xI2CMutex;
SemaphoreHandle_t xDataMutex;
SemaphoreHandle_t xAfeAlertSem;
QueueHandle_t xCanTxQueue;
QueueHandle_t xCanRxQueue;
QueueHandle_t xCcSampleQueue;
EventGroupHandle_t xSysEvents;
static TaskHandle_t s_state_task_handle;

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
        if (xI2CMutex != NULL) { vSemaphoreDelete(xI2CMutex); xI2CMutex = NULL; }
        if (xDataMutex != NULL) { vSemaphoreDelete(xDataMutex); xDataMutex = NULL; }
        if (xAfeAlertSem != NULL) { vSemaphoreDelete(xAfeAlertSem); xAfeAlertSem = NULL; }
        if (xCanTxQueue != NULL) { vQueueDelete(xCanTxQueue); xCanTxQueue = NULL; }
        if (xCanRxQueue != NULL) { vQueueDelete(xCanRxQueue); xCanRxQueue = NULL; }
        if (xCcSampleQueue != NULL) { vQueueDelete(xCcSampleQueue); xCcSampleQueue = NULL; }
        if (xSysEvents != NULL) { vEventGroupDelete(xSysEvents); xSysEvents = NULL; }
        return pdFALSE;
    }
    return pdTRUE;
}

static BaseType_t APL_Rtos_CreateOne(TaskFunction_t function,
                                     const char *name,
                                     uint16_t stack_words,
                                     UBaseType_t priority,
                                     TaskHandle_t *created_handle)
{
    TaskHandle_t handle;
    BaseType_t result;

    result = xTaskCreate(function, name, stack_words, NULL, priority, &handle);
    if ((result == pdPASS) && (created_handle != NULL))
    {
        *created_handle = handle;
    }
    return result;
}

BaseType_t APL_Rtos_CreateTasks(void)
{
    if (APL_Rtos_CreateOne(APL_TaskProtect, "Protect",
                           APL_RTOS_STACK_PROTECT,
                           APL_RTOS_PRIO_PROTECT, NULL) != pdPASS ||
        APL_Rtos_CreateOne(APL_TaskSample, "Sample",
                           APL_RTOS_STACK_SAMPLE,
                           APL_RTOS_PRIO_SAMPLE, NULL) != pdPASS ||
        APL_Rtos_CreateOne(APL_TaskState, "State",
                           APL_RTOS_STACK_STATE,
                           APL_RTOS_PRIO_STATE,
                           &s_state_task_handle) != pdPASS ||
        APL_Rtos_CreateOne(APL_TaskSoc, "SOC",
                           APL_RTOS_STACK_SOC,
                           APL_RTOS_PRIO_SOC, NULL) != pdPASS ||
        APL_Rtos_CreateOne(APL_TaskBalance, "Balance",
                           APL_RTOS_STACK_BALANCE,
                           APL_RTOS_PRIO_BALANCE, NULL) != pdPASS ||
        APL_Rtos_CreateOne(APL_TaskCanTx, "CANTx",
                           APL_RTOS_STACK_CAN_TX,
                           APL_RTOS_PRIO_CAN_TX, NULL) != pdPASS ||
        APL_Rtos_CreateOne(APL_TaskCanRx, "CANRx",
                           APL_RTOS_STACK_CAN_RX,
                           APL_RTOS_PRIO_CAN_RX, NULL) != pdPASS)
    {
        return pdFALSE;
    }
    return pdTRUE;
}

void APL_Rtos_NotifyStateUrgent(void)
{
    if (s_state_task_handle != NULL)
    {
        xTaskNotifyGive(s_state_task_handle);
    }
}

void APL_Rtos_RequestProtectService(void)
{
    if (xAfeAlertSem != NULL)
    {
        (void)xSemaphoreGive(xAfeAlertSem);
    }
}

uint32_t APL_TimeMs(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

uint32_t APL_TimeMsFromISR(void)
{
    return (uint32_t)(xTaskGetTickCountFromISR() * portTICK_PERIOD_MS);
}
