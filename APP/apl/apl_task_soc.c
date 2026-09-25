#include "apl_tasks.h"
#include "apl_rtos.h"

#include "os_objects_internal.h"
#include "fml_health.h"
#include "fml_persistence.h"
#include "fml_policy.h"
#include "fml_soc.h"

/* 消费独占 CC 队列、推进 SOC 积分并服务持久化。 */
void APL_TaskSoc(void *argument)
{
    /* 本轮待消费的样本数组。 */
    BMS_CcSample_t samples[BMS_SOC_MAX_CC_SAMPLES_PER_RUN];
    /* 本次读取的一致状态快照。 */
    BMS_SocSnapshot_t snapshot;
    /* 本轮处理使用的只读 BMS 策略。 */
    const BMS_Policy_t *policy;
    /* 当前任务的周期，单位为 OS tick。 */
    OS_Tick_t period;
    /* 上一次周期唤醒的 tick。 */
    OS_Tick_t last;
    /* 本轮取得的系统事件位。 */
    OS_EventBits_t events;
    /* 本次处理的单调毫秒时刻。 */
    uint32_t now_ms;
    /* 当前已处理或已生成的元素数量。 */
    uint8_t count;

    (void)argument;
    policy = FML_Policy_Get();
    period = OS_MsToTicks(policy->soc.period_ms);
    last = OS_TickCount();
    for (;;)
    {
        OS_DelayUntil(&last, period);
        /* SOC 是 CC queue 唯一 consumer；每周期有界 drain，避免低优先级长期占用 CPU。 */
        count = 0U;
        while ((count < BMS_SOC_MAX_CC_SAMPLES_PER_RUN) &&
               (g_os_cc_sample_queue != NULL) &&
               (OS_QueueReceive(g_os_cc_sample_queue, &samples[count], 0U) == OS_PASS))
        {
            ++count;
        }
        /* clear 返回清除前的 bits，把 APL queue gap 证据一次性交给 FML SOC。 */
        events = OS_EventGroupClearBits(g_os_system_events, EVT_CC_QUEUE_OVERFLOW);
        now_ms = APL_TimeMs();
        FML_Soc_RunOnce(now_ms, samples, count,
                        (events & EVT_CC_QUEUE_OVERFLOW) != 0U);
        snapshot = FML_Soc_GetSnapshot();
        /* Flash save 仍由 SOC task 单一执行，且此处不持 data/I2C lock。 */
        (void)FML_Persistence_TargetServiceSoc(
            snapshot.soc_permille,
            snapshot.remaining_capacity_mah,
            snapshot.queue_gap_count,
            snapshot.generation_change_count,
            snapshot.valid,
            now_ms);
        FML_Health_Heartbeat(BMS_HEALTH_TASK_SOC);
    }
}
