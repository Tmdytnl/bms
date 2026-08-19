#include "test_phase6.h"

#include <stdbool.h>

#include "app_rtos.h"

#define TEST_CHECK(expr_)          \
    do                             \
    {                              \
        if (!(expr_))              \
        {                          \
            ++failures;            \
        }                          \
    } while (0)

/*
 * Phase 6 task creation test. Runs before the scheduler starts; all seven
 * task control blocks are created from the FreeRTOS heap. The scheduler
 * must still report "not started" (the production image starts it from
 * main after this point).
 */
uint32_t Test_Phase6_Tasks(void)
{
    uint32_t failures;
    BaseType_t result;

    failures = 0UL;

    /* Test_Phase6_Objects already created the single production-equivalent
     * object set. Reuse it here; creating a second set would only measure a
     * harness leak and distort the Phase 6 heap evidence. */
    g_p6_probe = 20UL;
    result = ((xI2CMutex != NULL) && (xDataMutex != NULL) &&
              (xAfeAlertSem != NULL) && (xCanTxQueue != NULL) &&
              (xCanRxQueue != NULL) && (xCcSampleQueue != NULL) &&
              (xSysEvents != NULL)) ? pdTRUE : pdFALSE;
    g_p6_probe = 21UL;
    TEST_CHECK(result == pdTRUE);

    result = App_Rtos_CreateTasks();
    g_p6_probe = 22UL;
    TEST_CHECK(result == pdTRUE);

    /* Scheduler must not be running yet in the production flow. */
    g_p6_probe = 23UL;
    TEST_CHECK(xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED);
    g_p6_probe = 24UL;

    return failures;
}
