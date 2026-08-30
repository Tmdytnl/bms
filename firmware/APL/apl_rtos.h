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

/*
 * 七任务固定优先级（errata C-01）：5/4/3/3/2/2/2。
 *
 * Protect 直接承接 AFE ALERT，若延迟会扩大故障响应窗口，因此最高；Sample
 * 负责生成所有安全判断依赖的完整测量，排在其后。State 虽是系统协调核心，
 * 但它消费前两者的结果，不能反过来阻塞 ALERT 排空或采样。SOC 与 State 同级
 * 是为了及时消费 CC 队列，但其算法不拥有 FET；Balance、CAN Tx/Rx 属于慢速
 * 执行或通信服务，优先级较低，避免诊断流量抢占安全数据链。
 */
#define APP_RTOS_PRIO_PROTECT                   (5)
#define APP_RTOS_PRIO_SAMPLE                    (4)
#define APP_RTOS_PRIO_STATE                     (3)
#define APP_RTOS_PRIO_SOC                       (3)
#define APP_RTOS_PRIO_BALANCE                   (2)
#define APP_RTOS_PRIO_CAN_TX                    (2)
#define APP_RTOS_PRIO_CAN_RX                    (2)

/*
 * FreeRTOS 的 xTaskCreate() 以 StackType_t 的元素数接收栈深度；在 Cortex-M3
 * 上一个 word 为 4 B，因此这里的数值不是字节数。State 会在一个周期中串联
 * health、recovery、state、hardware recovery 与 FET service，调用深度最大，
 * 所以它的栈明显大于只做单一队列/周期服务的任务。
 */
#define APP_RTOS_STACK_PROTECT                  (160)
#define APP_RTOS_STACK_SAMPLE                   (192)
/* ARMCC5 callgraph 给出 State 最大 1104 B；384 word 还需覆盖异常上下文与运行余量。 */
#define APP_RTOS_STACK_STATE                    (384)
#define APP_RTOS_STACK_SOC                      (192)
/* ARMCC5 延续构建调用图实测：Balance=840 B，CAN Tx=752 B。 */
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

/*
 * CC 队列元素绑定原始计数、采样时刻与 XREADY generation。SOC 只积分已经由
 * ProtectTask 完成读取和身份确认的样本，不能自行再读 AFE，否则 CC_READY 的
 * W1C ownership、队列 newest-wins 语义和 generation 边界会被拆散。
 */
typedef struct
{
    int16_t raw;                       /* AFE CC 的有符号原始值，正负号保留电流方向。 */
    TickType_t tick;                   /* Protect 接纳该样本时的 RTOS tick。 */
    uint32_t xready_generation;        /* 样本所属 AFE 生命周期，跨代样本禁止连续积分。 */
} BMS_CcSample_t;

/* CAN 队列按值复制完整帧；生产者入队后不得再修改这份消息。 */
typedef struct
{
    /* 字段名为历史遗留；当前协议只允许 0..0x7FF 的 11-bit standard ID。 */
    uint32_t ext_id;
    uint8_t dlc;                       /* 有效载荷长度，接收路径必须先验证不超过 8。 */
    uint8_t data[8];                   /* 线上的显式字节序载荷，不直接映射 C struct。 */
    TickType_t received_tick;          /* ISR 捕获时间，用于服务请求的新鲜度检查。 */
} BMS_CanFrame_t;

/*
 * IPC handle 由启动上下文一次创建，任务只消费，不替换 handle：
 * - xI2CMutex：跨 Sample/Protect/Recovery/Balance 串行化完整 BQ transaction；
 * - xDataMutex：保护 BMS_Data 的整帧发布/快照复制，不能只锁单个字段；
 * - xAfeAlertSem：ISR 只 give，ProtectTask 在任务上下文执行 I2C 与 fault decode；
 * - xCanTxQueue：协议 encoder 生产、CANTx hardware service 唯一消费；
 * - xCanRxQueue：CAN FIFO0 ISR 生产、CANRxTask 唯一消费；
 * - xCcSampleQueue：ProtectTask 生产、SOCTask 唯一消费；
 * - xSysEvents：只传“发生了什么”的通知位，不转移任何业务状态写权限。
 */
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

/*
 * 有界跨任务唤醒；helper 只发通知，不执行 I2C、FET 写入或状态转移。
 * NotifyStateUrgent 缩短 State 的下一次检查延迟；RequestProtectService 让 Protect
 * 复用 ALERT drain 路径。两者都不把调用者提升为目标模块状态的 owner。
 */
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

#endif /* APP_RTOS_H：头文件防重复包含 */
