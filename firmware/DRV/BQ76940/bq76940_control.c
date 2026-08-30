#include "bq76940_control.h"

#include <stddef.h>

/* TI SLUSBK2I Rev.I Tables 8-9..8-11 threshold/delay；RSNS=0 为 lower range。 */

/* OCD 阈值表（mV）：编码范围 0x0..0xF。 */
static const uint16_t s_ocd_threshold_rsns1[BQ76940_CONTROL_OCD_THRESHOLD_COUNT] =
{
    17U, 22U, 28U, 33U, 39U, 44U, 50U, 56U,
    61U, 67U, 72U, 78U, 83U, 89U, 94U, 100U
};

static const uint16_t s_ocd_threshold_rsns0[BQ76940_CONTROL_OCD_THRESHOLD_COUNT] =
{
    8U, 11U, 14U, 17U, 19U, 22U, 25U, 28U,
    31U, 33U, 36U, 39U, 42U, 44U, 47U, 50U
};

/* OCD 延迟表（ms）：编码范围 0x0..0x7。 */
static const uint16_t s_ocd_delay_ms[BQ76940_CONTROL_OCD_DELAY_COUNT] =
{
    8U, 20U, 40U, 80U, 160U, 320U, 640U, 1280U
};

/* SCD 阈值表（mV）：编码范围 0x0..0x7。 */
static const uint16_t s_scd_threshold_rsns1[BQ76940_CONTROL_SCD_THRESHOLD_COUNT] =
{
    44U, 67U, 89U, 111U, 133U, 155U, 178U, 200U
};

static const uint16_t s_scd_threshold_rsns0[BQ76940_CONTROL_SCD_THRESHOLD_COUNT] =
{
    22U, 33U, 44U, 56U, 67U, 78U, 89U, 100U
};

/* SCD 延迟表（us）：编码范围 0x0..0x3。 */
static const uint16_t s_scd_delay_us[BQ76940_CONTROL_SCD_DELAY_COUNT] =
{
    70U, 100U, 200U, 400U
};

/* OV 延迟表（s）：编码范围 0x0..0x3。 */
static const uint8_t s_ov_delay_s[BQ76940_CONTROL_OV_DELAY_COUNT] =
{
    1U, 2U, 4U, 8U
};

/* UV 延迟表（s）：编码范围 0x0..0x3。 */
static const uint8_t s_uv_delay_s[BQ76940_CONTROL_UV_DELAY_COUNT] =
{
    1U, 4U, 8U, 16U
};

/* ------------------------------------------------------------------ */
/* OV / UV 跳闸寄存器编码。 */
/* ------------------------------------------------------------------ */

static BQ76940_Status_t BQ76940_Control_RequireCalibration(
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

/* 完整码 full_code=(target_mv-offset)×1000/gain；trip=(full>>4)&0xFF。 */
static BQ76940_Status_t BQ76940_Control_EncodeTrip(
    uint16_t target_mv,
    const BQ76940_Calibration_t *calibration,
    uint8_t msb_prefix,
    uint8_t lsb_preset,
    uint8_t *trip_value)
{
    int64_t numerator;
    int64_t full_code;
    uint16_t code;
    BQ76940_Status_t result;

    if (trip_value == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    result = BQ76940_Control_RequireCalibration(calibration);
    if (result != BQ76940_STATUS_OK)
    {
        return result;
    }

    /* 先移除 offset 再除 gain；64-bit 保证 mV→uV 放大不溢出。 */
    numerator = ((int64_t)target_mv - (int64_t)calibration->offset_mv) *
                1000LL;
    if (numerator < 0LL)
    {
        return BQ76940_STATUS_RANGE_ERROR;
    }
    full_code = numerator / (int64_t)calibration->gain_uv_per_lsb;
    if ((full_code < 0LL) || (full_code > 0x3FFFL))
    {
        return BQ76940_STATUS_RANGE_ERROR;
    }
    code = (uint16_t)full_code;

    /* 验证 14-bit full code 的固定 MSB prefix。 */
    if ((uint8_t)(code >> 12) != msb_prefix)
    {
        return BQ76940_STATUS_RANGE_ERROR;
    }
    /* register 只存中间 8 bit；4-bit LSB preset 在 decode 时重建。 */

    *trip_value = (uint8_t)((code >> 4) & 0xFFU);
    return BQ76940_STATUS_OK;
}

BQ76940_Status_t BQ76940_Control_EncodeOvTrip(
    uint16_t target_mv,
    const BQ76940_Calibration_t *calibration,
    uint8_t *trip_value)
{
    /* OV：高位前缀 MSB=10，低位预设 LSB=1000。 */
    return BQ76940_Control_EncodeTrip(target_mv, calibration,
                                      0x2U, 0x8U, trip_value);
}

BQ76940_Status_t BQ76940_Control_EncodeUvTrip(
    uint16_t target_mv,
    const BQ76940_Calibration_t *calibration,
    uint8_t *trip_value)
{
    /* UV：高位前缀 MSB=01，低位预设 LSB=0000。 */
    return BQ76940_Control_EncodeTrip(target_mv, calibration,
                                      0x1U, 0x0U, trip_value);
}

static uint16_t BQ76940_Control_DecodeTripMv(
    uint8_t trip_value,
    const BQ76940_Calibration_t *calibration,
    uint8_t msb_prefix,
    uint8_t lsb_preset)
{
    uint32_t full_code;
    int64_t microvolts;
    int64_t rounded_mv;

    full_code = (((uint32_t)msb_prefix << 12) |
                 ((uint32_t)trip_value << 4) |
                 (uint32_t)lsb_preset);
    microvolts = ((int64_t)full_code * calibration->gain_uv_per_lsb) +
                 ((int64_t)calibration->offset_mv * 1000LL);
    if (microvolts < 0LL)
    {
        return 0U;
    }
    rounded_mv = (microvolts + 500LL) / 1000LL;
    if (rounded_mv > 65535LL)
    {
        return 0U;
    }
    return (uint16_t)rounded_mv;
}

uint16_t BQ76940_Control_DecodeOvTripMv(
    uint8_t trip_value,
    const BQ76940_Calibration_t *calibration)
{
    if (BQ76940_Control_RequireCalibration(calibration) !=
        BQ76940_STATUS_OK)
    {
        return 0U;
    }
    return BQ76940_Control_DecodeTripMv(trip_value, calibration,
                                        0x2U, 0x8U);
}

uint16_t BQ76940_Control_DecodeUvTripMv(
    uint8_t trip_value,
    const BQ76940_Calibration_t *calibration)
{
    if (BQ76940_Control_RequireCalibration(calibration) !=
        BQ76940_STATUS_OK)
    {
        return 0U;
    }
    return BQ76940_Control_DecodeTripMv(trip_value, calibration,
                                        0x1U, 0x0U);
}

/* ------------------------------------------------------------------ */
/* OCD / SCD 离散表编码。 */
/* ------------------------------------------------------------------ */

static BQ76940_Status_t BQ76940_Control_SelectFromTable(
    const uint16_t *table,
    uint8_t count,
    uint16_t requested,
    uint8_t *code)
{
    uint8_t index;

    if (code == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    /*
     * 选择 value>=requested 的最小合法 code：离散硬件只能向更大的物理请求
     * 取整，且结果可预测；超上限拒绝，绝不静默钳位到最后一项。
     */
    for (index = 0U; index < count; ++index)
    {
        if (table[index] >= requested)
        {
            *code = index;
            return BQ76940_STATUS_OK;
        }
    }
    return BQ76940_STATUS_RANGE_ERROR;
}

BQ76940_Status_t BQ76940_Control_SelectOcdThreshold(
    uint16_t requested_mv,
    bool rsns,
    uint8_t *code)
{
    return BQ76940_Control_SelectFromTable(
        rsns ? s_ocd_threshold_rsns1 : s_ocd_threshold_rsns0,
        BQ76940_CONTROL_OCD_THRESHOLD_COUNT, requested_mv, code);
}

BQ76940_Status_t BQ76940_Control_SelectOcdDelayMs(
    uint16_t requested_ms,
    uint8_t *code)
{
    return BQ76940_Control_SelectFromTable(
        s_ocd_delay_ms, BQ76940_CONTROL_OCD_DELAY_COUNT,
        requested_ms, code);
}

BQ76940_Status_t BQ76940_Control_SelectScdThreshold(
    uint16_t requested_mv,
    bool rsns,
    uint8_t *code)
{
    return BQ76940_Control_SelectFromTable(
        rsns ? s_scd_threshold_rsns1 : s_scd_threshold_rsns0,
        BQ76940_CONTROL_SCD_THRESHOLD_COUNT, requested_mv, code);
}

BQ76940_Status_t BQ76940_Control_SelectScdDelayUs(
    uint16_t requested_us,
    uint8_t *code)
{
    return BQ76940_Control_SelectFromTable(
        s_scd_delay_us, BQ76940_CONTROL_SCD_DELAY_COUNT,
        requested_us, code);
}

BQ76940_Status_t BQ76940_Control_SelectOvDelayS(
    uint8_t requested_s,
    uint8_t *code)
{
    uint8_t index;

    if (code == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0U; index < BQ76940_CONTROL_OV_DELAY_COUNT; ++index)
    {
        if (s_ov_delay_s[index] >= requested_s)
        {
            *code = index;
            return BQ76940_STATUS_OK;
        }
    }
    return BQ76940_STATUS_RANGE_ERROR;
}

BQ76940_Status_t BQ76940_Control_SelectUvDelayS(
    uint8_t requested_s,
    uint8_t *code)
{
    uint8_t index;

    if (code == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0U; index < BQ76940_CONTROL_UV_DELAY_COUNT; ++index)
    {
        if (s_uv_delay_s[index] >= requested_s)
        {
            *code = index;
            return BQ76940_STATUS_OK;
        }
    }
    return BQ76940_STATUS_RANGE_ERROR;
}

BQ76940_Status_t BQ76940_Control_ComposeProtect1(
    bool rsns,
    uint8_t delay_code,
    uint8_t thresh_code,
    uint8_t *register_value)
{
    if (register_value == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    if ((delay_code >= BQ76940_CONTROL_SCD_DELAY_COUNT) ||
        (thresh_code >= BQ76940_CONTROL_SCD_THRESHOLD_COUNT))
    {
        return BQ76940_STATUS_RANGE_ERROR;
    }
    *register_value = (uint8_t)((rsns ? 0x80U : 0x00U) |
                                (delay_code << 3) | thresh_code);
    return BQ76940_STATUS_OK;
}

BQ76940_Status_t BQ76940_Control_ComposeProtect2(
    uint8_t delay_code,
    uint8_t thresh_code,
    uint8_t *register_value)
{
    if (register_value == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    if ((delay_code >= BQ76940_CONTROL_OCD_DELAY_COUNT) ||
        (thresh_code >= BQ76940_CONTROL_OCD_THRESHOLD_COUNT))
    {
        return BQ76940_STATUS_RANGE_ERROR;
    }
    *register_value = (uint8_t)((delay_code << 4) | thresh_code);
    return BQ76940_STATUS_OK;
}

BQ76940_Status_t BQ76940_Control_ComposeProtect3(
    uint8_t uv_delay_code,
    uint8_t ov_delay_code,
    uint8_t *register_value)
{
    if (register_value == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    if ((uv_delay_code >= BQ76940_CONTROL_UV_DELAY_COUNT) ||
        (ov_delay_code >= BQ76940_CONTROL_OV_DELAY_COUNT))
    {
        return BQ76940_STATUS_RANGE_ERROR;
    }
    *register_value = (uint8_t)((uv_delay_code << 6) |
                                (ov_delay_code << 4));
    return BQ76940_STATUS_OK;
}

/* ------------------------------------------------------------------ */
/* FET 位安全合成。 */
/* ------------------------------------------------------------------ */

uint8_t BQ76940_Control_SysCtrl2WithFets(uint8_t current_ctrl2,
                                         const BQ76940_FetRequest_t *request)
{
    uint8_t next;

    /*
     * 只保留 V1 contract 所需 CC_EN(bit6)。factory-test DELAY_DIS(bit7) 会绕过
     * protection delay，禁止从 readback 传播；CC_ONESHOT 与 reserved bits 也清零。
     * 缺失 request 按 fail-safe 双关处理。
     */
    next = (uint8_t)(current_ctrl2 & BQ76940_SYS_CTRL2_CC_EN_MASK);
    if (request == NULL)
    {
        return next;
    }
    if (request->chg == BQ76940_FET_DESIRE_ENABLE)
    {
        next |= 0x01U;   /* 设置 CHG_ON */
    }
    if (request->dsg == BQ76940_FET_DESIRE_ENABLE)
    {
        next |= 0x02U;   /* 设置 DSG_ON */
    }
    return next;
}

void BQ76940_Control_ObserveFets(uint8_t ctrl2,
                                 BQ76940_FetObserved_t *observed)
{
    if (observed == NULL)
    {
        return;
    }
    observed->chg_on = ((ctrl2 & 0x01U) != 0U);
    observed->dsg_on = ((ctrl2 & 0x02U) != 0U);
}

void BQ76940_Control_ApplyInhibits(const BQ76940_FetRequest_t *request,
                                   bool inhibit_chg,
                                   bool inhibit_dsg,
                                   BQ76940_FetRequest_t *effective)
{
    if (effective == NULL)
    {
        return;
    }
    if (request == NULL)
    {
        effective->chg = BQ76940_FET_DESIRE_DISABLE;
        effective->dsg = BQ76940_FET_DESIRE_DISABLE;
        return;
    }
    /* inhibit 的优先级高于 desire：任一安全 owner 都能单向撤销对应使能。 */
    effective->chg = (inhibit_chg) ? BQ76940_FET_DESIRE_DISABLE :
                     request->chg;
    effective->dsg = (inhibit_dsg) ? BQ76940_FET_DESIRE_DISABLE :
                     request->dsg;
}

/* ------------------------------------------------------------------ */
/* AFE 内部均衡通道映射。 */
/* ------------------------------------------------------------------ */

/* logical cell 1..13→CB1..8、CB10..13、CB15；跳过 short channel CB9/CB14。 */
static const uint8_t s_logical_cell_to_cb[BQ76940_CONTROL_LOGICAL_CELL_COUNT] =
{
    1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U,
    10U, 11U, 12U, 13U, 15U
};

uint8_t BQ76940_Control_CellBalBitOfLogicalCell(uint8_t logical_cell_index)
{
    if (logical_cell_index >= BQ76940_CONTROL_LOGICAL_CELL_COUNT)
    {
        return 0U;
    }
    return s_logical_cell_to_cb[logical_cell_index];
}

/* 将 CBx index 映射到三个 8-bit register 的局部 bit。 */
static bool BQ76940_Control_CellBalSetBit(uint8_t *bal1,
                                          uint8_t *bal2,
                                          uint8_t *bal3,
                                          uint8_t cb_bit)
{
    uint8_t *reg;

    if ((bal1 == NULL) || (bal2 == NULL) || (bal3 == NULL))
    {
        return false;
    }
    if ((cb_bit < 1U) || (cb_bit > 15U))
    {
        return false;
    }
    if (cb_bit <= 5U)
    {
        reg = bal1;
        *reg |= (uint8_t)(1U << (cb_bit - 1U));
    }
    else if (cb_bit <= 10U)
    {
        reg = bal2;
        *reg |= (uint8_t)(1U << (cb_bit - 6U));
    }
    else
    {
        reg = bal3;
        *reg |= (uint8_t)(1U << (cb_bit - 11U));
    }
    return true;
}

bool BQ76940_Control_ComposeCellBal(
    uint16_t balance_bitmap,
    uint8_t *bal1,
    uint8_t *bal2,
    uint8_t *bal3)
{
    uint8_t index;
    uint8_t cb_bit;
    uint16_t mask;

    if ((bal1 == NULL) || (bal2 == NULL) || (bal3 == NULL))
    {
        return false;
    }
    /* frozen one-cell API：只允许一个 logical cell。 */
    if (balance_bitmap == 0U)
    {
        *bal1 = 0U;
        *bal2 = 0U;
        *bal3 = 0U;
        return true;
    }
    if ((balance_bitmap & (balance_bitmap - 1U)) != 0U)
    {
        return false;   /* 设置了多个 bit */
    }
    if (balance_bitmap >= (1U << BQ76940_CONTROL_LOGICAL_CELL_COUNT))
    {
        return false;   /* bitmap 越界 */
    }

    *bal1 = 0U;
    *bal2 = 0U;
    *bal3 = 0U;
    for (index = 0U; index < BQ76940_CONTROL_LOGICAL_CELL_COUNT; ++index)
    {
        mask = (uint16_t)(1U << index);
        if ((balance_bitmap & mask) != 0U)
        {
            cb_bit = s_logical_cell_to_cb[index];
            return BQ76940_Control_CellBalSetBit(bal1, bal2, bal3, cb_bit);
        }
    }
    return false;
}

bool BQ76940_Control_ComposeCellBalPolicy(
    uint16_t balance_bitmap,
    uint8_t max_parallel_cells,
    bool adjacent_cells_permitted,
    uint8_t *bal1,
    uint8_t *bal2,
    uint8_t *bal3)
{
    uint8_t index;
    uint8_t selected;
    uint8_t cb_bit;
    uint16_t mask;

    if ((bal1 == NULL) || (bal2 == NULL) || (bal3 == NULL) ||
        (max_parallel_cells == 0U) ||
        (max_parallel_cells > BQ76940_CONTROL_LOGICAL_CELL_COUNT) ||
        ((balance_bitmap & (uint16_t)(~(
            (uint16_t)((1U << BQ76940_CONTROL_LOGICAL_CELL_COUNT) - 1U)))) != 0U) ||
        (!adjacent_cells_permitted &&
         ((balance_bitmap & (uint16_t)(balance_bitmap << 1U)) != 0U)))
    {
        return false;
    }

    /* 先清零输出；后续任何计数/映射失败都显式回到全关，禁止残留半成品。 */
    *bal1 = 0U;
    *bal2 = 0U;
    *bal3 = 0U;
    selected = 0U;
    for (index = 0U; index < BQ76940_CONTROL_LOGICAL_CELL_COUNT; ++index)
    {
        mask = (uint16_t)(1U << index);
        if ((balance_bitmap & mask) != 0U)
        {
            ++selected;
            if (selected > max_parallel_cells)
            {
                *bal1 = 0U;
                *bal2 = 0U;
                *bal3 = 0U;
                return false;
            }
            cb_bit = s_logical_cell_to_cb[index];
            if (!BQ76940_Control_CellBalSetBit(
                    bal1, bal2, bal3, cb_bit))
            {
                *bal1 = 0U;
                *bal2 = 0U;
                *bal3 = 0U;
                return false;
            }
        }
    }
    return true;
}

uint16_t BQ76940_Control_DecodeCellBal(uint8_t bal1,
                                       uint8_t bal2,
                                       uint8_t bal3)
{
    uint16_t bitmap;
    uint8_t index;
    uint8_t cb_bit;
    uint8_t reg_value;
    uint8_t bit_in_reg;

    /* 反解仍遍历 logical mapping，所以物理 CB9/CB14 即使置位也不会冒充电芯。 */
    bitmap = 0U;
    for (index = 0U; index < BQ76940_CONTROL_LOGICAL_CELL_COUNT; ++index)
    {
        cb_bit = s_logical_cell_to_cb[index];
        if (cb_bit <= 5U)
        {
            reg_value = bal1;
            bit_in_reg = (uint8_t)(cb_bit - 1U);
        }
        else if (cb_bit <= 10U)
        {
            reg_value = bal2;
            bit_in_reg = (uint8_t)(cb_bit - 6U);
        }
        else
        {
            reg_value = bal3;
            bit_in_reg = (uint8_t)(cb_bit - 11U);
        }
        if ((reg_value & (uint8_t)(1U << bit_in_reg)) != 0U)
        {
            bitmap |= (uint16_t)(1U << index);
        }
    }
    return bitmap;
}
