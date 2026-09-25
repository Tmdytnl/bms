#include "bsp_bq76940_measurement.h"

#include <string.h>

#include "bsp_bq76940_regs.h"

/*
 * 显式 13S logical-cell→VC table：1..8→VC1..8，9..12→VC10..13，13→VC15。
 * 跳过 VC9/VC14；index=logical_cell-1，绝不以算术假设 channel 连续。
 */
static const uint8_t s_logical_cell_to_vc[BQ76940_MEASUREMENT_CELL_COUNT] =
{
    1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U,
    10U, 11U, 12U, 13U, 15U
};

/* 把 13S 逻辑电芯序号映射到 BQ 的 VC 通道。 */
uint8_t BSP_BQ76940_Measurement_VcChannelOfLogicalCell(uint8_t logical_cell_index)
{
    if (logical_cell_index >= BQ76940_MEASUREMENT_CELL_COUNT)
    {
        return 0U;
    }
    return s_logical_cell_to_vc[logical_cell_index];
}

/* 检查 ADC 校准增益、偏移和有效标记后才允许换算。 */
static BQ76940_Status_t BSP_BQ76940_Measurement_RequireCalibration(
    const BQ76940_Calibration_t *calibration)
{
    if ((calibration == NULL) || !calibration->valid ||
        (calibration->gain_uv_per_lsb < BQ76940_ADC_GAIN_BASE_UV_PER_LSB) ||
        (calibration->gain_uv_per_lsb > BQ76940_ADC_GAIN_MAX_UV_PER_LSB) ||
        (calibration->offset_mv < -128) || (calibration->offset_mv > 127))
    {
        return BQ76940_STATUS_CALIBRATION_INVALID;
    }
    return BQ76940_STATUS_OK;
}

/* 按逻辑电芯映射读取 13 节电压，任一通道失败即拒绝整组。 */
BQ76940_Status_t BSP_BQ76940_ReadCellVoltages13(
    BQ76940_t *device,
    const BQ76940_Calibration_t *calibration,
    uint16_t cell_mv[BQ76940_MEASUREMENT_CELL_COUNT])
{
    /* 尚未正式发布的暂存值。 */
    uint8_t staged[BQ76940_MEASUREMENT_VC_WINDOW_BYTES];
    /* 各电芯原始 ADC 值换算后的毫伏值。 */
    uint16_t converted[BQ76940_MEASUREMENT_CELL_COUNT];
    /* AFE 测量寄存器读取结果，失败时不发布换算值。 */
    BQ76940_Status_t result;
    /* 当前换算的 AFE 测量通道索引。 */
    uint8_t index;
    /* 当前逻辑电芯映射到的 VC 测量通道。 */
    uint8_t vc_channel;
    /* 寄存器提供的 14 位原始值。 */
    uint16_t raw14;

    if (cell_mv == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    result = BSP_BQ76940_Measurement_RequireCalibration(calibration);
    if (result != BQ76940_STATUS_OK)
    {
        return result;
    }

    /*
     * VC1_HI..VC15_LO 用一次 30-byte transaction 读取，每个 byte 校验 CRC 后
     * 原子提交 block。这是同一 software read window，不代表 13 个 ADC 同时转换。
     */
    result = BSP_BQ76940_ReadBlock(device,
                               BQ76940_REG_VC1_HI,
                               staged,
                               BQ76940_MEASUREMENT_VC_WINDOW_BYTES);
    if (result != BQ76940_STATUS_OK)
    {
        return result;
    }

    for (index = 0U; index < BQ76940_MEASUREMENT_CELL_COUNT; ++index)
    {
        /* 查表跳过短接通道；converted 是第二级 staging，换算失败不污染 caller。 */
        vc_channel = s_logical_cell_to_vc[index];
        raw14 = BSP_BQ76940_DecodeRaw14(
            staged[(vc_channel - 1U) * 2U],
            staged[((vc_channel - 1U) * 2U) + 1U]);
        result = BSP_BQ76940_ConvertCellRawToMv(raw14, calibration,
                                            &converted[index]);
        if (result != BQ76940_STATUS_OK)
        {
            /* 任一 cell decode/convert 失败都中止整组 commit。 */
            return result;
        }
    }

    (void)memcpy(cell_mv, converted, sizeof(converted));
    return BQ76940_STATUS_OK;
}

/* 读取 BAT 通道并按校准换算为电池包诊断电压。 */
BQ76940_Status_t BSP_BQ76940_ReadPackVoltageMv(
    BQ76940_t *device,
    const BQ76940_Calibration_t *calibration,
    uint32_t *pack_mv)
{
    /* BQ76940 BAT 寄存器读取的原始电压编码。 */
    uint16_t bat_raw;
    /* 换算后的电压，单位 µV。 */
    int64_t microvolts;
    /* 量化前完成舍入的电压，单位毫伏。 */
    int64_t rounded_mv;
    /* AFE 测量寄存器读取结果，失败时不发布换算值。 */
    BQ76940_Status_t result;

    if (pack_mv == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    result = BSP_BQ76940_Measurement_RequireCalibration(calibration);
    if (result != BQ76940_STATUS_OK)
    {
        return result;
    }

    result = BSP_BQ76940_ReadAdjacentU16(device, BQ76940_REG_BAT_HI, &bat_raw);
    if (result != BQ76940_STATUS_OK)
    {
        return result;
    }

    /*
     * TI eq.9：BAT register 保存 cell ADC sum/4，因此乘 4 恢复求和尺度；
     * OFFSET 单位 mV，GAIN 单位 uV/LSB。
     */
    microvolts = ((int64_t)4 * (int64_t)calibration->gain_uv_per_lsb *
                  (int64_t)bat_raw) +
                 ((int64_t)BQ76940_MEASUREMENT_BAT_NUM_CELLS *
                  (int64_t)calibration->offset_mv * 1000LL);
    if (microvolts < 0LL)
    {
        return BQ76940_STATUS_RANGE_ERROR;
    }
    rounded_mv = (microvolts + 500LL) / 1000LL;
    if (rounded_mv > (int64_t)UINT32_MAX)
    {
        return BQ76940_STATUS_RANGE_ERROR;
    }

    *pack_mv = (uint32_t)rounded_mv;
    return BQ76940_STATUS_OK;
}

/* 读取库仑计的有符号原始值，不在驱动层管理事件生命周期。 */
BQ76940_Status_t BSP_BQ76940_ReadCcRaw(BQ76940_t *device, int16_t *cc_raw)
{
    /* 尚未换算的寄存器原始值。 */
    uint16_t raw;
    /* AFE 测量寄存器读取结果，失败时不发布换算值。 */
    BQ76940_Status_t result;

    if (cc_raw == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    result = BSP_BQ76940_ReadAdjacentU16(device, BQ76940_REG_CC_HI, &raw);
    if (result != BQ76940_STATUS_OK)
    {
        return result;
    }
    /* CC 的方向信息在二补数符号位中，此层只解码，不擅自套用板级 polarity。 */
    *cc_raw = BSP_BQ76940_DecodeSigned16((uint8_t)(raw >> 8),
                                     (uint8_t)(raw & 0xFFU));
    return BQ76940_STATUS_OK;
}

/* 按采样电阻和电流极性把 CC 原始码换算为 mA。 */
BQ76940_Status_t BSP_BQ76940_ConvertCcRawToCurrentMa(
    int16_t cc_raw,
    uint32_t rsense_uohm,
    int8_t polarity,
    int32_t *current_ma)
{
    /* 当前比例计算的分子。 */
    int64_t numerator;
    /* 换算后的电流，单位 mA。 */
    int64_t milliamps;

    if (current_ma == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    if ((rsense_uohm == 0U) || (rsense_uohm > 100000000UL))
    {
        return BQ76940_STATUS_RANGE_ERROR;
    }
    if ((polarity != 1) && (polarity != -1))
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }

    /* CC raw×8440 nV/LSB÷Rsense_uohm，64-bit numerator 覆盖完整 signed16 范围。 */
    numerator = (int64_t)cc_raw * (int64_t)BQ76940_MEASUREMENT_CC_LSB_NV;
    milliamps = (numerator * (int64_t)polarity) / (int64_t)rsense_uohm;
    if ((milliamps < INT32_MIN) || (milliamps > INT32_MAX))
    {
        return BQ76940_STATUS_RANGE_ERROR;
    }

    *current_ma = (int32_t)milliamps;
    return BQ76940_STATUS_OK;
}

/* 读取 TS1 的 14 位原始码供温度链使用。 */
BQ76940_Status_t BSP_BQ76940_ReadTs1Raw(BQ76940_t *device,
                                    uint16_t *ts1_raw14)
{
    /* 尚未换算的寄存器原始值。 */
    uint16_t raw;
    /* AFE 测量寄存器读取结果，失败时不发布换算值。 */
    BQ76940_Status_t result;

    if (ts1_raw14 == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    result = BSP_BQ76940_ReadAdjacentU16(device, BQ76940_REG_TS1_HI, &raw);
    if (result != BQ76940_STATUS_OK)
    {
        return result;
    }
    /* 与 cell 一样只取 HI 的低 6 bit；高位状态/保留位不能进入 ADC 值。 */
    *ts1_raw14 = BSP_BQ76940_DecodeRaw14((uint8_t)(raw >> 8),
                                     (uint8_t)(raw & 0xFFU));
    return BQ76940_STATUS_OK;
}

/* 把 TS1 原始码换算为 NTC 电阻，拒绝非法分母或范围。 */
BQ76940_Status_t BSP_BQ76940_ConvertTs1RawToResistanceOhm(
    uint16_t ts1_raw14,
    uint32_t *resistance_ohm)
{
    /* NTC 温度通道测量的微伏值。 */
    int64_t ts_uv;
    /* 当前比例计算的分母。 */
    int64_t denominator;
    /* NTC 分压计算得到的热敏电阻阻值。 */
    int64_t resistance;

    if (resistance_ohm == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    if (ts1_raw14 > BQ76940_CELL_RAW14_MASK)
    {
        return BQ76940_STATUS_RANGE_ERROR;
    }

    /* 热敏端电压 VTSX[uV] = raw×382 uV/LSB（公式 4）。 */
    ts_uv = (int64_t)ts1_raw14 *
            (int64_t)BQ76940_MEASUREMENT_TS_UV_PER_LSB;
    denominator = (int64_t)BQ76940_MEASUREMENT_TS_REGOUT_UV - ts_uv;
    if (denominator <= 0LL)
    {
        /* VTSX>=3.3 V 时 divider denominator 非正，超出 thermistor model。 */
        return BQ76940_STATUS_RANGE_ERROR;
    }

    /* RTS[ohm]=(10000×VTSX)/(3.3 V−VTSX)，电压统一使用 uV（eq.5）。 */
    resistance = ((int64_t)BQ76940_MEASUREMENT_TS_PULLUP_OHM * ts_uv) /
                 denominator;
    if ((resistance < 0LL) || (resistance > (int64_t)UINT32_MAX))
    {
        return BQ76940_STATUS_RANGE_ERROR;
    }

    *resistance_ohm = (uint32_t)resistance;
    return BQ76940_STATUS_OK;
}
