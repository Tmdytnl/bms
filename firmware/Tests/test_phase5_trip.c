#include "test_phase5.h"

#include <stdbool.h>

#include "bq76940_control.h"

/*
 * OV/UV trip golden vectors. Expected values are produced by the
 * independent Python oracle (tmp/golden_phase5.py), never by the C code.
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

    /* TI official example: OV 4.30V, GAIN=382, OFFSET=0 -> 0xBF. */
    trip = 0xEEU;
    TEST_CHECK(BQ76940_Control_EncodeOvTrip(4300U, &CAL_382_0, &trip) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(trip == 0xBFU);
    back = BQ76940_Control_DecodeOvTripMv(trip, &CAL_382_0);
    TEST_CHECK(back == 4300U);

    /* TI official example: UV 2.50V -> 0x99. */
    trip = 0xEEU;
    TEST_CHECK(BQ76940_Control_EncodeUvTrip(2500U, &CAL_382_0, &trip) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(trip == 0x99U);
    back = BQ76940_Control_DecodeUvTripMv(trip, &CAL_382_0);
    TEST_CHECK(back == 2500U);

    /* Reference defaults: OV 4.25V -> 0xB7, UV 2.80V -> 0xCA. */
    trip = 0xEEU;
    TEST_CHECK(BQ76940_Control_EncodeOvTrip(4250U, &CAL_382_0, &trip) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(trip == 0xB7U);
    back = BQ76940_Control_DecodeOvTripMv(trip, &CAL_382_0);
    TEST_CHECK(back == 4251U);   /* 4250.8 rounds to 4251 */

    trip = 0xEEU;
    TEST_CHECK(BQ76940_Control_EncodeUvTrip(2800U, &CAL_382_0, &trip) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(trip == 0xCAU);
    back = BQ76940_Control_DecodeUvTripMv(trip, &CAL_382_0);
    TEST_CHECK(back == 2799U);   /* 2799.2 rounds to 2799 */

    /* With OFFSET=+30 mV: OV 4.25V -> 0xB2, UV 2.80V -> 0xC5. */
    trip = 0xEEU;
    TEST_CHECK(BQ76940_Control_EncodeOvTrip(4250U, &CAL_382_30, &trip) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(trip == 0xB2U);

    trip = 0xEEU;
    TEST_CHECK(BQ76940_Control_EncodeUvTrip(2800U, &CAL_382_30, &trip) ==
               BQ76940_STATUS_OK);
    TEST_CHECK(trip == 0xC5U);

    /* MSB window violations -> RANGE_ERROR. */
    /* OV full code must have bits 13:12 = 10: e.g. 1.0V is below OV window
     * (full = 2617 = 0x0A39, MSB=00 -> error). */
    trip = 0xEEU;
    TEST_CHECK(BQ76940_Control_EncodeOvTrip(1000U, &CAL_382_0, &trip) ==
               BQ76940_STATUS_RANGE_ERROR);
    /* UV 4.5V full=11780=0x2E04, MSB=10 -> error (not UV window). */
    trip = 0xEEU;
    TEST_CHECK(BQ76940_Control_EncodeUvTrip(4500U, &CAL_382_0, &trip) ==
               BQ76940_STATUS_RANGE_ERROR);

    /* NULL output. */
    TEST_CHECK(BQ76940_Control_EncodeOvTrip(4250U, &CAL_382_0, NULL) ==
               BQ76940_STATUS_INVALID_ARGUMENT);
    TEST_CHECK(BQ76940_Control_EncodeUvTrip(2800U, &CAL_382_0, NULL) ==
               BQ76940_STATUS_INVALID_ARGUMENT);

    /* Invalid calibration. */
    {
        BQ76940_Calibration_t bad = { 100U, 0, false };
        trip = 0xEEU;
        TEST_CHECK(BQ76940_Control_EncodeOvTrip(4250U, &bad, &trip) ==
                   BQ76940_STATUS_CALIBRATION_INVALID);
        TEST_CHECK(BQ76940_Control_EncodeUvTrip(2800U, &bad, &trip) ==
                   BQ76940_STATUS_CALIBRATION_INVALID);
    }

    /* Decode with invalid calibration returns 0. */
    {
        BQ76940_Calibration_t bad = { 100U, 0, false };
        TEST_CHECK(BQ76940_Control_DecodeOvTripMv(0xBFU, &bad) == 0U);
        TEST_CHECK(BQ76940_Control_DecodeUvTripMv(0x99U, NULL) == 0U);
    }

    return failures;
}
