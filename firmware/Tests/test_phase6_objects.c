#include "test_phase6.h"

#include <stdbool.h>
#include <stddef.h>

#include "apl_rtos_internal.h"
#include "bms_can.h"

#define TEST_CHECK(expr_)          \
    do                             \
    {                              \
        if (!(expr_))              \
        {                          \
            ++failures;            \
        }                          \
    } while (0)

/*
 * scheduler 启动前直接覆盖 FreeRTOS heap/object constructor；七个 IPC 必须全部
 * 成功且 non-NULL，禁止半套 object set。
 */
uint32_t Test_Phase6_Objects(void)
{
    uint32_t failures;
    BaseType_t result;

    failures = 0UL;

    g_p6_probe = 10UL;
    result = APL_Rtos_CreateObjects();
    g_p6_probe = 11UL;
    TEST_CHECK(result == pdTRUE);

    TEST_CHECK(xI2CMutex != NULL);
    TEST_CHECK(xDataMutex != NULL);
    TEST_CHECK(xAfeAlertSem != NULL);
    TEST_CHECK(xCanTxQueue != NULL);
    TEST_CHECK(xCanRxQueue != NULL);
    TEST_CHECK(xCcSampleQueue != NULL);
    TEST_CHECK(xSysEvents != NULL);

    /* event bit 必须匹配 spec §11.2。 */
    TEST_CHECK(EVT_SAMPLE_READY == ((EventBits_t)1U << 0));
    TEST_CHECK(EVT_AFE_ONLINE == ((EventBits_t)1U << 1));
    TEST_CHECK(EVT_FAULT_PRESENT == ((EventBits_t)1U << 2));
    TEST_CHECK(EVT_PARAM_DIRTY == ((EventBits_t)1U << 3));
    TEST_CHECK(EVT_CC_QUEUE_OVERFLOW == ((EventBits_t)1U << 4));

    /* APL queues transport plain millisecond-domain FML records by value. */
    TEST_CHECK(offsetof(BMS_CcSample_t, raw) == 0);
    TEST_CHECK(offsetof(BMS_CcSample_t, sample_ms) == 4U);
    TEST_CHECK(offsetof(BMS_CcSample_t, xready_generation) == 8U);
    TEST_CHECK(offsetof(BMS_CcSample_t, transport_id) == 12U);
    TEST_CHECK(sizeof(BMS_CcSample_t) == 16U);

    TEST_CHECK(offsetof(BMS_CanFrame_t, standard_id) == 0);
    TEST_CHECK(offsetof(BMS_CanFrame_t, dlc) == sizeof(uint32_t));
    TEST_CHECK(offsetof(BMS_CanFrame_t, data) ==
               (sizeof(uint32_t) + sizeof(uint8_t)));
    TEST_CHECK(offsetof(BMS_CanFrame_t, received_ms) == 16U);
    TEST_CHECK(sizeof(BMS_CanFrame_t) == 20U);

    /* priority 必须匹配 C-01：5/4/3/3/2/2/2。 */
    TEST_CHECK(APL_RTOS_PRIO_PROTECT == 5U);
    TEST_CHECK(APL_RTOS_PRIO_SAMPLE == 4U);
    TEST_CHECK(APL_RTOS_PRIO_STATE == 3U);
    TEST_CHECK(APL_RTOS_PRIO_SOC == 3U);
    TEST_CHECK(APL_RTOS_PRIO_BALANCE == 2U);
    TEST_CHECK(APL_RTOS_PRIO_CAN_TX == 2U);
    TEST_CHECK(APL_RTOS_PRIO_CAN_RX == 2U);

    /* stack size 单位为 word，并匹配 spec §11.4。 */
    TEST_CHECK(APL_RTOS_STACK_PROTECT == 160U);
    TEST_CHECK(APL_RTOS_STACK_SAMPLE == 192U);
    /* ARMCC5 报告 State 1104-byte depth，因此使用更新后的 allocation。 */
    TEST_CHECK(APL_RTOS_STACK_STATE == 384U);
    TEST_CHECK(APL_RTOS_STACK_SOC == 192U);
    /* Balance/CAN Tx callgraph 分别需要 840/752 B。 */
    TEST_CHECK(APL_RTOS_STACK_BALANCE == 256U);
    TEST_CHECK(APL_RTOS_STACK_CAN_TX == 240U);
    TEST_CHECK(APL_RTOS_STACK_CAN_RX == 160U);

    return failures;
}
