#include "test_phase3.h"

#include <stdbool.h>
#include <stdint.h>

#include "bq76940.h"

#define TEST_CHECK(expr_)          \
    do                             \
    {                              \
        if (!(expr_))              \
        {                          \
            ++failures;            \
        }                          \
    } while (0)

static bool CalibrationEquals(const BQ76940_Calibration_t *calibration,
                              uint16_t gain,
                              int16_t offset)
{
    return calibration->valid &&
           (calibration->gain_uv_per_lsb == gain) &&
           (calibration->offset_mv == offset);
}

uint32_t Test_Phase3_Decode(void)
{
    BQ76940_Calibration_t calibration;
    BQ76940_Status_t status;
    uint32_t failures;
    uint16_t cell_mv;

    failures = 0UL;
    TEST_CHECK(BQ76940_DecodeRaw14(0xFFU, 0xFFU) == 0x3FFFU);
    TEST_CHECK(BQ76940_DecodeRaw14(0xC0U, 0x00U) == 0x0000U);
    TEST_CHECK(BQ76940_DecodeRaw14(0x18U, 0x00U) == 0x1800U);
    TEST_CHECK(BQ76940_DecodeRaw14(0x1FU, 0x10U) == 0x1F10U);

    TEST_CHECK(BQ76940_DecodeSigned16(0x00U, 0x00U) == 0);
    TEST_CHECK(BQ76940_DecodeSigned16(0x7FU, 0xFFU) == 32767);
    TEST_CHECK(BQ76940_DecodeSigned16(0x80U, 0x00U) == -32768);
    TEST_CHECK(BQ76940_DecodeSigned16(0xFFU, 0xFFU) == -1);

    TEST_CHECK(BQ76940_DecodeCalibration(0x00U, 0x00U, 0x00U,
                                        &calibration) == BQ76940_STATUS_OK);
    TEST_CHECK(CalibrationEquals(&calibration, 365U, 0));
    TEST_CHECK(BQ76940_DecodeCalibration(0x00U, 0x01U, 0x00U,
                                        &calibration) == BQ76940_STATUS_OK);
    TEST_CHECK(CalibrationEquals(&calibration, 365U, 1));
    TEST_CHECK(BQ76940_DecodeCalibration(0x0CU, 0x7FU, 0xE0U,
                                        &calibration) == BQ76940_STATUS_OK);
    TEST_CHECK(CalibrationEquals(&calibration, 396U, 127));
    TEST_CHECK(BQ76940_DecodeCalibration(0x04U, 0x80U, 0xA0U,
                                        &calibration) == BQ76940_STATUS_OK);
    TEST_CHECK(CalibrationEquals(&calibration, 378U, -128));
    TEST_CHECK(BQ76940_DecodeCalibration(0x08U, 0x81U, 0x60U,
                                        &calibration) == BQ76940_STATUS_OK);
    TEST_CHECK(CalibrationEquals(&calibration, 384U, -127));
    TEST_CHECK(BQ76940_DecodeCalibration(0xF7U, 0xFFU, 0xBFU,
                                        &calibration) == BQ76940_STATUS_OK);
    TEST_CHECK(CalibrationEquals(&calibration, 378U, -1));

    calibration.gain_uv_per_lsb = 380U;
    calibration.offset_mv = 30;
    calibration.valid = true;
    status = BQ76940_ConvertCellRawToMv(0x1800U, &calibration, &cell_mv);
    TEST_CHECK((status == BQ76940_STATUS_OK) && (cell_mv == 2365U));
    status = BQ76940_ConvertCellRawToMv(0x1F10U, &calibration, &cell_mv);
    TEST_CHECK((status == BQ76940_STATUS_OK) && (cell_mv == 3052U));

    calibration.gain_uv_per_lsb = 365U;
    calibration.offset_mv = -128;
    status = BQ76940_ConvertCellRawToMv(0x1000U, &calibration, &cell_mv);
    TEST_CHECK((status == BQ76940_STATUS_OK) && (cell_mv == 1367U));

    calibration.gain_uv_per_lsb = 396U;
    calibration.offset_mv = 127;
    status = BQ76940_ConvertCellRawToMv(0x3FFFU, &calibration, &cell_mv);
    TEST_CHECK((status == BQ76940_STATUS_OK) && (cell_mv == 6615U));

    calibration.gain_uv_per_lsb = 380U;
    calibration.offset_mv = 0;
    status = BQ76940_ConvertCellRawToMv(0x0019U, &calibration, &cell_mv);
    TEST_CHECK((status == BQ76940_STATUS_OK) && (cell_mv == 10U));

    calibration.offset_mv = -128;
    cell_mv = 0xEEEEU;
    TEST_CHECK(BQ76940_ConvertCellRawToMv(0U, &calibration, &cell_mv) ==
               BQ76940_STATUS_RANGE_ERROR);
    TEST_CHECK(cell_mv == 0xEEEEU);
    calibration.valid = false;
    TEST_CHECK(BQ76940_ConvertCellRawToMv(1U, &calibration, &cell_mv) ==
               BQ76940_STATUS_CALIBRATION_INVALID);
    calibration.gain_uv_per_lsb = 380U;
    calibration.offset_mv = 0;
    calibration.valid = true;
    cell_mv = 0xEEEEU;
    TEST_CHECK(BQ76940_ConvertCellRawToMv(0x4000U, &calibration, &cell_mv) ==
               BQ76940_STATUS_RANGE_ERROR);
    TEST_CHECK(cell_mv == 0xEEEEU);
    TEST_CHECK(BQ76940_ConvertCellRawToMv(0U, NULL, &cell_mv) ==
               BQ76940_STATUS_INVALID_ARGUMENT);

    return failures;
}
