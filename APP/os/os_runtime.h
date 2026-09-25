#ifndef OS_RUNTIME_H
#define OS_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

/*
 * FML 只依赖这些与操作系统无关的窄原语。production 实现在 OS，主机/目标
 * 测试可提供等价替身；接口不暴露 OS handle、tick 或调度器类型。
 * BusLock 与 DataLock 禁止嵌套：先完成 bounded hardware transaction 并释放总线，
 * 再发布/复制共享数据，避免高优先级硬件服务与数据消费者形成锁顺序反转。
 * Critical 只保护短小固定快照或 revision，不得包围 I/O、等待或 Flash 操作。
 */
bool OS_BusLock(uint32_t timeout_ms);
/* 释放 AFE 总线独占权；调用方必须已成功取锁。 */
void OS_BusUnlock(void);
/* 零等待尝试取得共享测量数据锁。 */
bool OS_DataLock(void);
/* 释放共享测量数据锁；调用方必须已成功取锁。 */
void OS_DataUnlock(void);

/*
 * 配置更新可能发生在 scheduler 启动前、已由调用方建立 exclusion 时，或正常并发
 * 运行期。GuardEnter 只在确有任务并发时建立一层新保护，并返回必须交还给
 * GuardExit 的 ownership token；调用方不得自行猜测是否需要退出。
 */
bool OS_ConcurrencyGuardEnter(void);
/* 仅当 Enter 建立保护时结束配置更新保护。 */
void OS_ConcurrencyGuardExit(bool guard_entered);

/* 暂停调度以复制或更新短小共享状态，不包围硬件 I/O。 */
void OS_CriticalEnter(void);
/* 恢复前述短临界区的任务调度。 */
void OS_CriticalExit(void);

#endif /* OS_RUNTIME_H */
