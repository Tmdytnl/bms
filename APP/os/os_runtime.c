#include "os_runtime.h"

#include "os_objects_internal.h"

/* AFE 访问的跨任务互斥锁；APL 创建，FML 通过 OS_BusLock 获取。 */
OS_Semaphore_t g_os_i2c_mutex;
/* 完整测量快照发布与复制的跨任务互斥锁。 */
OS_Semaphore_t g_os_data_mutex;
/* ALERT ISR 通知 ProtectTask 处理 SYS_STAT 的二值信号量。 */
OS_Semaphore_t g_os_afe_alert_semaphore;
/* CANTxTask 待发送的诊断帧队列。 */
OS_Queue_t g_os_can_tx_queue;
/* CAN RX ISR 向 CANRxTask 交接服务帧的队列。 */
OS_Queue_t g_os_can_rx_queue;
/* ProtectTask 向 SOCTask 交接同代 CC 样本的队列。 */
OS_Queue_t g_os_cc_sample_queue;
/* APL 任务间系统事件位，生命周期覆盖调度器运行。 */
OS_EventGroup_t g_os_system_events;

/*
 * FML 只看见这些窄同步原语，不依赖 FreeRTOS 类型或 object 身份。总线锁与数据锁
 * 不得嵌套：硬件 transaction 完成并释放总线后，才允许提交共享 snapshot，避免
 * Protect/Sample 与数据读者形成锁顺序反转。Critical 仅用于短快照复制/修订号。
 */

/* 在给定毫秒上限内获取 AFE 总线独占权；锁未创建或超时返回 false。 */
bool OS_BusLock(uint32_t timeout_ms)
{
    return (g_os_i2c_mutex != NULL) &&
        (OS_SemaphoreTake(g_os_i2c_mutex, OS_MsToTicks(timeout_ms)) == OS_PASS);
}

/* 释放 AFE 总线独占权；调用方必须已成功取锁。 */
void OS_BusUnlock(void)
{
    if (g_os_i2c_mutex != NULL)
    {
        (void)OS_SemaphoreGive(g_os_i2c_mutex);
    }
}

/* 零等待尝试取得共享测量数据锁。 */
bool OS_DataLock(void)
{
    return (g_os_data_mutex != NULL) &&
        (OS_SemaphoreTake(g_os_data_mutex, (OS_Tick_t)0U) == OS_PASS);
}

/* 释放共享测量数据锁；调用方必须已成功取锁。 */
void OS_DataUnlock(void)
{
    if (g_os_data_mutex != NULL)
    {
        (void)OS_SemaphoreGive(g_os_data_mutex);
    }
}

/* 仅在调度器运行时建立配置更新保护，并返回是否持有新保护。 */
bool OS_ConcurrencyGuardEnter(void)
{
    if (!OS_SchedulerIsRunning())
    {
        return false;
    }
    OS_SchedulerSuspend();
    return true;
}

/* 仅当 Enter 建立保护时结束配置更新保护。 */
void OS_ConcurrencyGuardExit(bool guard_entered)
{
    if (guard_entered)
    {
        (void)OS_SchedulerResume();
    }
}

/* 暂停调度以复制或更新短小共享状态，不包围硬件 I/O。 */
void OS_CriticalEnter(void)
{
    OS_SchedulerSuspend();
}

/* 恢复前述短临界区的任务调度。 */
void OS_CriticalExit(void)
{
    (void)OS_SchedulerResume();
}
