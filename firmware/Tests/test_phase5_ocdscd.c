#include "test_phase5.h"

#include <stdbool.h>

#include "bq76940_control.h"

#define TEST_CHECK(expr_)          \
    do                             \
    {                              \
        if (!(expr_))              \
        {                          \
            ++failures;            \
        }                          \
    } while (0)

uint32_t Test_Phase5_OcdScd(void)
{
    uint32_t failures;
    uint8_t code;

    failures = 0UL;

    /* OCD threshold selection (RSNS=1 upper range 17..100 mV). */
    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectOcdThreshold(56U, true, &code) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(code == 7U);        /* 56 mV -> code 7 */

    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectOcdThreshold(17U, true, &code) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(code == 0U);        /* 17 mV -> code 0 */

    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectOcdThreshold(100U, true, &code) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(code == 15U);       /* 100 mV -> code 15 */

    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectOcdThreshold(101U, true, &code) ==
               BQ76940_STATUS_RANGE_ERROR);

    /* RSNS=0 lower range (8..50 mV): 30 mV -> code 8 (31 mV). */
    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectOcdThreshold(30U, false, &code) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(code == 8U);

    /* OCD delay (8..1280 ms). */
    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectOcdDelayMs(80U, &code) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(code == 3U);

    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectOcdDelayMs(100U, &code) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(code == 4U);        /* 160 ms is first >= 100 */

    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectOcdDelayMs(1280U, &code) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(code == 7U);

    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectOcdDelayMs(2000U, &code) ==
               BQ76940_STATUS_RANGE_ERROR);

    /* SCD threshold (RSNS=1 upper range 6..178 mV). */
    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectScdThreshold(111U, true, &code) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(code == 4U);

    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectScdThreshold(178U, true, &code) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(code == 7U);

    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectScdThreshold(179U, true, &code) ==
               BQ76940_STATUS_RANGE_ERROR);

    /* RSNS=0: 60 mV -> code 4 (67 mV). */
    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectScdThreshold(60U, false, &code) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(code == 4U);

    /* SCD delay (70..400 us). */
    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectScdDelayUs(100U, &code) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(code == 1U);

    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectScdDelayUs(400U, &code) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(code == 3U);

    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectScdDelayUs(401U, &code) ==
               BQ76940_STATUS_RANGE_ERROR);

    /* OV delay (1..8 s). */
    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectOvDelayS(2U, &code) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(code == 1U);

    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectOvDelayS(4U, &code) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(code == 2U);

    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectOvDelayS(9U, &code) ==
               BQ76940_STATUS_RANGE_ERROR);

    /* UV delay (1..16 s). */
    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectUvDelayS(4U, &code) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(code == 1U);

    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectUvDelayS(16U, &code) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(code == 3U);

    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectUvDelayS(17U, &code) ==
               BQ76940_STATUS_RANGE_ERROR);

    /* NULL output. */
    TEST_CHECK(BQ76940_Control_SelectOcdThreshold(56U, true, NULL) ==
               BQ76940_STATUS_INVALID_ARGUMENT);
    TEST_CHECK(BQ76940_Control_SelectOcdDelayMs(80U, NULL) ==
               BQ76940_STATUS_INVALID_ARGUMENT);
    TEST_CHECK(BQ76940_Control_SelectScdThreshold(111U, true, NULL) ==
               BQ76940_STATUS_INVALID_ARGUMENT);
    TEST_CHECK(BQ76940_Control_SelectScdDelayUs(100U, NULL) ==
               BQ76940_STATUS_INVALID_ARGUMENT);
    TEST_CHECK(BQ76940_Control_SelectOvDelayS(2U, NULL) ==
               BQ76940_STATUS_INVALID_ARGUMENT);
    TEST_CHECK(BQ76940_Control_SelectUvDelayS(4U, NULL) ==
               BQ76940_STATUS_INVALID_ARGUMENT);

    /* PROTECT register composition. */
    TEST_CHECK(BQ76940_Control_ComposeProtect1(true, 0U, 0U) == 0x80U);
    /* TI example path: SCD 111mV (code 4) + 100us (code 1), RSNS=1. */
    TEST_CHECK(BQ76940_Control_ComposeProtect1(true, 1U, 4U) == 0x8CU);
    /* Datasheet OCD example: 320ms (0x5) + 14.4A code 0x0A -> 0x5A.
     * (The datasheet prose says 0x5B, but the register bit definition
     * (delay<<4)|threshold yields 0x5A; see Phase 5 report.) */
    TEST_CHECK(BQ76940_Control_ComposeProtect2(5U, 0x0AU) == 0x5AU);
    /* TI example: UV delay 4s (code 1) + OV delay 2s (code 1) -> 0x50. */
    TEST_CHECK(BQ76940_Control_ComposeProtect3(1U, 1U) == 0x50U);

    return failures;
}
