#include "app_rtos.h"

#include <stddef.h>

#include "bms_protect.h"
#if !defined(TEST_PHASE6_IMAGE)
#include "bms_balance.h"
#include "bms_can.h"
#include "bms_data.h"
#include "bms_debug.h"
#include "bms_fet_manager.h"
#include "bms_health.h"
#include "bms_hw_recovery.h"
#include "bms_policy.h"
#include "bms_persistence.h"
#include "bms_recovery.h"
#include "bms_soc.h"
#include "bms_state.h"
#include "bsp_iwdg.h"
#endif

/* ------------------------------------------------------------------ */
/*
 * 七个任务共享的 IPC 对象。
 * xI2CMutex 保证软件 I2C transaction 不交叉；xDataMutex 保护一致测量快照；
 * xAfeAlertSem 把 EXTI 的最小事件通知交给 ProtectTask；三个 queue 分别划分
 * CAN Tx、CAN Rx 与 CC sample 的唯一消费者；xSysEvents 只传递系统事件位。
 */
/* ------------------------------------------------------------------ */
SemaphoreHandle_t xI2CMutex;
SemaphoreHandle_t xDataMutex;
SemaphoreHandle_t xAfeAlertSem;
QueueHandle_t xCanTxQueue;
QueueHandle_t xCanRxQueue;
QueueHandle_t xCcSampleQueue;
EventGroupHandle_t xSysEvents;
static TaskHandle_t s_state_task_handle;

BaseType_t App_Rtos_CreateObjects(void)
{
    xI2CMutex = xSemaphoreCreateMutex();
    xDataMutex = xSemaphoreCreateMutex();
    xAfeAlertSem = xSemaphoreCreateBinary();
    xCanTxQueue = xQueueCreate(APP_RTOS_CAN_TX_QUEUE_DEPTH,
                               sizeof(BMS_CanFrame_t));
    xCanRxQueue = xQueueCreate(APP_RTOS_CAN_RX_QUEUE_DEPTH,
                               sizeof(BMS_CanFrame_t));
    xCcSampleQueue = xQueueCreate(APP_RTOS_CC_SAMPLE_QUEUE_DEPTH,
                                  sizeof(BMS_CcSample_t));
    xSysEvents = xEventGroupCreate();

    if ((xI2CMutex == NULL) || (xDataMutex == NULL) ||
        (xAfeAlertSem == NULL) || (xCanTxQueue == NULL) ||
        (xCanRxQueue == NULL) || (xCcSampleQueue == NULL) ||
        (xSysEvents == NULL))
    {
        /* 任一对象创建失败就回收已创建对象，禁止带着半套 IPC 启动调度器。 */
        if (xI2CMutex != NULL) { vSemaphoreDelete(xI2CMutex); xI2CMutex = NULL; }
        if (xDataMutex != NULL) { vSemaphoreDelete(xDataMutex); xDataMutex = NULL; }
        if (xAfeAlertSem != NULL) { vSemaphoreDelete(xAfeAlertSem); xAfeAlertSem = NULL; }
        if (xCanTxQueue != NULL) { vQueueDelete(xCanTxQueue); xCanTxQueue = NULL; }
        if (xCanRxQueue != NULL) { vQueueDelete(xCanRxQueue); xCanRxQueue = NULL; }
        if (xCcSampleQueue != NULL) { vQueueDelete(xCcSampleQueue); xCcSampleQueue = NULL; }
        if (xSysEvents != NULL) { vEventGroupDelete(xSysEvents); xSysEvents = NULL; }
        return pdFALSE;
    }
    return pdTRUE;
}

/* ------------------------------------------------------------------ */
/*
 * 七任务入口。
 * ProtectTask 与 SampleTask 分别由 bms_protect.c、bms_sample.c 实现，
 * 因为 ALERT fault lifecycle 与完整测量发布都必须由各自 owner 封装。
 * 本文件持有其余五个调度上下文，但每个循环仍把业务决策委托给 owner 模块。
 */
/* ------------------------------------------------------------------ */

/*
 * StateTask 是系统协调核心：每 10 ms 汇合 Recovery、State、Protect、
 * measurement 与 health 的同代快照，依次推进恢复、软件保护、硬件恢复
 * handshake 和 FET transaction。它只发布诊断聚合，不把 BMS_Data 当成
 * safety authority；紧急通知只缩短等待，不改变同一循环的所有权顺序。
 */
void Task_State(void *argument)
{
#if defined(TEST_PHASE6_IMAGE)
    (void)argument;
    for (;;)
    {
    }
#else
    const BMS_Policy_t *policy;
    BMS_HealthMonitor_t health_monitor;
    BMS_HealthDecision_t health;
    BMS_HwRecoveryEngine_t hw_recovery;
    BMS_ProtectHwRecoveryRequest_t request;
    BMS_ProtectSafetySnapshot_t protect;
    BMS_RecoverySnapshot_t recovery;
    BMS_StateSafetySnapshot_t state;
    BMS_DataSnapshot_t measurement;
    BMS_FaultSummary_t diagnostic_faults;
    uint32_t now_ms;
    bool iwdg_started;

    (void)argument;
    policy = BMS_Policy_Get();
    now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    BMS_Health_MonitorInit(&health_monitor, now_ms);
    BMS_HwRecovery_Init(&hw_recovery);
    iwdg_started = false;
    for (;;)
    {
        now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        BMS_Health_Heartbeat(BMS_HEALTH_TASK_STATE);
        health = BMS_Health_Evaluate(&health_monitor,
                                     &policy->health, now_ms);

        BMS_Recovery_Service(now_ms);
        recovery = BMS_Recovery_GetSnapshot();
        (void)BMS_State_RunOnce(now_ms,
                                recovery.technical_ready,
                                health.rtos_health_fault,
                                &state);

        protect = BMS_Protect_GetSafetySnapshot();
        if (BMS_Data_GetSnapshot(&measurement, now_ms) &&
            BMS_HwRecovery_Evaluate(&hw_recovery, policy,
                                    &protect, &measurement,
                                    now_ms, &request))
        {
            (void)BMS_Protect_SubmitHwRecoveryRequest(&request);
        }

        BMS_FetManager_Service();

        state = BMS_State_GetSafetySnapshot();
        protect = BMS_Protect_GetSafetySnapshot();
        diagnostic_faults.active =
            state.faults.active | protect.faults.active;
        diagnostic_faults.latched =
            state.faults.latched | protect.faults.latched;
        (void)BMS_Data_PublishStateDiagnostic(state.state,
                                               &diagnostic_faults);

        if (health.feed_allowed)
        {
            if (!iwdg_started)
            {
                iwdg_started = BSP_IWDG_StartNominal(
                    policy->health.iwdg_nominal_timeout_ms);
            }
            else
            {
                /*
                 * 只有所有必需任务的 heartbeat generation 都持续推进时，
                 * health 才允许到达这里。StateTask 是唯一喂狗者，避免某个
                 * 卡死任务被其他正常任务继续喂狗而掩盖。
                 */
                BSP_IWDG_Feed();
            }
        }

        (void)ulTaskNotifyTake(pdTRUE,
            pdMS_TO_TICKS(policy->state.period_ms));
    }
#endif
}

/*
 * SOCTask 按策略周期消费 ProtectTask 投递的独立 CC queue，并把 SOC owner
 * 结果交给 persistence service。它不重复读取 CC 寄存器，也不参与 FET 仲裁。
 */
void Task_SOC(void *argument)
{
    TickType_t period;
    TickType_t last;
#if !defined(TEST_PHASE6_IMAGE)
    BMS_SocSnapshot_t snapshot;
    uint32_t now_ms;
#endif

    (void)argument;
#if defined(TEST_PHASE6_IMAGE)
    period = pdMS_TO_TICKS(1000U);
#else
    period = pdMS_TO_TICKS(BMS_Policy_Get()->soc.period_ms);
#endif
    last = xTaskGetTickCount();
    for (;;)
    {
        vTaskDelayUntil(&last, period);
#if !defined(TEST_PHASE6_IMAGE)
        now_ms = (uint32_t)(
            xTaskGetTickCount() * portTICK_PERIOD_MS);
        BMS_Soc_RunOnce(now_ms);
        snapshot = BMS_Soc_GetSnapshot();
        (void)BMS_Persistence_TargetServiceSoc(
            snapshot.soc_permille,
            snapshot.remaining_capacity_mah,
            snapshot.queue_gap_count,
            snapshot.generation_change_count,
            snapshot.valid,
            now_ms);
        BMS_Health_Heartbeat(BMS_HEALTH_TASK_SOC);
#endif
    }
}

/*
 * BalanceTask 每 1 s 评估电芯差值与所有安全门禁，是调度器启动后唯一允许
 * 写 CELLBAL 的任务；独立周期使均衡控制不会延长 10 ms 安全协调路径。
 */
void Task_Balance(void *argument)
{
    TickType_t period;
    TickType_t last;

    (void)argument;
#if defined(TEST_PHASE6_IMAGE)
    period = pdMS_TO_TICKS(1000U);
#else
    period = pdMS_TO_TICKS(BMS_Policy_Get()->balance.period_ms);
#endif
    last = xTaskGetTickCount();
    for (;;)
    {
        vTaskDelayUntil(&last, period);
#if !defined(TEST_PHASE6_IMAGE)
        BMS_Balance_RunOnce((uint32_t)(
            xTaskGetTickCount() * portTICK_PERIOD_MS));
        BMS_Health_Heartbeat(BMS_HEALTH_TASK_BALANCE);
#endif
    }
}

/*
 * CANTxTask 每 10 ms 排空硬件发送服务，以保证 mailbox 有界推进；周期诊断帧
 * 每 100 ms 生成一次。UART debug 也挂在此任务，但使用 best-effort 非阻塞
 * service，TX busy 时立即让路，不能反向拖慢 CAN 或安全任务。
 */
void Task_CANTx(void *argument)
{
    const TickType_t period = pdMS_TO_TICKS(10U);
    TickType_t last;
#if !defined(TEST_PHASE6_IMAGE)
    uint32_t now_ms;
    uint32_t last_publish_ms;
#endif

    (void)argument;
    last = xTaskGetTickCount();
#if !defined(TEST_PHASE6_IMAGE)
    last_publish_ms = (uint32_t)(last * portTICK_PERIOD_MS) - 100UL;
#endif
    for (;;)
    {
        vTaskDelayUntil(&last, period);
#if !defined(TEST_PHASE6_IMAGE)
        now_ms = (uint32_t)(
            xTaskGetTickCount() * portTICK_PERIOD_MS);
        if ((uint32_t)(now_ms - last_publish_ms) >= 100UL)
        {
            BMS_Can_TxRunOnce(now_ms);
            last_publish_ms = now_ms;
        }
        BMS_Can_TxHardwareService(now_ms);
        BMS_Debug_Service(now_ms);
        BMS_Health_Heartbeat(BMS_HEALTH_TASK_CAN_TX);
#endif
    }
}

/*
 * CANRxTask 只消费 ISR 写入的服务帧队列。协议处理可以更新诊断/服务状态，
 * 但不能直接写 FET、CELLBAL、fault bitmap 或 IWDG，从入口处守住 ownership。
 */
void Task_CANRx(void *argument)
{
#if !defined(TEST_PHASE6_IMAGE)
    BMS_CanFrame_t frame;
    uint32_t now_ms;
#endif

    (void)argument;
#if !defined(TEST_PHASE6_IMAGE)
    (void)BMS_Can_EnableTargetRx();
#endif
    for (;;)
    {
#if !defined(TEST_PHASE6_IMAGE)
        if ((xCanRxQueue != NULL) &&
            (xQueueReceive(xCanRxQueue, &frame,
                           pdMS_TO_TICKS(100U)) == pdPASS))
        {
            now_ms = (uint32_t)(
                xTaskGetTickCount() * portTICK_PERIOD_MS);
            BMS_Can_RxProcess(&frame,
                (uint32_t)(frame.received_tick * portTICK_PERIOD_MS),
                now_ms);
        }
        BMS_Health_Heartbeat(BMS_HEALTH_TASK_CAN_RX);
#else
        vTaskDelay(pdMS_TO_TICKS(10U));
#endif
    }
}

/* ------------------------------------------------------------------ */
/* 任务创建顺序固定；失败后停止继续创建，由启动层决定是否进入安全失败路径。 */
/* ------------------------------------------------------------------ */
static BaseType_t App_Rtos_CreateOne(TaskFunction_t function,
                                     const char *name,
                                     uint16_t stack_words,
                                     UBaseType_t priority,
                                     TaskHandle_t *created_handle)
{
    TaskHandle_t handle;
    BaseType_t result;

    result = xTaskCreate(function, name, stack_words, NULL, priority, &handle);
    if ((result == pdPASS) && (created_handle != NULL))
    {
        *created_handle = handle;
    }
    return result;
}

BaseType_t App_Rtos_CreateTasks(void)
{
    if (App_Rtos_CreateOne(Task_Protect, "Protect",
                           APP_RTOS_STACK_PROTECT,
                           APP_RTOS_PRIO_PROTECT, NULL) != pdPASS)
    {
        return pdFALSE;
    }
    if (App_Rtos_CreateOne(Task_Sample, "Sample",
                           APP_RTOS_STACK_SAMPLE,
                           APP_RTOS_PRIO_SAMPLE, NULL) != pdPASS)
    {
        return pdFALSE;
    }
    if (App_Rtos_CreateOne(Task_State, "State",
                           APP_RTOS_STACK_STATE,
                           APP_RTOS_PRIO_STATE,
                           &s_state_task_handle) != pdPASS)
    {
        return pdFALSE;
    }
    if (App_Rtos_CreateOne(Task_SOC, "SOC",
                           APP_RTOS_STACK_SOC,
                           APP_RTOS_PRIO_SOC, NULL) != pdPASS)
    {
        return pdFALSE;
    }
    if (App_Rtos_CreateOne(Task_Balance, "Balance",
                           APP_RTOS_STACK_BALANCE,
                           APP_RTOS_PRIO_BALANCE, NULL) != pdPASS)
    {
        return pdFALSE;
    }
    if (App_Rtos_CreateOne(Task_CANTx, "CANTx",
                           APP_RTOS_STACK_CAN_TX,
                           APP_RTOS_PRIO_CAN_TX, NULL) != pdPASS)
    {
        return pdFALSE;
    }
    if (App_Rtos_CreateOne(Task_CANRx, "CANRx",
                           APP_RTOS_STACK_CAN_RX,
                           APP_RTOS_PRIO_CAN_RX, NULL) != pdPASS)
    {
        return pdFALSE;
    }
    return pdTRUE;
}

void App_Rtos_NotifyStateUrgent(void)
{
    if (s_state_task_handle != NULL)
    {
        xTaskNotifyGive(s_state_task_handle);
    }
}

void App_Rtos_RequestProtectService(void)
{
    if (xAfeAlertSem != NULL)
    {
        (void)xSemaphoreGive(xAfeAlertSem);
    }
}
