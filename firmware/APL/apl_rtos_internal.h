#ifndef APL_RTOS_INTERNAL_H
#define APL_RTOS_INTERNAL_H

#include "apl_rtos.h"

/*
 * APL 私有 RTOS object registry。
 *
 * 这些 handle 是 FreeRTOS 的组装细节，只允许 APL 的 IPC、IRQ 与 task glue 直接
 * 使用。FML 通过 bms_runtime_port 的窄操作访问锁，其他层不得依赖 object 身份。
 * 测试镜像可包含本头文件以核对冻结的对象拓扑，但 production public API 不再
 * 暴露可被任意 translation unit 操作的 raw handle。
 */
extern SemaphoreHandle_t xI2CMutex; /* AFE 总线的任务互斥对象。 */
extern SemaphoreHandle_t xDataMutex; /* 完整测量快照的任务互斥对象。 */
extern SemaphoreHandle_t xAfeAlertSem; /* ALERT ISR 到 ProtectTask 的通知。 */
extern QueueHandle_t xCanTxQueue; /* 周期诊断帧发送队列。 */
extern QueueHandle_t xCanRxQueue; /* 服务帧接收队列。 */
extern QueueHandle_t xCcSampleQueue; /* Protect 到 SOC 的 CC 样本队列。 */
extern EventGroupHandle_t xSysEvents; /* APL 任务间系统事件位。 */

#endif /* APL_RTOS_INTERNAL_H */
