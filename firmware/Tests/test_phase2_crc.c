#include "test_phase2.h"

#include <stdint.h>

#include "crc8_bq76940.h"

#define TEST_CHECK(expr_)          \
    do                             \
    {                              \
        if (!(expr_))              \
        {                          \
            ++failures;            \
        }                          \
    } while (0)

uint32_t Test_Phase2_Crc(void)
{
    static const uint8_t check_text[] =
        { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    static const uint8_t continuous[] = { 0x00U, 0xFFU, 0xAAU, 0x55U };
    uint32_t failures;

    failures = 0UL;
    TEST_CHECK(BQ76940_CRC8_Calculate(check_text, sizeof(check_text)) ==
               0xF4U);
    TEST_CHECK(BQ76940_CRC8_FirstWrite(0x10U, 0x0BU, 0x19U) == 0x7AU);
    TEST_CHECK(BQ76940_CRC8_FirstWrite(0x10U, 0x04U, 0x18U) == 0xBEU);
    TEST_CHECK(BQ76940_CRC8_FirstWrite(0x10U, 0x04U, 0x10U) == 0x86U);
    TEST_CHECK(BQ76940_CRC8_NextByte(0x40U) == 0xC7U);
    TEST_CHECK(BQ76940_CRC8_NextByte(0x00U) == 0x00U);
    TEST_CHECK(BQ76940_CRC8_FirstRead(0x11U, 0x12U) == 0x3CU);
    TEST_CHECK(BQ76940_CRC8_NextByte(0x34U) == 0x8CU);
    TEST_CHECK(BQ76940_CRC8_NextByte(0x56U) == 0xA5U);
    TEST_CHECK(BQ76940_CRC8_NextByte(0xFFU) == 0xF3U);
    TEST_CHECK(BQ76940_CRC8_Calculate(continuous, sizeof(continuous)) ==
               0x1DU);
    TEST_CHECK(BQ76940_CRC8_Calculate(NULL, 1U) == 0x00U);
    return failures;
}
