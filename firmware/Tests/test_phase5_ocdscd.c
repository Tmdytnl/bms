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

static const uint16_t TEST_SCD_RSNS1_MV[8] =
{
    44U, 67U, 89U, 111U, 133U, 155U, 178U, 200U
};

static const uint16_t TEST_SCD_RSNS0_MV[8] =
{
    22U, 33U, 44U, 56U, 67U, 78U, 89U, 100U
};

uint32_t Test_Phase5_OcdScd(void)
{
    uint32_t failures;
    uint8_t code;
    uint8_t index;
    uint8_t register_value;

    failures = 0UL;

    /* OCD threshold selection（RSNS=1 upper range 17..100 mV）。 */
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

    /* RSNS=0 lower range：30 mV→code8（31 mV）。 */
    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectOcdThreshold(30U, false, &code) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(code == 8U);

    /* OCD delay 8..1280 ms。 */
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

    /* 官方 SCD threshold table：覆盖两个 range 的每个 code。 */
    for (index = 0U; index < 8U; ++index)
    {
        code = 0xFFU;
        TEST_CHECK(BQ76940_Control_SelectScdThreshold(
                       TEST_SCD_RSNS1_MV[index], true, &code) ==
                   BQ76940_STATUS_OK);
        TEST_CHECK(code == index);

        code = 0xFFU;
        TEST_CHECK(BQ76940_Control_SelectScdThreshold(
                       TEST_SCD_RSNS0_MV[index], false, &code) ==
                   BQ76940_STATUS_OK);
        TEST_CHECK(code == index);
    }

    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectScdThreshold(111U, true, &code) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(code == 3U);

    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectScdThreshold(201U, true, &code) ==
               BQ76940_STATUS_RANGE_ERROR);

    /* RSNS=0：60 mV→code4（67 mV）。 */
    code = 0xFFU;
    TEST_CHECK(BQ76940_Control_SelectScdThreshold(60U, false, &code) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(code == 4U);

    /* SCD delay 70..400 us。 */
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

    /* OV delay 1..8 s。 */
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

    /* UV delay 1..16 s。 */
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

    /* NULL output。 */
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

    /* PROTECT register composition。 */
    register_value = 0xAAU;
    TEST_CHECK(BQ76940_Control_ComposeProtect1(true, 0U, 0U,
                                               &register_value) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(register_value == 0x80U);
    /* TI table：SCD 111 mV（code3）+100 us（code1），RSNS=1。 */
    TEST_CHECK(BQ76940_Control_ComposeProtect1(true, 1U, 3U,
                                               &register_value) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(register_value == 0x8BU);
    /* datasheet OCD example：320 ms(0x5)+14.4 A code0x0A→0x5A；寄存器 bit
     * 定义 (delay<<4)|threshold 决定 0x5A。 */
    TEST_CHECK(BQ76940_Control_ComposeProtect2(5U, 0x0AU,
                                               &register_value) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(register_value == 0x5AU);
    /* TI example：UV 4 s(code1)+OV 2 s(code1)→0x50。 */
    TEST_CHECK(BQ76940_Control_ComposeProtect3(1U, 1U,
                                               &register_value) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(register_value == 0x50U);

    /* 非法 code fail closed，并保持 caller output。 */
    register_value = 0xA5U;
    TEST_CHECK(BQ76940_Control_ComposeProtect1(true, 4U, 0U,
                                               &register_value) ==
               BQ76940_STATUS_RANGE_ERROR);
    TEST_CHECK(register_value == 0xA5U);
    TEST_CHECK(BQ76940_Control_ComposeProtect1(true, 0U, 8U,
                                               &register_value) ==
               BQ76940_STATUS_RANGE_ERROR);
    TEST_CHECK(register_value == 0xA5U);
    TEST_CHECK(BQ76940_Control_ComposeProtect2(8U, 0U,
                                               &register_value) ==
               BQ76940_STATUS_RANGE_ERROR);
    TEST_CHECK(register_value == 0xA5U);
    TEST_CHECK(BQ76940_Control_ComposeProtect2(0U, 16U,
                                               &register_value) ==
               BQ76940_STATUS_RANGE_ERROR);
    TEST_CHECK(register_value == 0xA5U);
    TEST_CHECK(BQ76940_Control_ComposeProtect3(4U, 0U,
                                               &register_value) ==
               BQ76940_STATUS_RANGE_ERROR);
    TEST_CHECK(register_value == 0xA5U);
    TEST_CHECK(BQ76940_Control_ComposeProtect3(0U, 4U,
                                               &register_value) ==
               BQ76940_STATUS_RANGE_ERROR);
    TEST_CHECK(register_value == 0xA5U);
    TEST_CHECK(BQ76940_Control_ComposeProtect1(true, 0U, 0U, NULL) ==
               BQ76940_STATUS_INVALID_ARGUMENT);
    TEST_CHECK(BQ76940_Control_ComposeProtect2(0U, 0U, NULL) ==
               BQ76940_STATUS_INVALID_ARGUMENT);
    TEST_CHECK(BQ76940_Control_ComposeProtect3(0U, 0U, NULL) ==
               BQ76940_STATUS_INVALID_ARGUMENT);

    return failures;
}
