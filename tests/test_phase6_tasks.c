#include "test_phase6.h"

#include <stdbool.h>

#include "os_objects_internal.h"
#include "apl_rtos.h"

#define TEST_CHECK(expr_)          \
    do                             \
    {                              \
        if (!(expr_))              \
        {                          \
            ++failures;            \
        }                          \
    } while (0)

/*
 * scheduler 前创建七个 task control block；此时 scheduler 必须仍为 not-started，
 * 正式 image 只由 main 在全部对象就绪后启动。
 */
uint32_t Test_Phase6_Tasks(void)
{
    BQ76940_t afe_device;
    uint32_t failures;
    OS_Result_t result;

    failures = 0UL;

    /* 复用 Test_Phase6_Objects 创建的唯一 production-equivalent object set；
     * 重复创建只会引入 harness leak 并扭曲 heap evidence。 */
    g_p6_probe = 20UL;
    result = ((g_os_i2c_mutex != NULL) && (g_os_data_mutex != NULL) &&
              (g_os_afe_alert_semaphore != NULL) && (g_os_can_tx_queue != NULL) &&
              (g_os_can_rx_queue != NULL) && (g_os_cc_sample_queue != NULL) &&
              (g_os_system_events != NULL)) ? OS_PASS : OS_FAIL;
    g_p6_probe = 21UL;
    TEST_CHECK(result == OS_PASS);

    result = APL_Rtos_CreateTasks(&afe_device);
    g_p6_probe = 22UL;
    TEST_CHECK(result == OS_PASS);

    /* 正式 flow 此刻 scheduler 仍未运行。 */
    g_p6_probe = 23UL;
    TEST_CHECK(!OS_SchedulerIsRunning());
    g_p6_probe = 24UL;

    return failures;
}
