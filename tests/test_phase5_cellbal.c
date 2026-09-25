#include "test_phase5.h"

#include <stdbool.h>

#include "bsp_bq76940_control.h"

/* expected logical-cell→CELLBAL mapping（spec §5.1，TI Table 8-4..6）。 */
static const uint8_t EXPECTED_CB[BQ76940_CONTROL_LOGICAL_CELL_COUNT] =
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

    /* 每节 logical cell 都映射到 expected CELLBAL bit。 */
    for (index = 0U; index < BQ76940_CONTROL_LOGICAL_CELL_COUNT; ++index)
    {
        cb = BSP_BQ76940_Control_CellBalBitOfLogicalCell(index);
        TEST_CHECK(cb == EXPECTED_CB[index]);
    }

    /* CB9/CB14 永不暴露。 */
    for (index = 0U; index < BQ76940_CONTROL_LOGICAL_CELL_COUNT; ++index)
    {
        cb = BSP_BQ76940_Control_CellBalBitOfLogicalCell(index);
        TEST_CHECK(cb != 9U);
        TEST_CHECK(cb != 14U);
    }

    /* 越界返回 0。 */
    TEST_CHECK(BSP_BQ76940_Control_CellBalBitOfLogicalCell(BQ76940_CONTROL_LOGICAL_CELL_COUNT) == 0U);
    TEST_CHECK(BSP_BQ76940_Control_CellBalBitOfLogicalCell(0xFFU) == 0U);

    /* 组合 single-cell bitmap。 */
    bal1 = bal2 = bal3 = 0xFFU;
    TEST_CHECK(BSP_BQ76940_Control_ComposeCellBal(1U << 0U, &bal1, &bal2, &bal3));
    TEST_CHECK(bal1 == 0x01U && bal2 == 0x00U && bal3 == 0x00U);

    bal1 = bal2 = bal3 = 0xFFU;
    TEST_CHECK(BSP_BQ76940_Control_ComposeCellBal(1U << 5U, &bal1, &bal2, &bal3));
    TEST_CHECK(bal1 == 0x00U && bal2 == 0x01U && bal3 == 0x00U);   /* CB6 */

    bal1 = bal2 = bal3 = 0xFFU;
    TEST_CHECK(BSP_BQ76940_Control_ComposeCellBal(1U << 8U, &bal1, &bal2, &bal3));
    TEST_CHECK(bal1 == 0x00U && bal2 == 0x10U && bal3 == 0x00U);   /* CB10 */

    bal1 = bal2 = bal3 = 0xFFU;
    TEST_CHECK(BSP_BQ76940_Control_ComposeCellBal(1U << 12U, &bal1, &bal2, &bal3));
    TEST_CHECK(bal1 == 0x00U && bal2 == 0x00U && bal3 == 0x10U);   /* CB15 */

    /* 空 bitmap→全零。 */
    bal1 = bal2 = bal3 = 0xFFU;
    TEST_CHECK(BSP_BQ76940_Control_ComposeCellBal(0U, &bal1, &bal2, &bal3));
    TEST_CHECK(bal1 == 0x00U && bal2 == 0x00U && bal3 == 0x00U);

    /* 多于一个 bit→false（frozen one-cell policy）。 */
    TEST_CHECK(!BSP_BQ76940_Control_ComposeCellBal(0x0003U, &bal1, &bal2, &bal3));

    /* bitmap 越界→false。 */
    TEST_CHECK(!BSP_BQ76940_Control_ComposeCellBal(1U << 13U, &bal1, &bal2, &bal3));

    /* NULL output→false。 */
    TEST_CHECK(!BSP_BQ76940_Control_ComposeCellBal(1U << 0U, NULL, &bal2, &bal3));
    TEST_CHECK(!BSP_BQ76940_Control_ComposeCellBal(1U << 0U, &bal1, NULL, &bal3));
    TEST_CHECK(!BSP_BQ76940_Control_ComposeCellBal(1U << 0U, &bal1, &bal2, NULL));

    /* decode CB15→bitmap bit12。 */
    bitmap = BSP_BQ76940_Control_DecodeCellBal(0x00U, 0x00U, 0x10U);
    TEST_CHECK(bitmap == (1U << 12U));

    /* decode CB1+CB10→cell0/cell8。 */
    bitmap = BSP_BQ76940_Control_DecodeCellBal(0x01U, 0x10U, 0x00U);
    TEST_CHECK(bitmap == ((1U << 0U) | (1U << 8U)));

    /* 每节 cell 的 encode/decode round-trip。 */
    for (index = 0U; index < BQ76940_CONTROL_LOGICAL_CELL_COUNT; ++index)
    {
        bal1 = bal2 = bal3 = 0U;
        TEST_CHECK(BSP_BQ76940_Control_ComposeCellBal((uint16_t)(1U << index),
                                                  &bal1, &bal2, &bal3));
        bitmap = BSP_BQ76940_Control_DecodeCellBal(bal1, bal2, bal3);
        TEST_CHECK(bitmap == (uint16_t)(1U << index));
    }

    /* 全部合法 physical balance bit→13 个 logical cell。 */
    bitmap = BSP_BQ76940_Control_DecodeCellBal(0xFFU, 0xFFU, 0xFFU);
    TEST_CHECK(bitmap == (uint16_t)0x1FFFU);   /* cell 0..12 */

    /* 非法 CB9/CB14/reserved bit 不映射 logical cell，单独 decode 必须为 0。 */
    bitmap = BSP_BQ76940_Control_DecodeCellBal(0x00U, 0x08U, 0x00U);  /* CB9 */
    TEST_CHECK(bitmap == 0U);
    bitmap = BSP_BQ76940_Control_DecodeCellBal(0x00U, 0x00U, 0x08U);  /* CB14 */
    TEST_CHECK(bitmap == 0U);

    return failures;
}
