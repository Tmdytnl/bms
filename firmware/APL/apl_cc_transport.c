#include "apl_rtos_internal.h"

#include <stddef.h>

/* 按 newest-wins 规则把 Protect 样本提交 SOC 队列并返回交接结果。 */
bool APL_Rtos_TransportCcSample(const BMS_CcSample_t *sample,
                                bool *overflowed,
                                bool *oldest_was_dropped)
{
    BMS_CcSample_t discarded;
    bool inserted;

    if ((sample == NULL) || (overflowed == NULL) ||
        (oldest_was_dropped == NULL) || (xCcSampleQueue == NULL))
    {
        return false;
    }
    *overflowed = false;
    *oldest_was_dropped = false;
    inserted = false;

    /*
     * 发送、丢最旧项、再发送必须是一个不可被 SOC consumer 插入的短临界流程；
     * 否则“满队列”观察与替换动作之间可能被消费，导致 newest-wins 结果失真。
     * 临界区只操作内存队列，不包含 I2C、等待或领域判断。
     */
    vTaskSuspendAll();
    if (xQueueSend(xCcSampleQueue, sample, 0U) == pdPASS)
    {
        inserted = true;
    }
    else
    {
        *overflowed = true;
        if (xQueueReceive(xCcSampleQueue, &discarded, 0U) == pdPASS)
        {
            *oldest_was_dropped = true;
            inserted = (xQueueSend(xCcSampleQueue, sample, 0U) == pdPASS);
        }
    }
    (void)xTaskResumeAll();

    if (*overflowed && (xSysEvents != NULL))
    {
        (void)xEventGroupSetBits(xSysEvents, EVT_CC_QUEUE_OVERFLOW);
    }
    return inserted;
}
