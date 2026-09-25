#include "os_objects_internal.h"
#include "apl_rtos.h"

#include <stddef.h>

/* 按 newest-wins 规则把 Protect 样本提交 SOC 队列并返回交接结果。 */
bool APL_Rtos_TransportCcSample(const BMS_CcSample_t *sample,
                                bool *overflowed,
                                bool *oldest_was_dropped)
{
    /* 队列满时被替换的最旧 CC 样本。 */
    BMS_CcSample_t discarded;
    /* 样本是否成功进入队列。 */
    bool inserted;

    if ((sample == NULL) || (overflowed == NULL) ||
        (oldest_was_dropped == NULL) || (g_os_cc_sample_queue == NULL))
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
    OS_SchedulerSuspend();
    if (OS_QueueSend(g_os_cc_sample_queue, sample, 0U) == OS_PASS)
    {
        inserted = true;
    }
    else
    {
        *overflowed = true;
        if (OS_QueueReceive(g_os_cc_sample_queue, &discarded, 0U) == OS_PASS)
        {
            *oldest_was_dropped = true;
            inserted = (OS_QueueSend(g_os_cc_sample_queue, sample, 0U) == OS_PASS);
        }
    }
    (void)OS_SchedulerResume();

    if (*overflowed && (g_os_system_events != NULL))
    {
        (void)OS_EventGroupSetBits(g_os_system_events, EVT_CC_QUEUE_OVERFLOW);
    }
    return inserted;
}
