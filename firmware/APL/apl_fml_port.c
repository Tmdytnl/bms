#include "bms_runtime_port.h"

#include "apl_rtos.h"

bool BMS_Runtime_BusLock(uint32_t timeout_ms)
{
    return (xI2CMutex != NULL) &&
        (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE);
}

void BMS_Runtime_BusUnlock(void)
{
    if (xI2CMutex != NULL)
    {
        (void)xSemaphoreGive(xI2CMutex);
    }
}

bool BMS_Runtime_DataLock(void)
{
    return (xDataMutex != NULL) &&
        (xSemaphoreTake(xDataMutex, (TickType_t)0U) == pdTRUE);
}

void BMS_Runtime_DataUnlock(void)
{
    if (xDataMutex != NULL)
    {
        (void)xSemaphoreGive(xDataMutex);
    }
}

void BMS_Runtime_CriticalEnter(void)
{
    vTaskSuspendAll();
}

void BMS_Runtime_CriticalExit(void)
{
    (void)xTaskResumeAll();
}
