#include "os_api.h"

#include "FreeRTOS.h"
#include "event_groups.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

/* Phase 7 测试复用原有的 FreeRTOS 行为替身，检查 OS 门面之后的真实 APL 路径。 */
OS_Tick_t OS_MsToTicks(uint32_t milliseconds)
{
    return (OS_Tick_t)pdMS_TO_TICKS(milliseconds);
}

/* 把任务推迟到指定 tick。 */
void OS_Delay(OS_Tick_t ticks)
{
    vTaskDelay((TickType_t)ticks);
}

/* 开始 newest-wins 队列事务的调度器保护。 */
void OS_SchedulerSuspend(void)
{
    vTaskSuspendAll();
}

/* 结束 newest-wins 队列事务的调度器保护。 */
void OS_SchedulerResume(void)
{
    (void)xTaskResumeAll();
}

/* 从 ISR 返回时保留原有的调度申请语义。 */
void OS_YieldFromISR(OS_Result_t higher_priority_task_woken)
{
    portYIELD_FROM_ISR((BaseType_t)higher_priority_task_woken);
}

/* 把 CC 样本送入 fake 队列。 */
OS_Result_t OS_QueueSend(OS_Queue_t queue, const void *item, OS_Tick_t timeout)
{
    return (OS_Result_t)xQueueSend((QueueHandle_t)queue, item,
                                   (TickType_t)timeout);
}

/* 从 fake 队列取出 CC 样本。 */
OS_Result_t OS_QueueReceive(OS_Queue_t queue, void *item, OS_Tick_t timeout)
{
    return (OS_Result_t)xQueueReceive((QueueHandle_t)queue, item,
                                      (TickType_t)timeout);
}

/* 发布 CC 队列溢出事件。 */
OS_EventBits_t OS_EventGroupSetBits(OS_EventGroup_t events, OS_EventBits_t bits)
{
    return (OS_EventBits_t)xEventGroupSetBits((EventGroupHandle_t)events,
                                              (EventBits_t)bits);
}

/* 等待 ALERT fake 信号量。 */
OS_Result_t OS_SemaphoreTake(OS_Semaphore_t semaphore, OS_Tick_t timeout)
{
    return (OS_Result_t)xSemaphoreTake((SemaphoreHandle_t)semaphore,
                                       (TickType_t)timeout);
}

/* 从 ISR 发出 ALERT fake 信号量。 */
OS_Result_t OS_SemaphoreGiveFromISR(OS_Semaphore_t semaphore,
                                    OS_Result_t *higher_priority_task_woken)
{
    BaseType_t woken = pdFALSE;
    BaseType_t result = xSemaphoreGiveFromISR((SemaphoreHandle_t)semaphore,
                                               &woken);
    if (higher_priority_task_woken != NULL)
    {
        *higher_priority_task_woken = (OS_Result_t)woken;
    }
    return (OS_Result_t)result;
}
