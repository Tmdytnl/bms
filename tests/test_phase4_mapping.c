#include "test_phase4.h"

#include <stdbool.h>

#include "bsp_bq76940_measurement.h"
#include "bsp_bq76940_regs.h"

/*
 * 13S mapping golden test；expected logical→VC 来自 TI Table 9-4 与 spec §5，
 * 常量不由被测代码派生。
 */
static const uint8_t EXPECTED_VCS[BQ76940_MEASUREMENT_CELL_COUNT] =
{
    1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 10U, 11U, 12U, 13U, 15U
};

#define TEST_CHECK(expr_)          \
    do                             \
    {                              \
        if (!(expr_))              \
        {                          \
            ++failures;            \
        }                          \
    } while (0)

uint32_t Test_Phase4_Mapping(void)
{
    uint32_t failures;
    uint8_t index;
    uint8_t channel;
    uint8_t probe;

    failures = 0UL;

    TEST_CHECK(BQ76940_MEASUREMENT_CELL_COUNT == 13U);

    /* 每个 logical cell 精确映射一个 expected VC。 */
    for (index = 0U; index < BQ76940_MEASUREMENT_CELL_COUNT; ++index)
    {
        channel = BSP_BQ76940_Measurement_VcChannelOfLogicalCell(index);
        TEST_CHECK(channel == EXPECTED_VCS[index]);
    }

    /* expected table 本身不暴露 VC9/VC14。 */
    for (index = 0U; index < BQ76940_MEASUREMENT_CELL_COUNT; ++index)
    {
        TEST_CHECK(EXPECTED_VCS[index] != 9U);
        TEST_CHECK(EXPECTED_VCS[index] != 14U);
    }

    /* 实现 table 同样不暴露 VC9/VC14。 */
    for (index = 0U; index < BQ76940_MEASUREMENT_CELL_COUNT; ++index)
    {
        channel = BSP_BQ76940_Measurement_VcChannelOfLogicalCell(index);
        TEST_CHECK(channel != 9U);
        TEST_CHECK(channel != 14U);
    }

    /* physical VC9/VC14 register 存在，但禁止选择。 */
    TEST_CHECK(BQ76940_REG_VC9_HI == 0x1CU);
    TEST_CHECK(BQ76940_REG_VC14_HI == 0x26U);

    /* index 越界返回 0（invalid）。 */
    TEST_CHECK(BSP_BQ76940_Measurement_VcChannelOfLogicalCell(
                   BQ76940_MEASUREMENT_CELL_COUNT) == 0U);
    TEST_CHECK(BSP_BQ76940_Measurement_VcChannelOfLogicalCell(0xFFU) == 0U);

    /* required mapping 的精确 spot check。 */
    TEST_CHECK(BSP_BQ76940_Measurement_VcChannelOfLogicalCell(0U) == 1U);
    TEST_CHECK(BSP_BQ76940_Measurement_VcChannelOfLogicalCell(7U) == 8U);
    TEST_CHECK(BSP_BQ76940_Measurement_VcChannelOfLogicalCell(8U) == 10U);
    TEST_CHECK(BSP_BQ76940_Measurement_VcChannelOfLogicalCell(11U) == 13U);
    TEST_CHECK(BSP_BQ76940_Measurement_VcChannelOfLogicalCell(12U) == 15U);

    /* probe 所有 VC channel id 0..255。 */
    for (probe = 0U; probe < 255U; ++probe)
    {
        channel = BSP_BQ76940_Measurement_VcChannelOfLogicalCell(probe);
        if (probe < BQ76940_MEASUREMENT_CELL_COUNT)
        {
            TEST_CHECK(channel == EXPECTED_VCS[probe]);
        }
        else
        {
            TEST_CHECK(channel == 0U);
        }
    }

    return failures;
}
