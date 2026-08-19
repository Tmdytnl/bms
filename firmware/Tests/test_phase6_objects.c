#include "test_phase6.h"

#include <stdbool.h>
#include <stddef.h>

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
 * Phase 6 object creation test. Runs BEFORE the scheduler starts; the
 * FreeRTOS heap and object constructors are exercised directly.
 * All seven IPC objects must be created successfully and be non-NULL.
 */
uint32_t Test_Phase6_Objects(void)
{
    uint32_t failures;
    BaseType_t result;

    failures = 0UL;

    g_p6_probe = 10UL;
    result = App_Rtos_CreateObjects();
    g_p6_probe = 11UL;
    TEST_CHECK(result == pdTRUE);

    TEST_CHECK(xI2CMutex != NULL);
    TEST_CHECK(xDataMutex != NULL);
    TEST_CHECK(xAfeAlertSem != NULL);
    TEST_CHECK(xCanTxQueue != NULL);
    TEST_CHECK(xCanRxQueue != NULL);
    TEST_CHECK(xCcSampleQueue != NULL);
    TEST_CHECK(xSysEvents != NULL);

    /* Event bits must match the spec §11.2 definitions. */
    TEST_CHECK(EVT_SAMPLE_READY == ((EventBits_t)1U << 0));
    TEST_CHECK(EVT_AFE_ONLINE == ((EventBits_t)1U << 1));
    TEST_CHECK(EVT_FAULT_PRESENT == ((EventBits_t)1U << 2));
    TEST_CHECK(EVT_PARAM_DIRTY == ((EventBits_t)1U << 3));
    TEST_CHECK(EVT_CC_QUEUE_OVERFLOW == ((EventBits_t)1U << 4));

    /* Queue element layout must match the spec §11.3 fields. The struct
     * is a C layout: BMS_CanFrame_t pads to 16 bytes (uint32_t alignment),
     * BMS_CcSample_t to 8 (tick at offset 4 after int16 + padding). */
    TEST_CHECK(offsetof(BMS_CcSample_t, raw) == 0);
    TEST_CHECK(offsetof(BMS_CcSample_t, tick) == 4U);
    TEST_CHECK(sizeof(BMS_CcSample_t) == 8U);

    TEST_CHECK(offsetof(BMS_CanFrame_t, ext_id) == 0);
    TEST_CHECK(offsetof(BMS_CanFrame_t, dlc) == sizeof(uint32_t));
    TEST_CHECK(offsetof(BMS_CanFrame_t, data) ==
               (sizeof(uint32_t) + sizeof(uint8_t)));
    TEST_CHECK(sizeof(BMS_CanFrame_t) == 16U);

    /* Priorities must match errata C-01: 5/4/3/3/2/2/2. */
    TEST_CHECK(APP_RTOS_PRIO_PROTECT == 5U);
    TEST_CHECK(APP_RTOS_PRIO_SAMPLE == 4U);
    TEST_CHECK(APP_RTOS_PRIO_STATE == 3U);
    TEST_CHECK(APP_RTOS_PRIO_SOC == 3U);
    TEST_CHECK(APP_RTOS_PRIO_BALANCE == 2U);
    TEST_CHECK(APP_RTOS_PRIO_CAN_TX == 2U);
    TEST_CHECK(APP_RTOS_PRIO_CAN_RX == 2U);

    /* Stack sizes must match spec §11.4 (words). */
    TEST_CHECK(APP_RTOS_STACK_PROTECT == 160U);
    TEST_CHECK(APP_RTOS_STACK_SAMPLE == 192U);
    TEST_CHECK(APP_RTOS_STACK_STATE == 128U);
    TEST_CHECK(APP_RTOS_STACK_SOC == 192U);
    TEST_CHECK(APP_RTOS_STACK_BALANCE == 160U);
    TEST_CHECK(APP_RTOS_STACK_CAN_TX == 160U);
    TEST_CHECK(APP_RTOS_STACK_CAN_RX == 160U);

    return failures;
}
