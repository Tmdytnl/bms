#include "bms_runtime_port.h"

#include "apl_rtos_internal.h"

/*
 * FML 只看见这些窄同步原语，不依赖 FreeRTOS 类型或 object 身份。总线锁与数据锁
 * 不得嵌套：硬件 transaction 完成并释放总线后，才允许提交共享 snapshot，避免
 * Protect/Sample 与数据读者形成锁顺序反转。Critical 仅用于短快照复制/修订号。
 */

/* 在给定毫秒上限内获取 AFE 总线独占权；锁未创建或超时返回 false。 */
bool BMS_Runtime_BusLock(uint32_t timeout_ms)
{
    return (xI2CMutex != NULL) &&
        (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE);
}

/* 释放 AFE 总线独占权；调用方必须已成功取锁。 */
void BMS_Runtime_BusUnlock(void)
{
    if (xI2CMutex != NULL)
    {
        (void)xSemaphoreGive(xI2CMutex);
    }
}

/* 零等待尝试取得共享测量数据锁。 */
bool BMS_Runtime_DataLock(void)
{
    return (xDataMutex != NULL) &&
        (xSemaphoreTake(xDataMutex, (TickType_t)0U) == pdTRUE);
}

/* 释放共享测量数据锁；调用方必须已成功取锁。 */
void BMS_Runtime_DataUnlock(void)
{
    if (xDataMutex != NULL)
    {
        (void)xSemaphoreGive(xDataMutex);
    }
}

/* 仅在调度器运行时建立配置更新保护，并返回是否持有新保护。 */
bool BMS_Runtime_ConcurrencyGuardEnter(void)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)
    {
        return false;
    }
    vTaskSuspendAll();
    return true;
}

/* 仅当 Enter 建立保护时结束配置更新保护。 */
void BMS_Runtime_ConcurrencyGuardExit(bool guard_entered)
{
    if (guard_entered)
    {
        (void)xTaskResumeAll();
    }
}

/* 暂停调度以复制或更新短小共享状态，不包围硬件 I/O。 */
void BMS_Runtime_CriticalEnter(void)
{
    vTaskSuspendAll();
}

/* 恢复前述短临界区的任务调度。 */
void BMS_Runtime_CriticalExit(void)
{
    (void)xTaskResumeAll();
}
