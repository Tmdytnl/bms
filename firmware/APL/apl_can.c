#include "apl_can.h"

#include <limits.h>
#include <string.h>

#include "apl_rtos_internal.h"
#include "bms_can.h"
#include "bsp_can.h"

#define APL_CAN_TARGET_RETRY_MS                  (1000UL)

/* 启动期绑定的 CAN 策略；决定过滤器和重试时序。 */
static const BMS_Policy_t *s_policy;
/* 上次 CAN 外设初始化尝试的毫秒时刻。 */
static uint32_t s_last_init_attempt_ms;
/* 至少尝试过一次目标 CAN 初始化的标志。 */
static bool s_init_attempted;
/* APL 已完成接收队列准备并允许 RX 中断的标志。 */
static bool s_rx_enabled;
/* ISR 累积、待任务层汇总的 RX FIFO 溢出次数。 */
static volatile uint32_t s_rx_fifo_overrun_pending;
/* ISR 累积、待任务层汇总的 RX 队列丢帧次数。 */
static volatile uint32_t s_rx_queue_drop_pending;

/* 把 ISR 累积的丢帧与溢出计数转入 FML 诊断后清零。 */
static void APL_Can_FlushIsrDiagnostics(void)
{
    uint32_t overrun_count;
    uint32_t drop_count;

    /* ISR 只累加 pending counter；task 临界复制并清零，FML 诊断永不在 ISR 内更新。 */
    taskENTER_CRITICAL();
    overrun_count = s_rx_fifo_overrun_pending;
    drop_count = s_rx_queue_drop_pending;
    s_rx_fifo_overrun_pending = 0UL;
    s_rx_queue_drop_pending = 0UL;
    taskEXIT_CRITICAL();
    BMS_Can_RecordDiagnosticCount(
        BMS_CAN_DIAG_TARGET_RX_FIFO_OVERRUN, overrun_count);
    BMS_Can_RecordDiagnosticCount(
        BMS_CAN_DIAG_TARGET_RX_QUEUE_DROP, drop_count);
}

/* 在中断上下文累计一次 CAN RX FIFO 溢出。 */
void APL_Can_RecordRxFifoOverrunFromISR(void)
{
    /* 饱和而非回绕，避免长时间故障后诊断计数伪装成较小值。 */
    if (s_rx_fifo_overrun_pending < UINT32_MAX)
    {
        ++s_rx_fifo_overrun_pending;
    }
}

/* 在中断上下文累计一次 CAN RX 队列丢帧。 */
void APL_Can_RecordRxQueueDropFromISR(void)
{
    if (s_rx_queue_drop_pending < UINT32_MAX)
    {
        ++s_rx_queue_drop_pending;
    }
}

/* 绑定 CAN 硬件配置与过滤器，不把硬件状态交给 FML。 */
bool APL_Can_BindTarget(const BMS_Policy_t *policy)
{
    s_policy = BMS_Policy_Validate(policy) ? policy : NULL;
    s_init_attempted = true;
    s_last_init_attempt_ms = 0UL;
    s_rx_enabled = false;
    s_rx_fifo_overrun_pending = 0UL;
    s_rx_queue_drop_pending = 0UL;
    if ((s_policy == NULL) || !s_policy->can.standard_11_bit_ids ||
        !BSP_CAN_Init500K(s_policy->can.service_rx_id))
    {
        BMS_Can_RecordDiagnostic(BMS_CAN_DIAG_TARGET_INIT_FAILURE);
        return false;
    }
    return true;
}

/* 在队列准备完成后开启 bxCAN 接收中断。 */
bool APL_Can_EnableRx(void)
{
    if (!BSP_CAN_EnableRxInterrupt())
    {
        BMS_Can_RecordDiagnostic(BMS_CAN_DIAG_TARGET_INIT_FAILURE);
        return false;
    }
    s_rx_enabled = true;
    return true;
}

/* 把 FML 编码的周期诊断帧投递到 APL 发送队列。 */
void APL_Can_QueuePeriodic(uint32_t now_ms)
{
    BMS_CanFrame_t frames[BMS_CAN_TX_FRAME_COUNT];
    uint8_t count;
    uint8_t index;

    count = BMS_Can_BuildPeriodicFrames(now_ms, frames);
    for (index = 0U; index < count; ++index)
    {
        if ((xCanTxQueue != NULL) &&
            (xQueueSend(xCanTxQueue, &frames[index], 0U) == pdPASS))
        {
            BMS_Can_RecordDiagnostic(BMS_CAN_DIAG_TX_ENQUEUED);
        }
        else
        {
            BMS_Can_RecordDiagnostic(BMS_CAN_DIAG_TX_QUEUE_DROP);
        }
    }
}

/* 有界处理发送队列、邮箱与 bus-off 硬件恢复。 */
void APL_Can_TxHardwareService(uint32_t now_ms)
{
    BMS_CanFrame_t queued;
    BSP_CanFrame_t target;
    BSP_CanTxResult_t result;

    APL_Can_FlushIsrDiagnostics();
    if (!BSP_CAN_IsInitialized())
    {
        if (s_init_attempted &&
            ((uint32_t)(now_ms - s_last_init_attempt_ms) <
             APL_CAN_TARGET_RETRY_MS))
        {
            return;
        }
        s_init_attempted = true;
        s_last_init_attempt_ms = now_ms;
        s_rx_enabled = false;
        if ((s_policy == NULL) ||
            !BSP_CAN_Init500K(s_policy->can.service_rx_id))
        {
            BMS_Can_RecordDiagnostic(BMS_CAN_DIAG_TARGET_INIT_FAILURE);
            return;
        }
    }
    if (!s_rx_enabled && !APL_Can_EnableRx())
    {
        return;
    }
    if (BSP_CAN_IsBusOff())
    {
        if (BSP_CAN_Recover())
        {
            BMS_Can_RecordDiagnostic(BMS_CAN_DIAG_TARGET_BUS_OFF_RECOVERY);
        }
        else
        {
            s_rx_enabled = false;
            return;
        }
    }
    while ((xCanTxQueue != NULL) &&
           (xQueueReceive(xCanTxQueue, &queued, 0U) == pdPASS))
    {
        target.id = queued.standard_id;
        target.extended = s_policy != NULL &&
            !s_policy->can.standard_11_bit_ids;
        target.dlc = queued.dlc;
        (void)memcpy(target.data, queued.data, sizeof(target.data));
        result = BSP_CAN_TryTransmit(&target);
        if (result == BSP_CAN_TX_ACCEPTED)
        {
            BMS_Can_RecordDiagnostic(BMS_CAN_DIAG_TARGET_TX);
        }
        else if (result == BSP_CAN_TX_NO_MAILBOX)
        {
            /* mailbox 暂满不是 frame 失败：放回队首并结束本轮，保持队列顺序。 */
            if (xQueueSendToFront(xCanTxQueue, &queued, 0U) != pdPASS)
            {
                BMS_Can_RecordDiagnostic(BMS_CAN_DIAG_TARGET_TX_DROP);
            }
            break;
        }
        else
        {
            BMS_Can_RecordDiagnostic(BMS_CAN_DIAG_TARGET_TX_DROP);
        }
    }
}
