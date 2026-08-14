#ifndef BQ76940_H
#define BQ76940_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "soft_i2c.h"

#define BQ76940_MAX_BLOCK_LENGTH          (32U)

typedef enum
{
    BQ76940_STATUS_OK = 0,
    BQ76940_STATUS_INVALID_ARGUMENT,
    BQ76940_STATUS_NOT_INITIALIZED,
    BQ76940_STATUS_I2C_TIMEOUT,
    BQ76940_STATUS_I2C_NACK,
    BQ76940_STATUS_I2C_ERROR,
    BQ76940_STATUS_CRC_MISMATCH,
    BQ76940_STATUS_CRC_REJECTED,
    BQ76940_STATUS_CALIBRATION_INVALID,
    BQ76940_STATUS_RANGE_ERROR
} BQ76940_Status_t;

typedef struct
{
    SoftI2C_t *bus;
    bool initialized;
} BQ76940_t;

typedef struct
{
    uint16_t gain_uv_per_lsb;
    int16_t offset_mv;
    bool valid;
} BQ76940_Calibration_t;

BQ76940_Status_t BQ76940_Init(BQ76940_t *device, SoftI2C_t *bus);
bool BQ76940_IsInitialized(const BQ76940_t *device);

BQ76940_Status_t BQ76940_WriteByte(BQ76940_t *device,
                                    uint8_t register_address,
                                    uint8_t value);
BQ76940_Status_t BQ76940_WriteBlock(BQ76940_t *device,
                                     uint8_t start_register,
                                     const uint8_t *data,
                                     size_t length);
BQ76940_Status_t BQ76940_ReadByte(BQ76940_t *device,
                                   uint8_t register_address,
                                   uint8_t *value);
BQ76940_Status_t BQ76940_ReadBlock(BQ76940_t *device,
                                    uint8_t start_register,
                                    uint8_t *data,
                                    size_t length);
BQ76940_Status_t BQ76940_ReadAdjacentU16(BQ76940_t *device,
                                         uint8_t high_register,
                                         uint16_t *value);

BQ76940_Status_t BQ76940_DecodeCalibration(uint8_t adc_gain1,
                                            uint8_t adc_offset,
                                            uint8_t adc_gain2,
                                            BQ76940_Calibration_t *calibration);
BQ76940_Status_t BQ76940_ReadCalibration(
    BQ76940_t *device,
    BQ76940_Calibration_t *calibration);

uint16_t BQ76940_DecodeRaw14(uint8_t high_byte, uint8_t low_byte);
int16_t BQ76940_DecodeSigned16(uint8_t high_byte, uint8_t low_byte);
BQ76940_Status_t BQ76940_ConvertCellRawToMv(
    uint16_t raw14,
    const BQ76940_Calibration_t *calibration,
    uint16_t *cell_mv);

#endif /* BQ76940_H */
