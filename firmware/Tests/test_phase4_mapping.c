#include "test_phase4.h"

#include <stdbool.h>

#include "bq76940_measurement.h"
#include "bq76940_regs.h"

/*
 * 13S mapping golden tests. The expected logical->VC relation is fixed by
 * TI SLUSBK2I Table 9-4 "13 Cells" configuration and the project spec §5.
 * These constants are NOT derived from the code under test.
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

    /* Every logical cell maps to exactly one expected VC channel. */
    for (index = 0U; index < BQ76940_MEASUREMENT_CELL_COUNT; ++index)
    {
        channel = BQ76940_Measurement_VcChannelOfLogicalCell(index);
        TEST_CHECK(channel == EXPECTED_VCS[index]);
    }

    /* The expected table itself must never expose VC9 or VC14. */
    for (index = 0U; index < BQ76940_MEASUREMENT_CELL_COUNT; ++index)
    {
        TEST_CHECK(EXPECTED_VCS[index] != 9U);
        TEST_CHECK(EXPECTED_VCS[index] != 14U);
    }

    /* The implemented table must likewise never expose VC9 or VC14. */
    for (index = 0U; index < BQ76940_MEASUREMENT_CELL_COUNT; ++index)
    {
        channel = BQ76940_Measurement_VcChannelOfLogicalCell(index);
        TEST_CHECK(channel != 9U);
        TEST_CHECK(channel != 14U);
    }

    /* Physical VC9 and VC14 registers exist but must never be selected. */
    TEST_CHECK(BQ76940_REG_VC9_HI == 0x1CU);
    TEST_CHECK(BQ76940_REG_VC14_HI == 0x26U);

    /* Out-of-range index returns 0 (invalid). */
    TEST_CHECK(BQ76940_Measurement_VcChannelOfLogicalCell(
                   BQ76940_MEASUREMENT_CELL_COUNT) == 0U);
    TEST_CHECK(BQ76940_Measurement_VcChannelOfLogicalCell(0xFFU) == 0U);

    /* Exact spot checks of the required mapping. */
    TEST_CHECK(BQ76940_Measurement_VcChannelOfLogicalCell(0U) == 1U);
    TEST_CHECK(BQ76940_Measurement_VcChannelOfLogicalCell(7U) == 8U);
    TEST_CHECK(BQ76940_Measurement_VcChannelOfLogicalCell(8U) == 10U);
    TEST_CHECK(BQ76940_Measurement_VcChannelOfLogicalCell(11U) == 13U);
    TEST_CHECK(BQ76940_Measurement_VcChannelOfLogicalCell(12U) == 15U);

    /* Probe the full range of every possible VC channel id (0..255). */
    for (probe = 0U; probe < 255U; ++probe)
    {
        channel = BQ76940_Measurement_VcChannelOfLogicalCell(probe);
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
