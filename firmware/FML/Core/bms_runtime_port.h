#ifndef BMS_RUNTIME_PORT_H
#define BMS_RUNTIME_PORT_H

#include <stdbool.h>
#include <stdint.h>

/*
 * FML 只依赖这些与操作系统无关的窄原语。production 实现在 APL，主机/目标
 * 测试可提供等价替身；接口不暴露 OS handle、tick 或调度器类型。
 * BusLock 与 DataLock 禁止嵌套：先完成 bounded hardware transaction 并释放总线，
 * 再发布/复制共享数据，避免高优先级硬件服务与数据消费者形成锁顺序反转。
 * Critical 只保护短小固定快照或 revision，不得包围 I/O、等待或 Flash 操作。
 */
bool BMS_Runtime_BusLock(uint32_t timeout_ms);
void BMS_Runtime_BusUnlock(void);
bool BMS_Runtime_DataLock(void);
void BMS_Runtime_DataUnlock(void);

/*
 * 配置更新可能发生在 scheduler 启动前、已由调用方建立 exclusion 时，或正常并发
 * 运行期。GuardEnter 只在确有任务并发时建立一层新保护，并返回必须交还给
 * GuardExit 的 ownership token；调用方不得自行猜测是否需要退出。
 */
bool BMS_Runtime_ConcurrencyGuardEnter(void);
void BMS_Runtime_ConcurrencyGuardExit(bool guard_entered);

void BMS_Runtime_CriticalEnter(void);
void BMS_Runtime_CriticalExit(void);

#endif /* BMS_RUNTIME_PORT_H */
