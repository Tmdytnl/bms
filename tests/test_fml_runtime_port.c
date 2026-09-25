#include "os_runtime.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/* 仅测试镜像使用的原生 FreeRTOS 对象替身。 */
extern SemaphoreHandle_t xI2CMutex;
/* 测量快照测试使用的原生互斥对象替身。 */
extern SemaphoreHandle_t xDataMutex;

/* FML production code is RTOS-free; test images bind the same narrow port to their stubs. */
bool OS_BusLock(uint32_t timeout_ms)
{
    return (xI2CMutex != NULL) &&
        (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE);
}

void OS_BusUnlock(void)
{
    if (xI2CMutex != NULL)
    {
        (void)xSemaphoreGive(xI2CMutex);
    }
}

bool OS_DataLock(void)
{
    return (xDataMutex != NULL) &&
        (xSemaphoreTake(xDataMutex, (TickType_t)0U) == pdTRUE);
}

void OS_DataUnlock(void)
{
    if (xDataMutex != NULL)
    {
        (void)xSemaphoreGive(xDataMutex);
    }
}

bool OS_ConcurrencyGuardEnter(void)
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

void OS_ConcurrencyGuardExit(bool guard_entered)
{
    if (guard_entered)
    {
        (void)xTaskResumeAll();
    }
}

void OS_CriticalEnter(void)
{
    vTaskSuspendAll();
}

void OS_CriticalExit(void)
{
    (void)xTaskResumeAll();
}
