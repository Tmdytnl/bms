#include "bq76940.h"

#include <string.h>

#include "bq76940_regs.h"
#include "crc8_bq76940.h"

static BQ76940_Status_t BQ76940_MapI2CStatus(SoftI2C_Status_t status)
{
    switch (status)
    {
        case SOFT_I2C_STATUS_OK:
            return BQ76940_STATUS_OK;
        case SOFT_I2C_STATUS_INVALID_ARGUMENT:
            return BQ76940_STATUS_INVALID_ARGUMENT;
        case SOFT_I2C_STATUS_NOT_INITIALIZED:
            return BQ76940_STATUS_NOT_INITIALIZED;
        case SOFT_I2C_STATUS_TIMEOUT:
        case SOFT_I2C_STATUS_SCL_STUCK_LOW:
        case SOFT_I2C_STATUS_SDA_STUCK_LOW:
            return BQ76940_STATUS_I2C_TIMEOUT;
        case SOFT_I2C_STATUS_NACK_ADDRESS:
        case SOFT_I2C_STATUS_NACK_DATA:
            return BQ76940_STATUS_I2C_NACK;
        case SOFT_I2C_STATUS_STATE_ERROR:
        case SOFT_I2C_STATUS_RECOVERY_FAILED:
        default:
            return BQ76940_STATUS_I2C_ERROR;
    }
}

static BQ76940_Status_t BQ76940_RequireReady(const BQ76940_t *device)
{
    if (device == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    if (!device->initialized || (device->bus == NULL) ||
        !SoftI2C_IsInitialized(device->bus))
    {
        return BQ76940_STATUS_NOT_INITIALIZED;
    }
    return BQ76940_STATUS_OK;
}

static BQ76940_Status_t BQ76940_StopAfterFailure(BQ76940_t *device,
                                                  BQ76940_Status_t primary)
{
    SoftI2C_Status_t stop_status;

    if ((device != NULL) && (device->bus != NULL))
    {
        stop_status = SoftI2C_Stop(device->bus);
        if (stop_status != SOFT_I2C_STATUS_OK)
        {
            /* The public CRC contract requires a detected mismatch to remain
             * distinguishable after the mandatory NACK-and-STOP attempt. */
            if (primary == BQ76940_STATUS_CRC_MISMATCH)
            {
                return primary;
            }
            return BQ76940_MapI2CStatus(stop_status);
        }
    }
    return primary;
}

BQ76940_Status_t BQ76940_Init(BQ76940_t *device, SoftI2C_t *bus)
{
    if ((device == NULL) || (bus == NULL))
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    if (!SoftI2C_IsInitialized(bus))
    {
        device->bus = NULL;
        device->initialized = false;
        return BQ76940_STATUS_NOT_INITIALIZED;
    }

    device->bus = bus;
    device->initialized = true;
    return BQ76940_STATUS_OK;
}

bool BQ76940_IsInitialized(const BQ76940_t *device)
{
    return (device != NULL) && device->initialized &&
           (device->bus != NULL) && SoftI2C_IsInitialized(device->bus);
}

BQ76940_Status_t BQ76940_WriteByte(BQ76940_t *device,
                                    uint8_t register_address,
                                    uint8_t value)
{
    return BQ76940_WriteBlock(device, register_address, &value, 1U);
}

BQ76940_Status_t BQ76940_WriteBlock(BQ76940_t *device,
                                     uint8_t start_register,
                                     const uint8_t *data,
                                     size_t length)
{
    BQ76940_Status_t result;
    SoftI2C_Status_t i2c_status;
    size_t index;
    uint8_t crc;

    result = BQ76940_RequireReady(device);
    if (result != BQ76940_STATUS_OK)
    {
        return result;
    }
    if ((data == NULL) || (length == 0U))
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    if ((length > BQ76940_MAX_BLOCK_LENGTH) ||
        ((length - 1U) > (size_t)(UINT8_MAX - start_register)))
    {
        return BQ76940_STATUS_RANGE_ERROR;
    }

    i2c_status = SoftI2C_Start(device->bus);
    if (i2c_status != SOFT_I2C_STATUS_OK)
    {
        result = BQ76940_MapI2CStatus(i2c_status);
        return BQ76940_StopAfterFailure(device, result);
    }
    i2c_status = SoftI2C_WriteAddress(device->bus,
                                      BQ76940_I2C_WIRE_WRITE);
    if (i2c_status != SOFT_I2C_STATUS_OK)
    {
        result = BQ76940_MapI2CStatus(i2c_status);
        goto failure;
    }
    i2c_status = SoftI2C_WriteByte(device->bus, start_register);
    if (i2c_status != SOFT_I2C_STATUS_OK)
    {
        result = BQ76940_MapI2CStatus(i2c_status);
        goto failure;
    }

    for (index = 0U; index < length; ++index)
    {
        i2c_status = SoftI2C_WriteByte(device->bus, data[index]);
        if (i2c_status != SOFT_I2C_STATUS_OK)
        {
            result = BQ76940_MapI2CStatus(i2c_status);
            goto failure;
        }
        crc = (index == 0U) ?
              BQ76940_CRC8_FirstWrite(BQ76940_I2C_WIRE_WRITE,
                                      start_register,
                                      data[index]) :
              BQ76940_CRC8_NextByte(data[index]);
        i2c_status = SoftI2C_WriteByte(device->bus, crc);
        if (i2c_status != SOFT_I2C_STATUS_OK)
        {
            result = (i2c_status == SOFT_I2C_STATUS_NACK_DATA) ?
                     BQ76940_STATUS_CRC_REJECTED :
                     BQ76940_MapI2CStatus(i2c_status);
            goto failure;
        }
    }

    i2c_status = SoftI2C_Stop(device->bus);
    if (i2c_status != SOFT_I2C_STATUS_OK)
    {
        /* ACK of all data/CRC bytes does not prove when the BQ7694003 applies
         * the register side effect. Without a successful final STOP this is
         * deliberately neither success nor definite rejection. */
        return BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS;
    }
    return BQ76940_STATUS_OK;

failure:
    return BQ76940_StopAfterFailure(device, result);
}

BQ76940_Status_t BQ76940_ReadByte(BQ76940_t *device,
                                   uint8_t register_address,
                                   uint8_t *value)
{
    uint8_t temporary;
    BQ76940_Status_t result;

    if (value == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    result = BQ76940_ReadBlock(device, register_address, &temporary, 1U);
    if (result == BQ76940_STATUS_OK)
    {
        *value = temporary;
    }
    return result;
}

BQ76940_Status_t BQ76940_ReadBlock(BQ76940_t *device,
                                    uint8_t start_register,
                                    uint8_t *data,
                                    size_t length)
{
    uint8_t staged[BQ76940_MAX_BLOCK_LENGTH];
    BQ76940_Status_t result;
    SoftI2C_MasterResponse_t response;
    SoftI2C_Status_t i2c_status;
    size_t index;
    uint8_t actual_crc;
    uint8_t expected_crc;

    result = BQ76940_RequireReady(device);
    if (result != BQ76940_STATUS_OK)
    {
        return result;
    }
    if ((data == NULL) || (length == 0U))
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    if ((length > BQ76940_MAX_BLOCK_LENGTH) ||
        ((length - 1U) > (size_t)(UINT8_MAX - start_register)))
    {
        return BQ76940_STATUS_RANGE_ERROR;
    }

    i2c_status = SoftI2C_Start(device->bus);
    if (i2c_status != SOFT_I2C_STATUS_OK)
    {
        result = BQ76940_MapI2CStatus(i2c_status);
        return BQ76940_StopAfterFailure(device, result);
    }
    i2c_status = SoftI2C_WriteAddress(device->bus,
                                      BQ76940_I2C_WIRE_WRITE);
    if (i2c_status != SOFT_I2C_STATUS_OK)
    {
        result = BQ76940_MapI2CStatus(i2c_status);
        goto failure;
    }
    i2c_status = SoftI2C_WriteByte(device->bus, start_register);
    if (i2c_status != SOFT_I2C_STATUS_OK)
    {
        result = BQ76940_MapI2CStatus(i2c_status);
        goto failure;
    }
    i2c_status = SoftI2C_RepeatedStart(device->bus);
    if (i2c_status != SOFT_I2C_STATUS_OK)
    {
        result = BQ76940_MapI2CStatus(i2c_status);
        goto failure;
    }
    i2c_status = SoftI2C_WriteAddress(device->bus,
                                      BQ76940_I2C_WIRE_READ);
    if (i2c_status != SOFT_I2C_STATUS_OK)
    {
        result = BQ76940_MapI2CStatus(i2c_status);
        goto failure;
    }

    for (index = 0U; index < length; ++index)
    {
        i2c_status = SoftI2C_ReadByteBegin(device->bus, &staged[index]);
        if (i2c_status != SOFT_I2C_STATUS_OK)
        {
            result = BQ76940_MapI2CStatus(i2c_status);
            goto failure;
        }
        i2c_status = SoftI2C_SendReadResponse(device->bus,
                                              SOFT_I2C_MASTER_ACK);
        if (i2c_status != SOFT_I2C_STATUS_OK)
        {
            result = BQ76940_MapI2CStatus(i2c_status);
            goto failure;
        }
        i2c_status = SoftI2C_ReadByteBegin(device->bus, &actual_crc);
        if (i2c_status != SOFT_I2C_STATUS_OK)
        {
            result = BQ76940_MapI2CStatus(i2c_status);
            goto failure;
        }

        expected_crc = (index == 0U) ?
                       BQ76940_CRC8_FirstRead(BQ76940_I2C_WIRE_READ,
                                             staged[index]) :
                       BQ76940_CRC8_NextByte(staged[index]);
        if (actual_crc != expected_crc)
        {
            result = BQ76940_STATUS_CRC_MISMATCH;
            i2c_status = SoftI2C_SendReadResponse(device->bus,
                                                  SOFT_I2C_MASTER_NACK);
            if (i2c_status != SOFT_I2C_STATUS_OK)
            {
                goto failure;
            }
            goto failure;
        }

        response = ((index + 1U) < length) ? SOFT_I2C_MASTER_ACK :
                                             SOFT_I2C_MASTER_NACK;
        i2c_status = SoftI2C_SendReadResponse(device->bus, response);
        if (i2c_status != SOFT_I2C_STATUS_OK)
        {
            result = BQ76940_MapI2CStatus(i2c_status);
            goto failure;
        }
    }

    i2c_status = SoftI2C_Stop(device->bus);
    if (i2c_status != SOFT_I2C_STATUS_OK)
    {
        return BQ76940_MapI2CStatus(i2c_status);
    }
    (void)memcpy(data, staged, length);
    return BQ76940_STATUS_OK;

failure:
    return BQ76940_StopAfterFailure(device, result);
}

BQ76940_Status_t BQ76940_ReadAdjacentU16(BQ76940_t *device,
                                         uint8_t high_register,
                                         uint16_t *value)
{
    uint8_t bytes[2];
    BQ76940_Status_t result;

    if (value == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    result = BQ76940_ReadBlock(device, high_register, bytes, 2U);
    if (result == BQ76940_STATUS_OK)
    {
        *value = (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
    }
    return result;
}

BQ76940_Status_t BQ76940_DecodeCalibration(uint8_t adc_gain1,
                                            uint8_t adc_offset,
                                            uint8_t adc_gain2,
                                            BQ76940_Calibration_t *calibration)
{
    BQ76940_Calibration_t decoded;
    uint8_t trim;

    if (calibration == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }

    trim = (uint8_t)(((adc_gain1 & BQ76940_ADCGAIN1_MASK) << 1) |
                     ((adc_gain2 & BQ76940_ADCGAIN2_MASK) >> 5));
    decoded.gain_uv_per_lsb =
        (uint16_t)(BQ76940_ADC_GAIN_BASE_UV_PER_LSB + trim);
    decoded.offset_mv = ((adc_offset & 0x80U) != 0U) ?
                        (int16_t)((int16_t)adc_offset - 256) :
                        (int16_t)adc_offset;
    decoded.valid = (decoded.gain_uv_per_lsb >=
                     BQ76940_ADC_GAIN_BASE_UV_PER_LSB) &&
                    (decoded.gain_uv_per_lsb <=
                     BQ76940_ADC_GAIN_MAX_UV_PER_LSB);
    if (!decoded.valid)
    {
        return BQ76940_STATUS_CALIBRATION_INVALID;
    }

    *calibration = decoded;
    return BQ76940_STATUS_OK;
}

BQ76940_Status_t BQ76940_ReadCalibration(
    BQ76940_t *device,
    BQ76940_Calibration_t *calibration)
{
    BQ76940_Calibration_t decoded;
    BQ76940_Status_t result;
    uint8_t gain1;
    uint8_t gain2;
    uint8_t offset;

    if (calibration == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    result = BQ76940_ReadByte(device, BQ76940_REG_ADCGAIN1, &gain1);
    if (result != BQ76940_STATUS_OK)
    {
        return result;
    }
    result = BQ76940_ReadByte(device, BQ76940_REG_ADCOFFSET, &offset);
    if (result != BQ76940_STATUS_OK)
    {
        return result;
    }
    result = BQ76940_ReadByte(device, BQ76940_REG_ADCGAIN2, &gain2);
    if (result != BQ76940_STATUS_OK)
    {
        return result;
    }
    result = BQ76940_DecodeCalibration(gain1, offset, gain2, &decoded);
    if (result == BQ76940_STATUS_OK)
    {
        *calibration = decoded;
    }
    return result;
}

uint16_t BQ76940_DecodeRaw14(uint8_t high_byte, uint8_t low_byte)
{
    return (uint16_t)(((uint16_t)(high_byte & 0x3FU) << 8) | low_byte);
}

int16_t BQ76940_DecodeSigned16(uint8_t high_byte, uint8_t low_byte)
{
    uint16_t raw;

    raw = (uint16_t)(((uint16_t)high_byte << 8) | low_byte);
    return ((raw & 0x8000U) != 0U) ?
           (int16_t)((int32_t)raw - 65536L) : (int16_t)raw;
}

BQ76940_Status_t BQ76940_ConvertCellRawToMv(
    uint16_t raw14,
    const BQ76940_Calibration_t *calibration,
    uint16_t *cell_mv)
{
    int64_t microvolts;
    int64_t rounded_mv;

    if ((calibration == NULL) || (cell_mv == NULL))
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    if (!calibration->valid ||
        (calibration->gain_uv_per_lsb < BQ76940_ADC_GAIN_BASE_UV_PER_LSB) ||
        (calibration->gain_uv_per_lsb > BQ76940_ADC_GAIN_MAX_UV_PER_LSB) ||
        (calibration->offset_mv < -128) || (calibration->offset_mv > 127))
    {
        return BQ76940_STATUS_CALIBRATION_INVALID;
    }
    if (raw14 > BQ76940_CELL_RAW14_MASK)
    {
        return BQ76940_STATUS_RANGE_ERROR;
    }

    microvolts = ((int64_t)raw14 * calibration->gain_uv_per_lsb) +
                 ((int64_t)calibration->offset_mv * 1000LL);
    if (microvolts < 0LL)
    {
        return BQ76940_STATUS_RANGE_ERROR;
    }
    rounded_mv = (microvolts + 500LL) / 1000LL;
    if (rounded_mv > 65535LL)
    {
        return BQ76940_STATUS_RANGE_ERROR;
    }

    *cell_mv = (uint16_t)rounded_mv;
    return BQ76940_STATUS_OK;
}
