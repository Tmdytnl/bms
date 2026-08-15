#include "test_phase5.h"

#include <stdbool.h>

#include "bq76940_control.h"

/* Expected logical-cell -> CELLBAL bit mapping (spec §5.1, TI Table 8-4..6). */
static const uint8_t EXPECTED_CB[BMS_CELL_COUNT] =
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

uint32_t Test_Phase5_CellBal(void)
{
    uint32_t failures;
    uint8_t index;
    uint8_t cb;
    uint8_t bal1;
    uint8_t bal2;
    uint8_t bal3;
    uint16_t bitmap;

    failures = 0UL;

    /* Every logical cell maps to the expected CELLBAL bit. */
    for (index = 0U; index < BMS_CELL_COUNT; ++index)
    {
        cb = BQ76940_Control_CellBalBitOfLogicalCell(index);
        TEST_CHECK(cb == EXPECTED_CB[index]);
    }

    /* CB9 and CB14 never exposed. */
    for (index = 0U; index < BMS_CELL_COUNT; ++index)
    {
        cb = BQ76940_Control_CellBalBitOfLogicalCell(index);
        TEST_CHECK(cb != 9U);
        TEST_CHECK(cb != 14U);
    }

    /* Out of range -> 0. */
    TEST_CHECK(BQ76940_Control_CellBalBitOfLogicalCell(BMS_CELL_COUNT) == 0U);
    TEST_CHECK(BQ76940_Control_CellBalBitOfLogicalCell(0xFFU) == 0U);

    /* Compose: single-cell bitmaps. */
    bal1 = bal2 = bal3 = 0xFFU;
    TEST_CHECK(BQ76940_Control_ComposeCellBal(1U << 0U, &bal1, &bal2, &bal3));
    TEST_CHECK(bal1 == 0x01U && bal2 == 0x00U && bal3 == 0x00U);

    bal1 = bal2 = bal3 = 0xFFU;
    TEST_CHECK(BQ76940_Control_ComposeCellBal(1U << 5U, &bal1, &bal2, &bal3));
    TEST_CHECK(bal1 == 0x00U && bal2 == 0x01U && bal3 == 0x00U);   /* CB6 */

    bal1 = bal2 = bal3 = 0xFFU;
    TEST_CHECK(BQ76940_Control_ComposeCellBal(1U << 8U, &bal1, &bal2, &bal3));
    TEST_CHECK(bal1 == 0x00U && bal2 == 0x10U && bal3 == 0x00U);   /* CB10 */

    bal1 = bal2 = bal3 = 0xFFU;
    TEST_CHECK(BQ76940_Control_ComposeCellBal(1U << 12U, &bal1, &bal2, &bal3));
    TEST_CHECK(bal1 == 0x00U && bal2 == 0x00U && bal3 == 0x10U);   /* CB15 */

    /* Empty bitmap -> all zero. */
    bal1 = bal2 = bal3 = 0xFFU;
    TEST_CHECK(BQ76940_Control_ComposeCellBal(0U, &bal1, &bal2, &bal3));
    TEST_CHECK(bal1 == 0x00U && bal2 == 0x00U && bal3 == 0x00U);

    /* More than one bit -> false (V1 one-cell policy). */
    TEST_CHECK(!BQ76940_Control_ComposeCellBal(0x0003U, &bal1, &bal2, &bal3));

    /* Out of range bitmap -> false. */
    TEST_CHECK(!BQ76940_Control_ComposeCellBal(1U << 13U, &bal1, &bal2, &bal3));

    /* NULL outputs -> false. */
    TEST_CHECK(!BQ76940_Control_ComposeCellBal(1U << 0U, NULL, &bal2, &bal3));
    TEST_CHECK(!BQ76940_Control_ComposeCellBal(1U << 0U, &bal1, NULL, &bal3));
    TEST_CHECK(!BQ76940_Control_ComposeCellBal(1U << 0U, &bal1, &bal2, NULL));

    /* Decode: CB15 set -> bitmap 1<<12. */
    bitmap = BQ76940_Control_DecodeCellBal(0x00U, 0x00U, 0x10U);
    TEST_CHECK(bitmap == (1U << 12U));

    /* Decode: CB1 + CB10 -> cells 0 and 8. */
    bitmap = BQ76940_Control_DecodeCellBal(0x01U, 0x10U, 0x00U);
    TEST_CHECK(bitmap == ((1U << 0U) | (1U << 8U)));

    /* Decode round-trip: every single cell. */
    for (index = 0U; index < BMS_CELL_COUNT; ++index)
    {
        bal1 = bal2 = bal3 = 0U;
        TEST_CHECK(BQ76940_Control_ComposeCellBal((uint16_t)(1U << index),
                                                  &bal1, &bal2, &bal3));
        bitmap = BQ76940_Control_DecodeCellBal(bal1, bal2, bal3);
        TEST_CHECK(bitmap == (uint16_t)(1U << index));
    }

    /* All legal physical balance bits -> all 13 logical cells. */
    bitmap = BQ76940_Control_DecodeCellBal(0xFFU, 0xFFU, 0xFFU);
    TEST_CHECK(bitmap == (uint16_t)0x1FFFU);   /* cells 0..12 */

    /* Illegal physical bits (CB9/CB14/reserved) never map to a logical
     * cell: CB9 alone (bit 3 of CELLBAL2) and CB14 alone (bit 3 of
     * CELLBAL3) must decode to 0. */
    bitmap = BQ76940_Control_DecodeCellBal(0x00U, 0x08U, 0x00U);  /* CB9 */
    TEST_CHECK(bitmap == 0U);
    bitmap = BQ76940_Control_DecodeCellBal(0x00U, 0x00U, 0x08U);  /* CB14 */
    TEST_CHECK(bitmap == 0U);

    return failures;
}
