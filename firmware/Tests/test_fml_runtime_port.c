#include "bms_runtime_port.h"

#include "apl_rtos_internal.h"

/* FML production code is RTOS-free; test images bind the same narrow port to their stubs. */
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

bool BMS_Runtime_ConcurrencyGuardEnter(void)
{
#if defined(TEST_PHASE8_SAMPLE_IMAGE)
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)
    {
        return false;
    }
#endif
    vTaskSuspendAll();
    return true;
}

void BMS_Runtime_ConcurrencyGuardExit(bool guard_entered)
{
    if (guard_entered)
    {
        (void)xTaskResumeAll();
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
