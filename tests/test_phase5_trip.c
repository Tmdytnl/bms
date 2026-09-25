#include "test_phase5.h"

#include <stdbool.h>

#include "bsp_bq76940_control.h"

/*
 * OV/UV trip golden vector；expected value 来自独立 Python oracle，不由被测 C 生成。
 */
static const BQ76940_Calibration_t CAL_382_0 = { 382U, 0, true };
static const BQ76940_Calibration_t CAL_382_30 = { 382U, 30, true };

#define TEST_CHECK(expr_)          \
    do                             \
    {                              \
        if (!(expr_))              \
        {                          \
            ++failures;            \
        }                          \
    } while (0)

uint32_t Test_Phase5_Trip(void)
{
    uint32_t failures;
    uint8_t trip;
    uint16_t back;

    failures = 0UL;

    /* TI official example：OV 4.30 V、GAIN=382、OFFSET=0→0xBF。 */
    trip = 0xEEU;
    TEST_CHECK(BSP_BQ76940_Control_EncodeOvTrip(4300U, &CAL_382_0, &trip) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(trip == 0xBFU);
    back = BSP_BQ76940_Control_DecodeOvTripMv(trip, &CAL_382_0);
    TEST_CHECK(back == 4300U);

    /* TI official example：UV 2.50 V→0x99。 */
    trip = 0xEEU;
    TEST_CHECK(BSP_BQ76940_Control_EncodeUvTrip(2500U, &CAL_382_0, &trip) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(trip == 0x99U);
    back = BSP_BQ76940_Control_DecodeUvTripMv(trip, &CAL_382_0);
    TEST_CHECK(back == 2500U);

    /* 默认 vector：OV 4.25 V→0xB7，UV 2.80 V→0xCA。 */
    trip = 0xEEU;
    TEST_CHECK(BSP_BQ76940_Control_EncodeOvTrip(4250U, &CAL_382_0, &trip) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(trip == 0xB7U);
    back = BSP_BQ76940_Control_DecodeOvTripMv(trip, &CAL_382_0);
    TEST_CHECK(back == 4251U);   /* 4250.8 舍入为 4251 */

    trip = 0xEEU;
    TEST_CHECK(BSP_BQ76940_Control_EncodeUvTrip(2800U, &CAL_382_0, &trip) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(trip == 0xCAU);
    back = BSP_BQ76940_Control_DecodeUvTripMv(trip, &CAL_382_0);
    TEST_CHECK(back == 2799U);   /* 2799.2 舍入为 2799 */

    /* OFFSET=+30 mV：OV 4.25 V→0xB2，UV 2.80 V→0xC5。 */
    trip = 0xEEU;
    TEST_CHECK(BSP_BQ76940_Control_EncodeOvTrip(4250U, &CAL_382_30, &trip) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(trip == 0xB2U);
    back = BSP_BQ76940_Control_DecodeOvTripMv(trip, &CAL_382_30);
    TEST_CHECK(back == 4250U);

    trip = 0xEEU;
    TEST_CHECK(BSP_BQ76940_Control_EncodeUvTrip(2800U, &CAL_382_30, &trip) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(trip == 0xC5U);
    back = BSP_BQ76940_Control_DecodeUvTripMv(trip, &CAL_382_30);
    TEST_CHECK(back == 2799U);

    /* MSB window 违规→RANGE_ERROR。 */
    /* OV full bits13:12 必须为 10；1.0 V 得到 MSB=00，必须失败。 */
    trip = 0xEEU;
    TEST_CHECK(BSP_BQ76940_Control_EncodeOvTrip(1000U, &CAL_382_0, &trip) ==
               BQ76940_STATUS_RANGE_ERROR);
    /* UV 4.5 V 得到 MSB=10，不在 UV window。 */
    trip = 0xEEU;
    TEST_CHECK(BSP_BQ76940_Control_EncodeUvTrip(4500U, &CAL_382_0, &trip) ==
               BQ76940_STATUS_RANGE_ERROR);

    /* NULL output。 */
    TEST_CHECK(BSP_BQ76940_Control_EncodeOvTrip(4250U, &CAL_382_0, NULL) ==
               BQ76940_STATUS_INVALID_ARGUMENT);
    TEST_CHECK(BSP_BQ76940_Control_EncodeUvTrip(2800U, &CAL_382_0, NULL) ==
               BQ76940_STATUS_INVALID_ARGUMENT);

    /* calibration 非法。 */
    {
        BQ76940_Calibration_t bad = { 100U, 0, false };
        trip = 0xEEU;
        TEST_CHECK(BSP_BQ76940_Control_EncodeOvTrip(4250U, &bad, &trip) ==
                   BQ76940_STATUS_CALIBRATION_INVALID);
        TEST_CHECK(BSP_BQ76940_Control_EncodeUvTrip(2800U, &bad, &trip) ==
                   BQ76940_STATUS_CALIBRATION_INVALID);
    }

    /* 非法 calibration decode 返回 0。 */
    {
        BQ76940_Calibration_t bad = { 100U, 0, false };
        TEST_CHECK(BSP_BQ76940_Control_DecodeOvTripMv(0xBFU, &bad) == 0U);
        TEST_CHECK(BSP_BQ76940_Control_DecodeUvTripMv(0x99U, NULL) == 0U);
    }
    {
        BQ76940_Calibration_t bad_gain = { 100U, 0, true };
        BQ76940_Calibration_t bad_offset = { 382U, 200, true };
        TEST_CHECK(BSP_BQ76940_Control_DecodeOvTripMv(0xBFU, &bad_gain) == 0U);
        TEST_CHECK(BSP_BQ76940_Control_DecodeUvTripMv(0x99U, &bad_offset) == 0U);
    }

    return failures;
}
