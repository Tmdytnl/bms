#ifndef OS_OBJECTS_INTERNAL_H
#define OS_OBJECTS_INTERNAL_H

#include "os_api.h"

/*
 * 项目同步对象的存储位于 OS；APL 在启动时创建、赋值，失败时销毁。
 *
 * 这些不透明 handle 只允许 OS 与 APL 的 IPC、IRQ 和任务组装直接使用。
 * FML 通过 os_runtime 的窄操作访问锁，其他层不得依赖 object 身份。
 * 测试镜像可包含本头文件以核对冻结的对象拓扑；这是 OS 与 APL 之间的
 * 受限交接接口，不属于对 FML 或 BSP 开放的通用 OS API。
 */
extern OS_Semaphore_t g_os_i2c_mutex; /* AFE 总线的任务互斥对象。 */
extern OS_Semaphore_t g_os_data_mutex; /* 完整测量快照的任务互斥对象。 */
extern OS_Semaphore_t g_os_afe_alert_semaphore; /* ALERT ISR 到 ProtectTask 的通知。 */
extern OS_Queue_t g_os_can_tx_queue; /* 周期诊断帧发送队列。 */
extern OS_Queue_t g_os_can_rx_queue; /* 服务帧接收队列。 */
extern OS_Queue_t g_os_cc_sample_queue; /* Protect 到 SOC 的 CC 样本队列。 */
extern OS_EventGroup_t g_os_system_events; /* APL 任务间系统事件位。 */

#endif /* OS_OBJECTS_INTERNAL_H */
