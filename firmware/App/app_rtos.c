#include "app_rtos.h"

#include <stddef.h>

#include "bms_protect.h"   /* Task_Protect entry (Phase 7 implementation) */

/* ------------------------------------------------------------------ */
/* IPC objects (spec §11.2).                                           */
/* ------------------------------------------------------------------ */
SemaphoreHandle_t xI2CMutex;
SemaphoreHandle_t xDataMutex;
SemaphoreHandle_t xAfeAlertSem;
QueueHandle_t xCanTxQueue;
QueueHandle_t xCanRxQueue;
QueueHandle_t xCcSampleQueue;
EventGroupHandle_t xSysEvents;

BaseType_t App_Rtos_CreateObjects(void)
{
    xI2CMutex = xSemaphoreCreateMutex();
    xDataMutex = xSemaphoreCreateMutex();
    xAfeAlertSem = xSemaphoreCreateBinary();
    xCanTxQueue = xQueueCreate(APP_RTOS_CAN_TX_QUEUE_DEPTH,
                               sizeof(BMS_CanFrame_t));
    xCanRxQueue = xQueueCreate(APP_RTOS_CAN_RX_QUEUE_DEPTH,
                               sizeof(BMS_CanFrame_t));
    xCcSampleQueue = xQueueCreate(APP_RTOS_CC_SAMPLE_QUEUE_DEPTH,
                                  sizeof(BMS_CcSample_t));
    xSysEvents = xEventGroupCreate();

    if ((xI2CMutex == NULL) || (xDataMutex == NULL) ||
        (xAfeAlertSem == NULL) || (xCanTxQueue == NULL) ||
        (xCanRxQueue == NULL) || (xCcSampleQueue == NULL) ||
        (xSysEvents == NULL))
    {
        /* No partial object set: delete everything that was created. */
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

/* ------------------------------------------------------------------ */
/* Seven task bodies. Task_Protect is implemented in bms_protect.c and
 * Task_Sample in bms_sample.c. The remaining five are placeholders that
 * keep their specified periods until their later phases. */
/* ------------------------------------------------------------------ */

void Task_State(void *argument)
{
    const TickType_t period = pdMS_TO_TICKS(100U);
    TickType_t last;

    (void)argument;
    last = xTaskGetTickCount();
    for (;;)
    {
        vTaskDelayUntil(&last, period);
        /* Phase 9: state machine / software protection / IWDG feed. */
    }
}

void Task_SOC(void *argument)
{
    const TickType_t period = pdMS_TO_TICKS(1000U);
    TickType_t last;

    (void)argument;
    last = xTaskGetTickCount();
    for (;;)
    {
        vTaskDelayUntil(&last, period);
        /* Phase 10: CC queue consumption + coulomb integration. */
    }
}

void Task_Balance(void *argument)
{
    const TickType_t period = pdMS_TO_TICKS(1000U);
    TickType_t last;

    (void)argument;
    last = xTaskGetTickCount();
    for (;;)
    {
        vTaskDelayUntil(&last, period);
        /* Phase 10: balancing policy + CELLBAL writes. */
    }
}

void Task_CANTx(void *argument)
{
    const TickType_t period = pdMS_TO_TICKS(250U);
    TickType_t last;

    (void)argument;
    last = xTaskGetTickCount();
    for (;;)
    {
        vTaskDelayUntil(&last, period);
        /* Phase 11: drain xCanTxQueue, transmit. */
    }
}

void Task_CANRx(void *argument)
{
    const TickType_t period = pdMS_TO_TICKS(10U);
    TickType_t last;

    (void)argument;
    last = xTaskGetTickCount();
    for (;;)
    {
        vTaskDelayUntil(&last, period);
        /* Phase 11: drain xCanRxQueue, protocol decode. */
    }
}

/* ------------------------------------------------------------------ */
/* Task creation.                                                      */
/* ------------------------------------------------------------------ */
static BaseType_t App_Rtos_CreateOne(TaskFunction_t function,
                                     const char *name,
                                     uint16_t stack_words,
                                     UBaseType_t priority)
{
    TaskHandle_t handle;

    return xTaskCreate(function, name, stack_words, NULL, priority, &handle);
}

BaseType_t App_Rtos_CreateTasks(void)
{
    if (App_Rtos_CreateOne(Task_Protect, "Protect",
                           APP_RTOS_STACK_PROTECT,
                           APP_RTOS_PRIO_PROTECT) != pdPASS)
    {
        return pdFALSE;
    }
    if (App_Rtos_CreateOne(Task_Sample, "Sample",
                           APP_RTOS_STACK_SAMPLE,
                           APP_RTOS_PRIO_SAMPLE) != pdPASS)
    {
        return pdFALSE;
    }
    if (App_Rtos_CreateOne(Task_State, "State",
                           APP_RTOS_STACK_STATE,
                           APP_RTOS_PRIO_STATE) != pdPASS)
    {
        return pdFALSE;
    }
    if (App_Rtos_CreateOne(Task_SOC, "SOC",
                           APP_RTOS_STACK_SOC,
                           APP_RTOS_PRIO_SOC) != pdPASS)
    {
        return pdFALSE;
    }
    if (App_Rtos_CreateOne(Task_Balance, "Balance",
                           APP_RTOS_STACK_BALANCE,
                           APP_RTOS_PRIO_BALANCE) != pdPASS)
    {
        return pdFALSE;
    }
    if (App_Rtos_CreateOne(Task_CANTx, "CANTx",
                           APP_RTOS_STACK_CAN_TX,
                           APP_RTOS_PRIO_CAN_TX) != pdPASS)
    {
        return pdFALSE;
    }
    if (App_Rtos_CreateOne(Task_CANRx, "CANRx",
                           APP_RTOS_STACK_CAN_RX,
                           APP_RTOS_PRIO_CAN_RX) != pdPASS)
    {
        return pdFALSE;
    }
    return pdTRUE;
}
