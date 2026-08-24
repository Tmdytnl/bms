#ifndef BQ76940_MEASUREMENT_H
#define BQ76940_MEASUREMENT_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_build_assert.h"
#include "bms_config.h"
#include "bq76940.h"
#include "bq76940_regs.h"

/*
 * BQ7694003 (13S) Measurement Layer.
 *
 * Official TI basis (BQ769x0 Datasheet SLUSBK2I Rev.I):
 *   - Cell voltage:       V(cell) = GAIN x ADC(cell) + OFFSET        (eq. 1)
 *                         GAIN in uV/LSB, OFFSET in mV.
 *   - Thermistor:         VTSX    = (ADC in Decimal) x 382 uV/LSB    (eq. 4)
 *                         RTS     = (10000 x VTSX) / (3.3 - VTSX)    (eq. 5)
 *   - CC:                 CC Reading (uV) = [16-bit 2's complement]
 *                                           x (8.44 uV/LSB)          (eq. 3)
 *   - Pack voltage (BAT): V(BAT)  = 4 x GAIN x ADC + (#Cells x OFFSET) (eq. 9)
 *                         BAT register = (sum of cell ADC) / 4.
 *
 * This module is synchronous and RTOS-free. It never writes the global
 * BMS snapshot (Phase 8 SampleTask owns snapshot publishing) and never
 * performs protection, ALERT, balancing, or FET control (Phase 5+).
 *
 * Measurement validity notes:
 *   - Cell voltages are only produced when calibration.valid == true.
 *   - A single 30-byte VC register window read gives one software read
 *     window. It does NOT mean all 13 cell ADCs converted simultaneously;
 *     the BQ76940 scheduler groups conversions (see SLUSBK2I 8.3.1.1.7).
 *   - After SHIP -> NORMAL the BQ76940 requires about 800 ms before the
 *     first cell data is valid (SLUSBK2I 8.3.1.1.3); a boot manager that
 *     enforces this wait is out of Phase 4 scope and is documented in the
 *     Phase 4 report.
 */

#define BQ76940_MEASUREMENT_CELL_COUNT         (BMS_CELL_COUNT)  /* 13 */
#define BQ76940_MEASUREMENT_VC_WINDOW_BYTES    (30U)  /* VC1_HI..VC15_LO */

/* BAT pack-voltage formula cell count. TI eq. (9) sums the full device
 * window ("summing 15 cells"); for the 13S reference configuration the
 * shorted channels contribute ~0 ADC, so the effective OFFSET term uses
 * BMS_CELL_COUNT. Board-level BAT accuracy remains HARDWARE VALIDATION
 * REQUIRED and BAT stays a diagnostic against the cell-summed pack
 * voltage (spec §18). */
#define BQ76940_MEASUREMENT_BAT_NUM_CELLS      (BMS_CELL_COUNT)

/* CC: 8.44 uV/LSB x 1000 -> 8440 nV/LSB (SLUSBK2I eq. 3, CCLSB typ). */
#define BQ76940_MEASUREMENT_CC_LSB_NV          (8440UL)

/* TS: 382 uV/LSB (SLUSBK2I eq. 4). */
#define BQ76940_MEASUREMENT_TS_UV_PER_LSB      (382UL)

/* TS reference pull-up and REGOUT (SLUSBK2I eq. 5). */
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
 * Explicit 13S logical-cell -> VC channel mapping. Logical cells 1..13 map
 * to VC channels 1..8, 10..13, 15; VC9 and VC14 are never exposed as a
 * logical cell (SLUSBK2I Table 9-4 "13 Cells" configuration).
 *
 * logical_cell_index: 0..12 (logical cell 1..13). Returns 0 for an
 * out-of-range index so callers can detect invalid inputs.
 */
uint8_t BQ76940_Measurement_VcChannelOfLogicalCell(uint8_t logical_cell_index);

/*
 * Read all 13 logical cell voltages with ONE atomic 30-byte block read of
 * the VC1_HI..VC15_LO window (every data byte and CRC checked by the
 * Phase 3 transport), decode the 15 physical channels, apply the explicit
 * 13S mapping, and convert with the Phase 3 calibration formula.
 *
 * Transactional contract: the caller array is committed only after ALL 13
 * conversions succeed. Any I2C/CRC/decode/calibration/range failure leaves
 * cell_mv byte-for-byte unchanged.
 *
 * Requires calibration->valid == true (BQ76940_STATUS_CALIBRATION_INVALID
 * otherwise). cell_mv must point to BQ76940_MEASUREMENT_CELL_COUNT entries.
 */
BQ76940_Status_t BQ76940_ReadCellVoltages13(
    BQ76940_t *device,
    const BQ76940_Calibration_t *calibration,
    uint16_t cell_mv[BQ76940_MEASUREMENT_CELL_COUNT]);

/*
 * Read BAT_HI/BAT_LO as one atomic adjacent transaction and convert with
 * the official TI eq. (9):
 *
 *   V(BAT)[uV] = 4 x GAIN x BAT_raw + BAT_NUM_CELLS x OFFSET x 1000
 *   pack_mv    = V(BAT)[uV] / 1000, nonnegative nearest half-up rounding
 *
 * Uses 64-bit intermediates (no float). Requires valid calibration.
 * pack_mv unchanged on any failure.
 */
BQ76940_Status_t BQ76940_ReadPackVoltageMv(
    BQ76940_t *device,
    const BQ76940_Calibration_t *calibration,
    uint32_t *pack_mv);

/*
 * Read CC_HI/CC_LO as one atomic 2-byte transaction and return the
 * 16-bit two's-complement raw value (Phase 3 signed decode helper).
 * cc_raw unchanged on any failure.
 */
BQ76940_Status_t BQ76940_ReadCcRaw(BQ76940_t *device, int16_t *cc_raw);

/*
 * Convert a CC raw value into a reference current in milliamps:
 *
 *   I[mA] = polarity x (CC_raw x 8.44 uV/LSB) / (Rsense in m-ohm)
 *         = polarity x (CC_raw x 8440 nV/LSB) / (Rsense in u-ohm)
 *
 * 64-bit intermediate, truncating integer division (matches spec §19.4),
 * overflow-checked. Rsense is the reference Rsense in micro-ohm.
 *
 * polarity selects the reference hardware current direction:
 *   +1: positive CC raw -> positive current (charge)
 *   -1: inverted board polarity
 * The physical polarity of the reference board is NOT verified yet
 * (HARDWARE VALIDATION REQUIRED); this parameter makes the assumption
 * explicit instead of silently baking it in.
 */
BQ76940_Status_t BQ76940_ConvertCcRawToCurrentMa(
    int16_t cc_raw,
    uint32_t rsense_uohm,
    int8_t polarity,
    int32_t *current_ma);

/*
 * Read TS1_HI/TS1_LO as one atomic adjacent transaction and return the
 * 14-bit raw ADC reading (top 6 bits of HI, full LO byte).
 * ts1_raw14 unchanged on any failure.
 */
BQ76940_Status_t BQ76940_ReadTs1Raw(BQ76940_t *device,
                                    uint16_t *ts1_raw14);

/*
 * Convert a TS1 14-bit raw reading to thermistor resistance in ohm using
 * the official TI eq. (4)/(5):
 *
 *   VTSX[uV] = raw x 382 uV/LSB
 *   RTS[ohm] = (10000 x VTSX) / (3.3 V - VTSX)   [VTSX in uV, 3.3 V in uV]
 *
 * 64-bit intermediates, truncating division, range-checked: VTSX must be
 * below 3.3 V and the resulting resistance must fit uint32_t.
 *
 * Temperature conversion is deliberately delegated to bms_ntc.c so the
 * transport/measurement driver never embeds a product curve. SIM_POLICY_V1
 * supplies the current simulation table; production calibration still
 * requires an approved immutable NTC artifact.
 */
BQ76940_Status_t BQ76940_ConvertTs1RawToResistanceOhm(
    uint16_t ts1_raw14,
    uint32_t *resistance_ohm);

#endif /* BQ76940_MEASUREMENT_H */
