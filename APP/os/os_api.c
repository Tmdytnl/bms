#include "os_api.h"

#include "FreeRTOS.h"
#include "event_groups.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

/* 将毫秒转换为内核 tick，沿用 FreeRTOS 的舍入规则。 */
OS_Tick_t OS_MsToTicks(uint32_t milliseconds)
{
    return (OS_Tick_t)pdMS_TO_TICKS(milliseconds);
}

/* 将 tick 换算为领域时间使用的毫秒。 */
uint32_t OS_TicksToMs(OS_Tick_t ticks)
{
    return (uint32_t)(ticks * portTICK_PERIOD_MS);
}

/* 读取任务上下文中的 tick。 */
OS_Tick_t OS_TickCount(void)
{
    return (OS_Tick_t)xTaskGetTickCount();
}

/* 读取 ISR 上下文中的 tick。 */
OS_Tick_t OS_TickCountFromISR(void)
{
    return (OS_Tick_t)xTaskGetTickCountFromISR();
}

/* 推迟当前任务的执行。 */
void OS_Delay(OS_Tick_t ticks)
{
    vTaskDelay((TickType_t)ticks);
}

/* 按固定周期唤醒当前任务。 */
void OS_DelayUntil(OS_Tick_t *previous_wake, OS_Tick_t period)
{
    vTaskDelayUntil((TickType_t *)previous_wake, (TickType_t)period);
}

/* 暂停调度器，供短临界区使用。 */
void OS_SchedulerSuspend(void)
{
    vTaskSuspendAll();
}

/* 恢复前述暂停的调度器。 */
void OS_SchedulerResume(void)
{
    (void)xTaskResumeAll();
}

/* 查询调度器是否处于运行状态。 */
bool OS_SchedulerIsRunning(void)
{
    return xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

/* 启动 FreeRTOS 调度器。 */
void OS_StartScheduler(void)
{
    vTaskStartScheduler();
}

/* 根据 ISR 唤醒结果申请调度。 */
void OS_YieldFromISR(OS_Result_t higher_priority_task_woken)
{
    portYIELD_FROM_ISR((BaseType_t)higher_priority_task_woken);
}

/* 进入任务与 ISR 共享数据的短临界区。 */
void OS_InterruptCriticalEnter(void)
{
    taskENTER_CRITICAL();
}

/* 退出任务与 ISR 共享数据的短临界区。 */
void OS_InterruptCriticalExit(void)
{
    taskEXIT_CRITICAL();
}

/* 创建内核任务，并隐藏原生任务句柄。 */
OS_Result_t OS_TaskCreate(OS_TaskFunction_t function, const char *name,
                          uint16_t stack_words, void *argument,
                          uint32_t priority, OS_Task_t *created_task)
{
    /* 当前创建或操作的 OS 对象句柄。 */
    TaskHandle_t handle = NULL;
    /* 内核任务创建接口返回的成功或失败状态。 */
    BaseType_t result = xTaskCreate(function, name, stack_words, argument,
                                    (UBaseType_t)priority, &handle);
    if (created_task != NULL)
    {
        *created_task = handle;
    }
    return (OS_Result_t)result;
}

/* 通知目标任务。 */
void OS_TaskNotifyGive(OS_Task_t task)
{
    xTaskNotifyGive((TaskHandle_t)task);
}

/* 等待当前任务的通知并返回累计数量。 */
uint32_t OS_TaskNotifyTake(bool clear_on_exit, OS_Tick_t timeout)
{
    return (uint32_t)ulTaskNotifyTake(clear_on_exit ? pdTRUE : pdFALSE,
                                      (TickType_t)timeout);
}

/* 创建互斥锁。 */
OS_Semaphore_t OS_MutexCreate(void)
{
    return xSemaphoreCreateMutex();
}

/* 创建二值信号量。 */
OS_Semaphore_t OS_BinarySemaphoreCreate(void)
{
    return xSemaphoreCreateBinary();
}

/* 删除同步对象。 */
void OS_SemaphoreDelete(OS_Semaphore_t semaphore)
{
    vSemaphoreDelete((SemaphoreHandle_t)semaphore);
}

/* 从任务上下文获取同步对象。 */
OS_Result_t OS_SemaphoreTake(OS_Semaphore_t semaphore, OS_Tick_t timeout)
{
    return (OS_Result_t)xSemaphoreTake((SemaphoreHandle_t)semaphore,
                                       (TickType_t)timeout);
}

/* 从任务上下文释放同步对象。 */
OS_Result_t OS_SemaphoreGive(OS_Semaphore_t semaphore)
{
    return (OS_Result_t)xSemaphoreGive((SemaphoreHandle_t)semaphore);
}

/* 从 ISR 释放同步对象，并透传任务唤醒结果。 */
OS_Result_t OS_SemaphoreGiveFromISR(OS_Semaphore_t semaphore,
                                    OS_Result_t *higher_priority_task_woken)
{
    /* 记录本次 ISR 操作是否唤醒了更高优先级任务。 */
    BaseType_t woken = (higher_priority_task_woken != NULL &&
                        *higher_priority_task_woken != OS_FAIL) ? pdTRUE : pdFALSE;
    /* 信号量释放操作的内核返回状态。 */
    BaseType_t result = xSemaphoreGiveFromISR((SemaphoreHandle_t)semaphore,
                                               &woken);
    if (higher_priority_task_woken != NULL)
    {
        *higher_priority_task_woken = (OS_Result_t)woken;
    }
    return (OS_Result_t)result;
}

/* 创建固定大小的队列。 */
OS_Queue_t OS_QueueCreate(uint32_t depth, uint32_t item_bytes)
{
    return xQueueCreate((UBaseType_t)depth, (UBaseType_t)item_bytes);
}

/* 删除队列。 */
void OS_QueueDelete(OS_Queue_t queue)
{
    vQueueDelete((QueueHandle_t)queue);
}

/* 把元素送入队列尾部。 */
OS_Result_t OS_QueueSend(OS_Queue_t queue, const void *item, OS_Tick_t timeout)
{
    return (OS_Result_t)xQueueSend((QueueHandle_t)queue, item,
                                   (TickType_t)timeout);
}

/* 把元素送入队列头部。 */
OS_Result_t OS_QueueSendToFront(OS_Queue_t queue, const void *item,
                                 OS_Tick_t timeout)
{
    return (OS_Result_t)xQueueSendToFront((QueueHandle_t)queue, item,
                                          (TickType_t)timeout);
}

/* 从队列接收一个元素。 */
OS_Result_t OS_QueueReceive(OS_Queue_t queue, void *item, OS_Tick_t timeout)
{
    return (OS_Result_t)xQueueReceive((QueueHandle_t)queue, item,
                                      (TickType_t)timeout);
}

/* 从 ISR 把元素送入队列并报告任务唤醒结果。 */
OS_Result_t OS_QueueSendFromISR(OS_Queue_t queue, const void *item,
                                 OS_Result_t *higher_priority_task_woken)
{
    /* 记录队列发送是否唤醒了更高优先级任务。 */
    BaseType_t woken = (higher_priority_task_woken != NULL &&
                        *higher_priority_task_woken != OS_FAIL) ? pdTRUE : pdFALSE;
    /* ISR 队列发送的内核返回状态。 */
    BaseType_t result = xQueueSendFromISR((QueueHandle_t)queue, item, &woken);
    if (higher_priority_task_woken != NULL)
    {
        *higher_priority_task_woken = (OS_Result_t)woken;
    }
    return (OS_Result_t)result;
}

/* 创建事件组。 */
OS_EventGroup_t OS_EventGroupCreate(void)
{
    return xEventGroupCreate();
}

/* 删除事件组。 */
void OS_EventGroupDelete(OS_EventGroup_t events)
{
    vEventGroupDelete((EventGroupHandle_t)events);
}

/* 设置事件位。 */
OS_EventBits_t OS_EventGroupSetBits(OS_EventGroup_t events, OS_EventBits_t bits)
{
    return (OS_EventBits_t)xEventGroupSetBits((EventGroupHandle_t)events,
                                              (EventBits_t)bits);
}

/* 清除事件位并返回原来的位集合。 */
OS_EventBits_t OS_EventGroupClearBits(OS_EventGroup_t events, OS_EventBits_t bits)
{
    return (OS_EventBits_t)xEventGroupClearBits((EventGroupHandle_t)events,
                                                (EventBits_t)bits);
}

/* 读取当前可用的内核堆容量。 */
size_t OS_HeapFreeBytes(void)
{
    return xPortGetFreeHeapSize();
}

/* 读取历史最低可用的内核堆容量。 */
size_t OS_HeapMinimumFreeBytes(void)
{
    return xPortGetMinimumEverFreeHeapSize();
}
