#ifndef OS_API_H
#define OS_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 统一的 OS 调用结果；非零表示操作成功。 */
typedef int32_t OS_Result_t;
/* RTOS tick 计数；本项目配置为 32 位 tick。 */
typedef uint32_t OS_Tick_t;
/* 事件组中的位集合。 */
typedef uint32_t OS_EventBits_t;
/* 任务入口函数的签名。 */
typedef void (*OS_TaskFunction_t)(void *argument);
/* 互斥锁或信号量的不透明句柄。 */
typedef void *OS_Semaphore_t;
/* 队列的不透明句柄。 */
typedef void *OS_Queue_t;
/* 事件组的不透明句柄。 */
typedef void *OS_EventGroup_t;
/* 任务的不透明句柄。 */
typedef void *OS_Task_t;

/* 与内核成功返回值一致，供任务创建和队列、信号量操作判断。 */
#define OS_PASS (1)
/* 与内核失败返回值一致；创建对象失败则返回 NULL 句柄。 */
#define OS_FAIL (0)
/* 仅供任务上下文的阻塞调用使用，表示无限等待。 */
#define OS_WAIT_FOREVER (UINT32_MAX)

/* 未标 FromISR 的阻塞和对象操作只在任务上下文调用；所有超时均以 tick 为单位。 */

/* 将毫秒转换为内核 tick。 */
OS_Tick_t OS_MsToTicks(uint32_t milliseconds);
/* 将内核 tick 转换为毫秒。 */
uint32_t OS_TicksToMs(OS_Tick_t ticks);
/* 读取任务上下文中的当前 tick。 */
OS_Tick_t OS_TickCount(void);
/* 读取中断上下文中的当前 tick。 */
OS_Tick_t OS_TickCountFromISR(void);
/* 将任务推迟给定 tick 数。 */
void OS_Delay(OS_Tick_t ticks);
/* 按固定节拍唤醒任务并更新上次唤醒时间。 */
void OS_DelayUntil(OS_Tick_t *previous_wake, OS_Tick_t period);
/* 暂停任务调度以保护短暂的共享内存操作。 */
void OS_SchedulerSuspend(void);
/* 恢复先前暂停的任务调度。 */
void OS_SchedulerResume(void);
/* 判断任务调度器是否正在运行。 */
bool OS_SchedulerIsRunning(void);
/* 启动任务调度器；正常运行后不返回。 */
void OS_StartScheduler(void);
/* 根据 ISR 唤醒结果请求退出中断后的任务切换。 */
void OS_YieldFromISR(OS_Result_t higher_priority_task_woken);
/* 关闭可抢占当前任务的中断以复制 ISR 共享计数。 */
void OS_InterruptCriticalEnter(void);
/* 退出与 ISR 共享数据的短临界区。 */
void OS_InterruptCriticalExit(void);

/*
 * 创建任务；stack_words 是栈字数而非字节数。成功返回 OS_PASS，失败返回
 * OS_FAIL。created_task 可为 NULL；非 NULL 时失败会写入 NULL，成功写入新句柄。
 */
OS_Result_t OS_TaskCreate(OS_TaskFunction_t function, const char *name,
                          uint16_t stack_words, void *argument,
                          uint32_t priority, OS_Task_t *created_task);
/* 通知指定任务处理待决工作。 */
void OS_TaskNotifyGive(OS_Task_t task);
/* 等待当前任务通知，timeout 单位 tick；超时返回 0，否则返回取得的通知次数。 */
uint32_t OS_TaskNotifyTake(bool clear_on_exit, OS_Tick_t timeout);

/* 在内核堆创建互斥锁；分配失败返回 NULL。 */
OS_Semaphore_t OS_MutexCreate(void);
/* 在内核堆创建初始为空的二值信号量；分配失败返回 NULL。 */
OS_Semaphore_t OS_BinarySemaphoreCreate(void);
/* 删除互斥锁或信号量。 */
void OS_SemaphoreDelete(OS_Semaphore_t semaphore);
/* 在任务上下文等待信号量，timeout 单位 tick；成功返回 OS_PASS，超时返回 OS_FAIL。 */
OS_Result_t OS_SemaphoreTake(OS_Semaphore_t semaphore, OS_Tick_t timeout);
/* 在任务上下文释放信号量；成功返回 OS_PASS，状态不允许释放时返回 OS_FAIL。 */
OS_Result_t OS_SemaphoreGive(OS_Semaphore_t semaphore);
/*
 * 从 ISR 释放信号量；返回 OS_PASS/OS_FAIL。唤醒标志可为 NULL；非 NULL 时
 * 累计本次 ISR 是否唤醒更高优先级任务，退出中断前交给 OS_YieldFromISR。
 */
OS_Result_t OS_SemaphoreGiveFromISR(OS_Semaphore_t semaphore,
                                    OS_Result_t *higher_priority_task_woken);

/* 创建 depth 项、每项 item_bytes 字节的队列；内核堆不足时返回 NULL。 */
OS_Queue_t OS_QueueCreate(uint32_t depth, uint32_t item_bytes);
/* 删除队列。 */
void OS_QueueDelete(OS_Queue_t queue);
/* 复制一个元素到队尾；timeout 单位 tick，满且超时返回 OS_FAIL，队列保持原状。 */
OS_Result_t OS_QueueSend(OS_Queue_t queue, const void *item, OS_Tick_t timeout);
/* 复制一个元素到队首；timeout 单位 tick，满且超时返回 OS_FAIL，队列保持原状。 */
OS_Result_t OS_QueueSendToFront(OS_Queue_t queue, const void *item,
                                 OS_Tick_t timeout);
/* 取出队首元素并复制到 item；timeout 单位 tick，空且超时返回 OS_FAIL。 */
OS_Result_t OS_QueueReceive(OS_Queue_t queue, void *item, OS_Tick_t timeout);
/*
 * 从 ISR 复制元素到队尾；满时返回 OS_FAIL。唤醒标志可为 NULL；非 NULL 时
 * 累计本次 ISR 是否唤醒更高优先级任务，供 OS_YieldFromISR 使用。
 */
OS_Result_t OS_QueueSendFromISR(OS_Queue_t queue, const void *item,
                                 OS_Result_t *higher_priority_task_woken);

/* 在内核堆创建事件组；分配失败返回 NULL。 */
OS_EventGroup_t OS_EventGroupCreate(void);
/* 删除事件组。 */
void OS_EventGroupDelete(OS_EventGroup_t events);
/* 设置事件位并返回更新后的位集合。 */
OS_EventBits_t OS_EventGroupSetBits(OS_EventGroup_t events, OS_EventBits_t bits);
/* 清除事件位并返回清除前的位集合。 */
OS_EventBits_t OS_EventGroupClearBits(OS_EventGroup_t events, OS_EventBits_t bits);

/* 读取当前可用的 FreeRTOS 堆字节数。 */
size_t OS_HeapFreeBytes(void);
/* 读取历史最低可用的 FreeRTOS 堆字节数。 */
size_t OS_HeapMinimumFreeBytes(void);

#endif /* OS_API_H */
