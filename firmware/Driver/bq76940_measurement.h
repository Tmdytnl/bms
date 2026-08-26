#ifndef BQ76940_MEASUREMENT_H
#define BQ76940_MEASUREMENT_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_build_assert.h"
#include "bms_config.h"
#include "bq76940.h"
#include "bq76940_regs.h"

/*
 * BQ7694003（13S）同步 measurement layer，公式来自 TI SLUSBK2I Rev.I：
 * cell 使用 GAIN×ADC+OFFSET（eq.1）；TS1 使用 382 uV/LSB 与 10 kΩ divider
 * （eq.4/5）；CC 是 signed16×8.44 uV/LSB（eq.3）；BAT 使用
 * 4×GAIN×ADC + cell-count×OFFSET（eq.9）。
 *
 * 本层无 RTOS、无全局 BMS snapshot 写入，也不执行 protection、ALERT、balance
 * 或 FET control。calibration.valid 是电压换算前置条件。一次 30-byte VC block
 * read 只保证软件 transaction 原子提交，不表示 13 个 ADC 同时转换；SHIP→NORMAL
 * 后首帧所需 settle 由 BMS_AfeStartup 在 scheduler 前保证。
 */

#define BQ76940_MEASUREMENT_CELL_COUNT         (BMS_CELL_COUNT)  /* 13 节逻辑电芯 */
#define BQ76940_MEASUREMENT_VC_WINDOW_BYTES    (30U)  /* VC1_HI..VC15_LO 连续窗口 */

/*
 * BAT eq.9 原本覆盖完整 device window；13S mapping 中 short channel 贡献近 0，
 * OFFSET 项使用 BMS_CELL_COUNT。BAT 只与 cell-summed pack voltage 做诊断交叉检查，
 * 不代替逐节安全计算。
 */
#define BQ76940_MEASUREMENT_BAT_NUM_CELLS      (BMS_CELL_COUNT)

/* CC：8.44 uV/LSB×1000 = 8440 nV/LSB，便于全整数换算。 */
#define BQ76940_MEASUREMENT_CC_LSB_NV          (8440UL)

/* TS：382 uV/LSB（SLUSBK2I eq.4）。 */
#define BQ76940_MEASUREMENT_TS_UV_PER_LSB      (382UL)

/* TS divider 的 reference pull-up 与 REGOUT（SLUSBK2I eq.5）。 */
#define BQ76940_MEASUREMENT_TS_PULLUP_OHM      (10000UL)
#define BQ76940_MEASUREMENT_TS_REGOUT_UV       (3300000UL)

BMS_BUILD_ASSERT(BQ76940_MEASUREMENT_CELL_COUNT == BMS_CELL_COUNT,
                 measurement_cell_count_matches_config);
BMS_BUILD_ASSERT(BQ76940_MEASUREMENT_CELL_COUNT == 13U,
                 measurement_cell_count_is_thirteen);
BMS_BUILD_ASSERT(BQ76940_MEASUREMENT_VC_WINDOW_BYTES ==
                     (BQ76940_REG_VC15_LO - BQ76940_REG_VC1_HI + 1U),
                 vc_window_matches_register_range);
BMS_BUILD_ASSERT(BQ76940_MEASUREMENT_VC_WINDOW_BYTES <=
                     BQ76940_MAX_BLOCK_LENGTH,
                 vc_window_fits_transport_block);
BMS_BUILD_ASSERT(BQ76940_MEASUREMENT_BAT_NUM_CELLS == BMS_CELL_COUNT,
                 bat_cell_count_matches_config);
BMS_BUILD_ASSERT(BQ76940_MEASUREMENT_CC_LSB_NV == 8440UL,
                 cc_lsb_matches_datasheet);

/*
 * 显式 13S logical-cell→VC mapping：Cell1..8→VC1..8，Cell9..12→VC10..13，
 * Cell13→VC15；VC9/VC14 是 short channel，绝不暴露为逻辑电芯。输入 index 0..12，
 * 越界返回 0，禁止用简单算术推导 channel。
 */
uint8_t BQ76940_Measurement_VcChannelOfLogicalCell(uint8_t logical_cell_index);

/*
 * 一次原子 30-byte block read 获取 VC1_HI..VC15_LO；transport 校验每个 data CRC，
 * 再解码 15 个物理 channel、应用显式 13S mapping 与 calibration。
 * 只有 13 节全部换算成功才一次提交 caller array；任一 I2C/CRC/decode/calibration/
 * range 失败都保持 cell_mv 逐 byte 不变。calibration->valid 必须为 true。
 */
BQ76940_Status_t BQ76940_ReadCellVoltages13(
    BQ76940_t *device,
    const BQ76940_Calibration_t *calibration,
    uint16_t cell_mv[BQ76940_MEASUREMENT_CELL_COUNT]);

/*
 * 原子读取 BAT_HI/LO，并按 eq.9 换算：
 * V(BAT)[uV]=4×GAIN×raw + BAT_NUM_CELLS×OFFSET×1000；pack_mv 采用非负 half-up。
 * 全程 64-bit integer、要求有效 calibration；失败保持输出不变。
 */
BQ76940_Status_t BQ76940_ReadPackVoltageMv(
    BQ76940_t *device,
    const BQ76940_Calibration_t *calibration,
    uint32_t *pack_mv);

/* 原子读取 CC_HI/LO 并解码 signed16 two's-complement；失败保持 cc_raw 不变。 */
BQ76940_Status_t BQ76940_ReadCcRaw(BQ76940_t *device, int16_t *cc_raw);

/*
 * CC raw→mA：I = polarity×(CC_raw×8440 nV/LSB)/Rsense_uohm。
 * 使用 64-bit intermediate、向零截断并检查 overflow。polarity=+1 表示 raw 正值
 * 为 charge，-1 表示 board sense polarity 反向；把方向作为显式参数而非隐藏常量。
 */
BQ76940_Status_t BQ76940_ConvertCcRawToCurrentMa(
    int16_t cc_raw,
    uint32_t rsense_uohm,
    int8_t polarity,
    int32_t *current_ma);

/* 原子读取 TS1_HI/LO，解码 HI 低 6 bit + LO 的 14-bit ADC；失败保持输出不变。 */
BQ76940_Status_t BQ76940_ReadTs1Raw(BQ76940_t *device,
                                    uint16_t *ts1_raw14);

/*
 * TS1 raw→thermistor resistance：VTSX=raw×382 uV/LSB；
 * RTS=(10000×VTSX)/(3.3 V−VTSX)。使用 64-bit、向零截断并检查 VTSX<3.3 V 与
 * uint32_t range。温度插值委托给 bms_ntc.c，使 transport/measurement 不嵌入曲线。
 */
BQ76940_Status_t BQ76940_ConvertTs1RawToResistanceOhm(
    uint16_t ts1_raw14,
    uint32_t *resistance_ohm);

#endif /* BQ76940_MEASUREMENT_H：include guard */
