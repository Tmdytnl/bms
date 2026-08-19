#ifndef APP_RTOS_H
#define APP_RTOS_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "event_groups.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

/*
 * BMS V1 RTOS foundation (Phase 6).
 *
 * Owns the seven application tasks and the IPC objects (spec §11.1/§11.2,
 * Software Gate §3.2). Phase 6 provides the task skeletons and object
 * creation; task bodies are filled in by later phases:
 *   - ProtectTask  : Phase 7 (ALERT/SYS_STAT/CC_READY)
 *   - SampleTask   : Phase 8 (measurement publication)
 *   - StateTask    : Phase 9 (state machine / software protection / IWDG)
 *   - SOCTask      : Phase 10 (SOC integration)
 *   - BalanceTask  : Phase 10 (balancing)
 *   - CANTxTask    : Phase 11
 *   - CANRxTask    : Phase 11
 */

/* Fixed seven-task priorities (errata C-01): 5/4/3/3/2/2/2. */
#define APP_RTOS_PRIO_PROTECT                   (5)
#define APP_RTOS_PRIO_SAMPLE                    (4)
#define APP_RTOS_PRIO_STATE                     (3)
#define APP_RTOS_PRIO_SOC                       (3)
#define APP_RTOS_PRIO_BALANCE                   (2)
#define APP_RTOS_PRIO_CAN_TX                    (2)
#define APP_RTOS_PRIO_CAN_RX                    (2)

/* Task stack sizes in words (spec §11.4). */
#define APP_RTOS_STACK_PROTECT                  (160)
#define APP_RTOS_STACK_SAMPLE                   (192)
#define APP_RTOS_STACK_STATE                    (128)
#define APP_RTOS_STACK_SOC                      (192)
#define APP_RTOS_STACK_BALANCE                  (160)
#define APP_RTOS_STACK_CAN_TX                   (160)
#define APP_RTOS_STACK_CAN_RX                   (160)

/* Queue depths (spec §11.3). */
#define APP_RTOS_CAN_TX_QUEUE_DEPTH             (24)
#define APP_RTOS_CAN_RX_QUEUE_DEPTH             (12)
#define APP_RTOS_CC_SAMPLE_QUEUE_DEPTH          (8)

/* System event bits (spec §11.2). */
#define EVT_SAMPLE_READY                        ((EventBits_t)(1U << 0))
#define EVT_AFE_ONLINE                          ((EventBits_t)(1U << 1))
#define EVT_FAULT_PRESENT                       ((EventBits_t)(1U << 2))
#define EVT_PARAM_DIRTY                         ((EventBits_t)(1U << 3))
#define EVT_CC_QUEUE_OVERFLOW                   ((EventBits_t)(1U << 4))

/* Queue element types (spec §11.3). */
typedef struct
{
    int16_t raw;
    TickType_t tick;
} BMS_CcSample_t;

typedef struct
{
    uint32_t ext_id;
    uint8_t dlc;
    uint8_t data[8];
} BMS_CanFrame_t;

/* IPC objects (spec §11.2 / Gate §3.2). */
extern SemaphoreHandle_t xI2CMutex;
extern SemaphoreHandle_t xDataMutex;
extern SemaphoreHandle_t xAfeAlertSem;
extern QueueHandle_t xCanTxQueue;
extern QueueHandle_t xCanRxQueue;
extern QueueHandle_t xCcSampleQueue;
extern EventGroupHandle_t xSysEvents;

/*
 * Create all IPC objects. Returns pdTRUE only if every object was
 * created; on any failure, already-created objects are deleted and
 * pdFALSE is returned (no partial object set).
 */
BaseType_t App_Rtos_CreateObjects(void);

/*
 * Create all seven task skeletons. Returns pdTRUE only if every task
 * was created; on any failure the function stops creating further tasks.
 * (Created tasks are left running; callers check the return value.)
 */
BaseType_t App_Rtos_CreateTasks(void);

/* Task entry points. Task_Protect is declared in bms_protect.h (its
 * implementation moved there in Phase 7); the rest remain skeletons
 * until their phases. */
void Task_Sample(void *argument);
void Task_State(void *argument);
void Task_SOC(void *argument);
void Task_Balance(void *argument);
void Task_CANTx(void *argument);
void Task_CANRx(void *argument);

#endif /* APP_RTOS_H */
