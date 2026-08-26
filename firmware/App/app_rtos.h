#ifndef APP_RTOS_H
#define APP_RTOS_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "event_groups.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

/*
 * BMS V1 的 FreeRTOS 集成边界。
 *
 * 本模块创建七个应用任务和共享 IPC。Protect、Sample 的入口留在各自 owner
 * 模块，State、SOC、Balance、CAN Tx、CAN Rx 的调度上下文集中在 app_rtos.c。
 * 这种划分只负责“谁被调度”，不会把各模块的状态写权限汇总到本文件。
 */

/* 七任务固定优先级（errata C-01）：5/4/3/3/2/2/2。 */
#define APP_RTOS_PRIO_PROTECT                   (5)
#define APP_RTOS_PRIO_SAMPLE                    (4)
#define APP_RTOS_PRIO_STATE                     (3)
#define APP_RTOS_PRIO_SOC                       (3)
#define APP_RTOS_PRIO_BALANCE                   (2)
#define APP_RTOS_PRIO_CAN_TX                    (2)
#define APP_RTOS_PRIO_CAN_RX                    (2)

/* 任务栈单位为 word（spec §11.4），不是 byte。 */
#define APP_RTOS_STACK_PROTECT                  (160)
#define APP_RTOS_STACK_SAMPLE                   (192)
/* ARMCC5 callgraph 给出 State 最大 1104 B；384 word 还需覆盖异常上下文与运行余量。 */
#define APP_RTOS_STACK_STATE                    (384)
#define APP_RTOS_STACK_SOC                      (192)
/* ARMCC5 continuation callgraph：Balance=840 B，CAN Tx=752 B。 */
#define APP_RTOS_STACK_BALANCE                  (256)
#define APP_RTOS_STACK_CAN_TX                   (240)
#define APP_RTOS_STACK_CAN_RX                   (160)

/* 队列深度是并发契约的一部分，不可把积压无限转移到 heap。 */
#define APP_RTOS_CAN_TX_QUEUE_DEPTH             (24)
#define APP_RTOS_CAN_RX_QUEUE_DEPTH             (12)
#define APP_RTOS_CC_SAMPLE_QUEUE_DEPTH          (8)

/* 系统事件位只表达通知，不转移对应模块的状态 ownership。 */
#define EVT_SAMPLE_READY                        ((EventBits_t)(1U << 0))
#define EVT_AFE_ONLINE                          ((EventBits_t)(1U << 1))
#define EVT_FAULT_PRESENT                       ((EventBits_t)(1U << 2))
#define EVT_PARAM_DIRTY                         ((EventBits_t)(1U << 3))
#define EVT_CC_QUEUE_OVERFLOW                   ((EventBits_t)(1U << 4))

/* 队列元素按值复制，生产者发布后不能再修改已入队对象。 */
typedef struct
{
    int16_t raw;
    TickType_t tick;
    uint32_t xready_generation;
} BMS_CcSample_t;

typedef struct
{
    /* 字段名为历史遗留；当前协议只允许 0..0x7FF 的 11-bit standard ID。 */
    uint32_t ext_id;
    uint8_t dlc;
    uint8_t data[8];
    TickType_t received_tick;
} BMS_CanFrame_t;

/* IPC handle 由启动上下文一次创建，任务只消费，不替换 handle。 */
extern SemaphoreHandle_t xI2CMutex;
extern SemaphoreHandle_t xDataMutex;
extern SemaphoreHandle_t xAfeAlertSem;
extern QueueHandle_t xCanTxQueue;
extern QueueHandle_t xCanRxQueue;
extern QueueHandle_t xCcSampleQueue;
extern EventGroupHandle_t xSysEvents;

/*
 * 原子式创建完整 IPC 集合。任一创建失败都会删除已创建对象并返回 pdFALSE，
 * 避免任务在 NULL handle 与有效 handle 混杂的半初始化系统中运行。
 */
BaseType_t App_Rtos_CreateObjects(void);

/*
 * 按固定顺序创建七个应用任务；全部成功才返回 pdTRUE。失败时不再创建后续
 * 任务，已创建任务由启动层的失败策略统一处理。
 */
BaseType_t App_Rtos_CreateTasks(void);

/* 有界跨任务唤醒；helper 只发通知，不执行 I2C、FET 写入或状态转移。 */
void App_Rtos_NotifyStateUrgent(void);
void App_Rtos_RequestProtectService(void);

/*
 * 七个任务入口。Protect/Sample 留在业务 owner；其余入口集中于本模块，
 * 但任务周期、heartbeat 与业务写权限仍由对应策略和 owner contract 约束。
 */
void Task_Sample(void *argument);
void Task_State(void *argument);
void Task_SOC(void *argument);
void Task_Balance(void *argument);
void Task_CANTx(void *argument);
void Task_CANRx(void *argument);

#endif /* APP_RTOS_H：include guard */
