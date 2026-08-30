#ifndef BMS_RUNTIME_PORT_H
#define BMS_RUNTIME_PORT_H

#include <stdbool.h>
#include <stdint.h>

/*
 * FML 只依赖这些与操作系统无关的窄原语。production 实现在 APL，主机/目标
 * 测试可提供等价替身；接口不暴露 OS handle、tick 或调度器类型。
 */
bool BMS_Runtime_BusLock(uint32_t timeout_ms);
void BMS_Runtime_BusUnlock(void);
bool BMS_Runtime_DataLock(void);
void BMS_Runtime_DataUnlock(void);
void BMS_Runtime_CriticalEnter(void);
void BMS_Runtime_CriticalExit(void);

#endif /* BMS_RUNTIME_PORT_H */
