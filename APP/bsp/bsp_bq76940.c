#include "bsp_bq76940.h"

/*
 * BQ transport 负责 address/register/data/CRC/STOP 的完整 transaction。
 * read block 先写入局部 staging，所有 byte CRC 成功后才提交 caller output；
 * write 的最终 STOP 是 side-effect 边界，失败时返回 AMBIGUOUS 而非猜测或重放。
 */

#include <string.h>

#include "bsp_bq76940_regs.h"
#include "bsp_bq76940_crc8.h"

/* 把 SoftI2C 结果映射为 BQ76940 驱动状态。 */
static BQ76940_Status_t BSP_BQ76940_MapI2CStatus(SoftI2C_Status_t status)
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

/* 在寄存器事务前确认器件句柄与总线已初始化。 */
static BQ76940_Status_t BSP_BQ76940_RequireReady(const BQ76940_t *device)
{
    if (device == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    if (!device->initialized || (device->bus == NULL) ||
        !BSP_SoftI2C_IsInitialized(device->bus))
    {
        return BQ76940_STATUS_NOT_INITIALIZED;
    }
    return BQ76940_STATUS_OK;
}

/* 传输阶段失败后尝试 STOP，保留原始失败及提交不明语义。 */
static BQ76940_Status_t BSP_BQ76940_StopAfterFailure(BQ76940_t *device,
                                                  BQ76940_Status_t primary)
{
    /* STOP 阶段返回的状态码。 */
    SoftI2C_Status_t stop_status;

    if ((device != NULL) && (device->bus != NULL))
    {
        /* 即使主事务已失败也尽力生成 STOP，防止下一调用继承半截总线状态。 */
        stop_status = BSP_SoftI2C_Stop(device->bus);
        if (stop_status != SOFT_I2C_STATUS_OK)
        {
            /* CRC mismatch 在完成 mandatory NACK+STOP 后仍保留独立状态，便于诊断。 */
            if (primary == BQ76940_STATUS_CRC_MISMATCH)
            {
                return primary;
            }
            return BSP_BQ76940_MapI2CStatus(stop_status);
        }
    }
    return primary;
}

/* 绑定已初始化的软件 I2C 总线并建立器件句柄状态。 */
BQ76940_Status_t BSP_BQ76940_Init(BQ76940_t *device, SoftI2C_t *bus)
{
    if ((device == NULL) || (bus == NULL))
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    if (!BSP_SoftI2C_IsInitialized(bus))
    {
        device->bus = NULL;
        device->initialized = false;
        return BQ76940_STATUS_NOT_INITIALIZED;
    }

    device->bus = bus;
    device->initialized = true;
    return BQ76940_STATUS_OK;
}

/* 确认器件句柄和底层软件 I2C 均已初始化。 */
bool BSP_BQ76940_IsInitialized(const BQ76940_t *device)
{
    return (device != NULL) && device->initialized &&
           (device->bus != NULL) && BSP_SoftI2C_IsInitialized(device->bus);
}

/* 写入一个 AFE 寄存器，并保留 STOP 提交不明的状态。 */
BQ76940_Status_t BSP_BQ76940_WriteByte(BQ76940_t *device,
                                    uint8_t register_address,
                                    uint8_t value)
{
    return BSP_BQ76940_WriteBlock(device, register_address, &value, 1U);
}

/* 按器件 CRC 规则连续写寄存器数据并报告传输状态。 */
BQ76940_Status_t BSP_BQ76940_WriteBlock(BQ76940_t *device,
                                     uint8_t start_register,
                                     const uint8_t *data,
                                     size_t length)
{
    /* 当前 AFE I²C 事务结果，决定是否继续或收尾。 */
    BQ76940_Status_t result;
    /* 当前 I²C 事务返回的状态码。 */
    SoftI2C_Status_t i2c_status;
    /* 当前 I²C 事务的数据字节索引。 */
    size_t index;
    /* 当前 CRC 累积值。 */
    uint8_t crc;

    result = BSP_BQ76940_RequireReady(device);
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

    /* write 流程：START -> SLA+W -> register -> (data, CRC)* -> STOP。 */
    i2c_status = BSP_SoftI2C_Start(device->bus);
    if (i2c_status != SOFT_I2C_STATUS_OK)
    {
        result = BSP_BQ76940_MapI2CStatus(i2c_status);
        return BSP_BQ76940_StopAfterFailure(device, result);
    }
    i2c_status = BSP_SoftI2C_WriteAddress(device->bus,
                                      BQ76940_I2C_WIRE_WRITE);
    if (i2c_status != SOFT_I2C_STATUS_OK)
    {
        result = BSP_BQ76940_MapI2CStatus(i2c_status);
        /* 当前失败原因或失败状态。 */
        goto failure;
    }
    i2c_status = BSP_SoftI2C_WriteByte(device->bus, start_register);
    if (i2c_status != SOFT_I2C_STATUS_OK)
    {
        result = BSP_BQ76940_MapI2CStatus(i2c_status);
        /* 当前失败原因或失败状态。 */
        goto failure;
    }

    for (index = 0U; index < length; ++index)
    {
        i2c_status = BSP_SoftI2C_WriteByte(device->bus, data[index]);
        if (i2c_status != SOFT_I2C_STATUS_OK)
        {
            result = BSP_BQ76940_MapI2CStatus(i2c_status);
            /* 当前失败原因或失败状态。 */
            goto failure;
        }
        /* 首字节 CRC 包含 address+register；后续 CRC 按器件协议对单 data 计算。 */
        crc = (index == 0U) ?
              BSP_BQ76940_CRC8_FirstWrite(BQ76940_I2C_WIRE_WRITE,
                                      start_register,
                                      data[index]) :
              BSP_BQ76940_CRC8_NextByte(data[index]);
        i2c_status = BSP_SoftI2C_WriteByte(device->bus, crc);
        if (i2c_status != SOFT_I2C_STATUS_OK)
        {
            result = (i2c_status == SOFT_I2C_STATUS_NACK_DATA) ?
                     BQ76940_STATUS_CRC_REJECTED :
                     BSP_BQ76940_MapI2CStatus(i2c_status);
            /* 当前失败原因或失败状态。 */
            goto failure;
        }
    }

    i2c_status = BSP_SoftI2C_Stop(device->bus);
    if (i2c_status != SOFT_I2C_STATUS_OK)
    {
        /*
         * data/CRC 全 ACK 不证明 BQ7694003 在何时应用 register side effect；
         * final STOP 未成功时刻意返回既非 success 也非 definite rejection。
         */
        return BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS;
    }
    return BQ76940_STATUS_OK;

failure:
    return BSP_BQ76940_StopAfterFailure(device, result);
}

/* 读取一个 AFE 寄存器，失败时不把输出当作有效证据。 */
BQ76940_Status_t BSP_BQ76940_ReadByte(BQ76940_t *device,
                                   uint8_t register_address,
                                   uint8_t *value)
{
    /* 修改控制寄存器时使用的暂存字节。 */
    uint8_t temporary;
    /* 当前 AFE I²C 事务结果，决定是否继续或收尾。 */
    BQ76940_Status_t result;

    if (value == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    result = BSP_BQ76940_ReadBlock(device, register_address, &temporary, 1U);
    if (result == BQ76940_STATUS_OK)
    {
        *value = temporary;
    }
    return result;
}

/* 连续读取 AFE 寄存器并校验每组 CRC。 */
BQ76940_Status_t BSP_BQ76940_ReadBlock(BQ76940_t *device,
                                    uint8_t start_register,
                                    uint8_t *data,
                                    size_t length)
{
    /* 尚未正式发布的暂存值。 */
    uint8_t staged[BQ76940_MAX_BLOCK_LENGTH];
    /* 当前 AFE I²C 事务结果，决定是否继续或收尾。 */
    BQ76940_Status_t result;
    /* 软件 I²C 读字节后的主机应答方式。 */
    SoftI2C_MasterResponse_t response;
    /* 当前 I²C 事务返回的状态码。 */
    SoftI2C_Status_t i2c_status;
    /* 当前 I²C 事务的数据字节索引。 */
    size_t index;
    /* 从记录读取到的 CRC 值。 */
    uint8_t actual_crc;
    /* 依据数据重新计算的 CRC 值。 */
    uint8_t expected_crc;

    result = BSP_BQ76940_RequireReady(device);
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

    /* read 流程先用 SLA+W 选择寄存器，再 repeated START + SLA+R 连续读取。 */
    i2c_status = BSP_SoftI2C_Start(device->bus);
    if (i2c_status != SOFT_I2C_STATUS_OK)
    {
        result = BSP_BQ76940_MapI2CStatus(i2c_status);
        return BSP_BQ76940_StopAfterFailure(device, result);
    }
    i2c_status = BSP_SoftI2C_WriteAddress(device->bus,
                                      BQ76940_I2C_WIRE_WRITE);
    if (i2c_status != SOFT_I2C_STATUS_OK)
    {
        result = BSP_BQ76940_MapI2CStatus(i2c_status);
        /* 当前失败原因或失败状态。 */
        goto failure;
    }
    i2c_status = BSP_SoftI2C_WriteByte(device->bus, start_register);
    if (i2c_status != SOFT_I2C_STATUS_OK)
    {
        result = BSP_BQ76940_MapI2CStatus(i2c_status);
        /* 当前失败原因或失败状态。 */
        goto failure;
    }
    i2c_status = BSP_SoftI2C_RepeatedStart(device->bus);
    if (i2c_status != SOFT_I2C_STATUS_OK)
    {
        result = BSP_BQ76940_MapI2CStatus(i2c_status);
        /* 当前失败原因或失败状态。 */
        goto failure;
    }
    i2c_status = BSP_SoftI2C_WriteAddress(device->bus,
                                      BQ76940_I2C_WIRE_READ);
    if (i2c_status != SOFT_I2C_STATUS_OK)
    {
        result = BSP_BQ76940_MapI2CStatus(i2c_status);
        /* 当前失败原因或失败状态。 */
        goto failure;
    }

    for (index = 0U; index < length; ++index)
    {
        /* 每个 data 后紧跟一个 CRC；读取 data 后先 ACK，才能取得其 CRC byte。 */
        i2c_status = BSP_SoftI2C_ReadByteBegin(device->bus, &staged[index]);
        if (i2c_status != SOFT_I2C_STATUS_OK)
        {
            result = BSP_BQ76940_MapI2CStatus(i2c_status);
            /* 当前失败原因或失败状态。 */
            goto failure;
        }
        i2c_status = BSP_SoftI2C_SendReadResponse(device->bus,
                                              SOFT_I2C_MASTER_ACK);
        if (i2c_status != SOFT_I2C_STATUS_OK)
        {
            result = BSP_BQ76940_MapI2CStatus(i2c_status);
            /* 当前失败原因或失败状态。 */
            goto failure;
        }
        i2c_status = BSP_SoftI2C_ReadByteBegin(device->bus, &actual_crc);
        if (i2c_status != SOFT_I2C_STATUS_OK)
        {
            result = BSP_BQ76940_MapI2CStatus(i2c_status);
            /* 当前失败原因或失败状态。 */
            goto failure;
        }

        expected_crc = (index == 0U) ?
                       BSP_BQ76940_CRC8_FirstRead(BQ76940_I2C_WIRE_READ,
                                             staged[index]) :
                       BSP_BQ76940_CRC8_NextByte(staged[index]);
        if (actual_crc != expected_crc)
        {
            /* CRC 错误后主机 NACK 当前 CRC，终止继续传输，并仍完成 mandatory STOP。 */
            result = BQ76940_STATUS_CRC_MISMATCH;
            i2c_status = BSP_SoftI2C_SendReadResponse(device->bus,
                                                  SOFT_I2C_MASTER_NACK);
            if (i2c_status != SOFT_I2C_STATUS_OK)
            {
                /* 当前失败原因或失败状态。 */
                goto failure;
            }
            /* 当前失败原因或失败状态。 */
            goto failure;
        }

        /* 只有最后一个 CRC 返回 NACK，告诉 AFE 本次 block 到此结束。 */
        response = ((index + 1U) < length) ? SOFT_I2C_MASTER_ACK :
                                             SOFT_I2C_MASTER_NACK;
        i2c_status = BSP_SoftI2C_SendReadResponse(device->bus, response);
        if (i2c_status != SOFT_I2C_STATUS_OK)
        {
            result = BSP_BQ76940_MapI2CStatus(i2c_status);
            /* 当前失败原因或失败状态。 */
            goto failure;
        }
    }

    i2c_status = BSP_SoftI2C_Stop(device->bus);
    if (i2c_status != SOFT_I2C_STATUS_OK)
    {
        return BSP_BQ76940_MapI2CStatus(i2c_status);
    }
    /* STOP 确认后才把 staging 整体发布，调用者不会看到半新半旧 block。 */
    (void)memcpy(data, staged, length);
    return BQ76940_STATUS_OK;

failure:
    return BSP_BQ76940_StopAfterFailure(device, result);
}

/* 连续读取相邻的两个寄存器并按器件字节序组合 16 位原始值。 */
BQ76940_Status_t BSP_BQ76940_ReadAdjacentU16(BQ76940_t *device,
                                         uint8_t high_register,
                                         uint16_t *value)
{
    /* 连续读取的两个寄存器原始字节。 */
    uint8_t bytes[2];
    /* 当前 AFE I²C 事务结果，决定是否继续或收尾。 */
    BQ76940_Status_t result;

    if (value == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    result = BSP_BQ76940_ReadBlock(device, high_register, bytes, 2U);
    if (result == BQ76940_STATUS_OK)
    {
        *value = (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
    }
    return result;
}

/* 把器件校准寄存器解码为增益与偏移，并校验取值域。 */
BQ76940_Status_t BSP_BQ76940_DecodeCalibration(uint8_t adc_gain1,
                                            uint8_t adc_offset,
                                            uint8_t adc_gain2,
                                            BQ76940_Calibration_t *calibration)
{
    /* 从原始记录解码得到的值。 */
    BQ76940_Calibration_t decoded;
    /* AFE ADC 偏移寄存器的校准修正位。 */
    uint8_t trim;

    if (calibration == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }

    /* gain trim 被拆在两个非相邻寄存器中，必须掩码后按数据手册重新拼接。 */
    trim = (uint8_t)(((adc_gain1 & BQ76940_ADCGAIN1_MASK) << 1) |
                     ((adc_gain2 & BQ76940_ADCGAIN2_MASK) >> 5));
    decoded.gain_uv_per_lsb =
        (uint16_t)(BQ76940_ADC_GAIN_BASE_UV_PER_LSB + trim);
    /* ADCOFFSET 是 8-bit 二补数；显式扩展避免编译器 char signedness 差异。 */
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

/* 读取并解码器件 ADC 校准寄存器，非法取值不发布。 */
BQ76940_Status_t BSP_BQ76940_ReadCalibration(
    BQ76940_t *device,
    BQ76940_Calibration_t *calibration)
{
    /* 从原始记录解码得到的值。 */
    BQ76940_Calibration_t decoded;
    /* 当前 AFE I²C 事务结果，决定是否继续或收尾。 */
    BQ76940_Status_t result;
    /* ADCGAIN1 寄存器的原始增益位。 */
    uint8_t gain1;
    /* ADCGAIN2 寄存器的原始增益位。 */
    uint8_t gain2;
    /* 当前数据或寄存器字段的偏移量。 */
    uint8_t offset;

    if (calibration == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    /* 三次事务全部成功并解码后才写 caller，避免暴露拼到一半的校准。 */
    result = BSP_BQ76940_ReadByte(device, BQ76940_REG_ADCGAIN1, &gain1);
    if (result != BQ76940_STATUS_OK)
    {
        return result;
    }
    result = BSP_BQ76940_ReadByte(device, BQ76940_REG_ADCOFFSET, &offset);
    if (result != BQ76940_STATUS_OK)
    {
        return result;
    }
    result = BSP_BQ76940_ReadByte(device, BQ76940_REG_ADCGAIN2, &gain2);
    if (result != BQ76940_STATUS_OK)
    {
        return result;
    }
    result = BSP_BQ76940_DecodeCalibration(gain1, offset, gain2, &decoded);
    if (result == BQ76940_STATUS_OK)
    {
        *calibration = decoded;
    }
    return result;
}

/* 屏蔽高字节保留位并组合 BQ 的 14 位 ADC 原始码。 */
uint16_t BSP_BQ76940_DecodeRaw14(uint8_t high_byte, uint8_t low_byte)
{
    return (uint16_t)(((uint16_t)(high_byte & 0x3FU) << 8) | low_byte);
}

/* 把两个器件字节按补码解释为有符号原始值。 */
int16_t BSP_BQ76940_DecodeSigned16(uint8_t high_byte, uint8_t low_byte)
{
    /* 尚未换算的寄存器原始值。 */
    uint16_t raw;

    raw = (uint16_t)(((uint16_t)high_byte << 8) | low_byte);
    return ((raw & 0x8000U) != 0U) ?
           (int16_t)((int32_t)raw - 65536L) : (int16_t)raw;
}

/* 用当前校准把单体 ADC 原始码换算为 mV。 */
BQ76940_Status_t BSP_BQ76940_ConvertCellRawToMv(
    uint16_t raw14,
    const BQ76940_Calibration_t *calibration,
    uint16_t *cell_mv)
{
    /* 换算后的电压，单位 µV。 */
    int64_t microvolts;
    /* 量化前完成舍入的电压，单位毫伏。 */
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

    /* 先在 uV 域完成有符号 offset，再 half-up 到 mV，避免负值被无符号回绕。 */
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
