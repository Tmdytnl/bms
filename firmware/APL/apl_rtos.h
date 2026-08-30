#ifndef APL_RTOS_H
#define APL_RTOS_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "event_groups.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "bms_protect.h"

/* 冻结的七任务拓扑与优先级：Protect/Sample/State/SOC/Balance/CAN Tx/Rx。 */
#define APL_RTOS_PRIO_PROTECT                   (5)
#define APL_RTOS_PRIO_SAMPLE                    (4)
#define APL_RTOS_PRIO_STATE                     (3)
#define APL_RTOS_PRIO_SOC                       (3)
#define APL_RTOS_PRIO_BALANCE                   (2)
#define APL_RTOS_PRIO_CAN_TX                    (2)
#define APL_RTOS_PRIO_CAN_RX                    (2)

#define APL_RTOS_STACK_PROTECT                  (160)
#define APL_RTOS_STACK_SAMPLE                   (192)
#define APL_RTOS_STACK_STATE                    (384)
#define APL_RTOS_STACK_SOC                      (192)
#define APL_RTOS_STACK_BALANCE                  (256)
#define APL_RTOS_STACK_CAN_TX                   (240)
#define APL_RTOS_STACK_CAN_RX                   (160)

#define APL_RTOS_CAN_TX_QUEUE_DEPTH             (24)
#define APL_RTOS_CAN_RX_QUEUE_DEPTH             (12)
#define APL_RTOS_CC_SAMPLE_QUEUE_DEPTH          (8)

#define EVT_SAMPLE_READY                        ((EventBits_t)(1U << 0))
#define EVT_AFE_ONLINE                          ((EventBits_t)(1U << 1))
#define EVT_FAULT_PRESENT                       ((EventBits_t)(1U << 2))
#define EVT_PARAM_DIRTY                         ((EventBits_t)(1U << 3))
#define EVT_CC_QUEUE_OVERFLOW                   ((EventBits_t)(1U << 4))

/* scheduler 前构造完整 IPC 集；任一对象失败会删除已经创建的对象。 */
BaseType_t APL_Rtos_CreateObjects(void);

/*
 * 创建七个任务并把 composition root 持有的 AFE device 直接交给 ProtectTask。
 * NULL 会 fail-closed，避免任务启动后再通过全局 accessor 反向获取组装内部状态。
 */
BaseType_t APL_Rtos_CreateTasks(BQ76940_t *afe_device);
void APL_Rtos_NotifyStateUrgent(void);
void APL_Rtos_RequestProtectService(void);
uint32_t APL_TimeMs(void);
uint32_t APL_TimeMsFromISR(void);

/* ProtectTask 唯一生产者使用 newest-wins；结果回送 FML 完成两阶段确认。 */
bool APL_Rtos_TransportCcSample(const BMS_CcSample_t *sample,
                                bool *overflowed,
                                bool *oldest_was_dropped);

#endif /* APL_RTOS_H */
