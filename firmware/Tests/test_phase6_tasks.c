#include "test_phase6.h"

#include <stdbool.h>

#include "apl_rtos_internal.h"

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
    BaseType_t result;

    failures = 0UL;

    /* 复用 Test_Phase6_Objects 创建的唯一 production-equivalent object set；
     * 重复创建只会引入 harness leak 并扭曲 heap evidence。 */
    g_p6_probe = 20UL;
    result = ((xI2CMutex != NULL) && (xDataMutex != NULL) &&
              (xAfeAlertSem != NULL) && (xCanTxQueue != NULL) &&
              (xCanRxQueue != NULL) && (xCcSampleQueue != NULL) &&
              (xSysEvents != NULL)) ? pdTRUE : pdFALSE;
    g_p6_probe = 21UL;
    TEST_CHECK(result == pdTRUE);

    result = APL_Rtos_CreateTasks(&afe_device);
    g_p6_probe = 22UL;
    TEST_CHECK(result == pdTRUE);

    /* 正式 flow 此刻 scheduler 仍未运行。 */
    g_p6_probe = 23UL;
    TEST_CHECK(xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED);
    g_p6_probe = 24UL;

    return failures;
}
