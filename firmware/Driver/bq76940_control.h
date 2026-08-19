#ifndef BQ76940_CONTROL_H
#define BQ76940_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_build_assert.h"
#include "bms_config.h"
#include "bq76940.h"
#include "bq76940_regs.h"

/*
 * BQ7694003 Protection Configuration / FET Arbitration / Internal
 * Balancing foundation (Phase 5).
 *
 * Official TI basis (BQ769x0 Datasheet SLUSBK2I Rev.I):
 *   - OV_TRIP: 8 bits of the 14-bit ADC reading, upper 2 MSB preset "10",
 *     lower 4 LSB preset "1000". trip = (full >> 4) & 0xFF with
 *     full = (V_mV - OFFSET_mV) * 1000 / GAIN_uV (SLUSBK2I 8.3.1.2.1).
 *   - UV_TRIP: same mapping with "01" / "0000" presets.
 *   - PROTECT1 (0x06): RSNS=bit7, SCD_D1:0=bits4-3 (70/100/200/400 us),
 *     SCD_T2:0=bits2-0 (RSNS=1: 44..200 mV).
 *   - PROTECT2 (0x07): OCD_D2:0=bits6-4 (8..1280 ms), OCD_T3:0=bits3-0
 *     (RSNS=1: 17..100 mV).
 *   - PROTECT3 (0x08): UV_D1:0=bits7-6 (1/4/8/16 s), OV_D1:0=bits5-4
 *     (1/2/4/8 s). Bits 3-0 are TI-internal debug, keep default.
 *   - SYS_CTRL1 (0x04): ADC_EN=bit4, TEMP_SEL=bit3, SHUT_A=bit2,
 *     SHUT_B=bit1.
 *   - SYS_CTRL2 (0x05): DELAY_DIS=bit7, CC_EN=bit6, DSG_ON=bit1,
 *     CHG_ON=bit0.
 *   - CELLBAL1 (0x01): CB1..CB5 (bits0-4); CELLBAL2 (0x02): CB6..CB10;
 *     CELLBAL3 (0x03): CB11..CB15.
 *
 * Phase 5 scope: pure register encoding + safe arbitration primitives.
 * It does NOT run a SYS_STAT service loop, ALERT/EXTI, ProtectTask,
 * SampleTask, or any RTOS object (Phase 6/7). No protection policy or
 * threshold is evaluated here; callers provide desired target values.
 *
 * Protection thresholds (4.2 V / 4.25 V / 3.0 V etc.) are REFERENCE
 * configuration only (spec E-08) and belong to the App layer; this driver
 * only converts a requested mV/uV target into the official register code.
 */

/* ------------------------------------------------------------------ */
/* OV / UV trip encoding (SLUSBK2I 8.3.1.2.1)                          */
/* ------------------------------------------------------------------ */

/*
 * Compute the 8-bit OV_TRIP register value for a requested cell voltage
 * (mV) using the official formula:
 *
 *   full_code = (target_mv - calibration.offset_mv) * 1000 / gain_uv
 *   trip      = (full_code >> 4) & 0xFF
 *
 * 64-bit intermediate, no float. Fails with BQ76940_STATUS_RANGE_ERROR
 * if calibration is invalid or the resulting full code falls outside the
 * OV "10" MSB window (i.e. the 14-bit code must have bits 13:12 == 10b).
 */
BQ76940_Status_t BQ76940_Control_EncodeOvTrip(
    uint16_t target_mv,
    const BQ76940_Calibration_t *calibration,
    uint8_t *trip_value);

/*
 * Same for UV_TRIP. The 14-bit full code must have bits 13:12 == 01b.
 */
BQ76940_Status_t BQ76940_Control_EncodeUvTrip(
    uint16_t target_mv,
    const BQ76940_Calibration_t *calibration,
    uint8_t *trip_value);

/*
 * Decode a programmed OV_TRIP register byte back to the millivolt
 * threshold implied by the official fixed-bit mapping, for verification:
 * rebuild the full 14-bit code from trip and the OV "10"/"1000" presets,
 * then invert the calibration formula. Returns the rounded mV value.
 */
uint16_t BQ76940_Control_DecodeOvTripMv(
    uint8_t trip_value,
    const BQ76940_Calibration_t *calibration);

uint16_t BQ76940_Control_DecodeUvTripMv(
    uint8_t trip_value,
    const BQ76940_Calibration_t *calibration);

/* ------------------------------------------------------------------ */
/* OCD / SCD encoding (SLUSBK2I 8.3.1.2.2/8.3.1.2.3)                  */
/* ------------------------------------------------------------------ */

/* Official RSNS=1 threshold tables (mV across SRP-SRN). */
#define BQ76940_CONTROL_OCD_THRESHOLD_COUNT       (16U)
#define BQ76940_CONTROL_SCD_THRESHOLD_COUNT       (8U)

/* Official delay tables. */
#define BQ76940_CONTROL_OCD_DELAY_COUNT           (8U)
#define BQ76940_CONTROL_SCD_DELAY_COUNT           (4U)
#define BQ76940_CONTROL_OV_DELAY_COUNT            (4U)
#define BQ76940_CONTROL_UV_DELAY_COUNT            (4U)

/*
 * Select the OCD threshold code for a requested sense voltage (mV) using
 * the "not below the requested value" policy (choose the smallest legal
 * code whose threshold is >= requested_mv). Fails RANGE_ERROR when the
 * request exceeds the table maximum. rsns selects the input range table:
 *   true  -> RSNS=1 upper range (17..100 mV)
 *   false -> RSNS=0 lower range (8..50 mV)
 */
BQ76940_Status_t BQ76940_Control_SelectOcdThreshold(
    uint16_t requested_mv,
    bool rsns,
    uint8_t *code);

BQ76940_Status_t BQ76940_Control_SelectOcdDelayMs(
    uint16_t requested_ms,
    uint8_t *code);

BQ76940_Status_t BQ76940_Control_SelectScdThreshold(
    uint16_t requested_mv,
    bool rsns,
    uint8_t *code);

BQ76940_Status_t BQ76940_Control_SelectScdDelayUs(
    uint16_t requested_us,
    uint8_t *code);

BQ76940_Status_t BQ76940_Control_SelectOvDelayS(
    uint8_t requested_s,
    uint8_t *code);

BQ76940_Status_t BQ76940_Control_SelectUvDelayS(
    uint8_t requested_s,
    uint8_t *code);

/*
 * Compose the full PROTECT1 (SCD) register byte from parts.
 * rsns: RSNS bit 7. delay_code: SCD_D1:0 (bits 4-3). thresh_code:
 * SCD_T2:0 (bits 2-0). Bits 6-5 stay 0 per datasheet. Invalid codes return
 * RANGE_ERROR and leave register_value unchanged.
 */
BQ76940_Status_t BQ76940_Control_ComposeProtect1(
    bool rsns,
    uint8_t delay_code,
    uint8_t thresh_code,
    uint8_t *register_value);

/* PROTECT2 (OCD): delay_code in bits 6-4, thresh_code in bits 3-0.
 * Invalid codes return RANGE_ERROR and leave register_value unchanged. */
BQ76940_Status_t BQ76940_Control_ComposeProtect2(
    uint8_t delay_code,
    uint8_t thresh_code,
    uint8_t *register_value);

/* PROTECT3: uv_delay_code bits 7-6, ov_delay_code bits 5-4.
 * Invalid codes return RANGE_ERROR and leave register_value unchanged. */
BQ76940_Status_t BQ76940_Control_ComposeProtect3(
    uint8_t uv_delay_code,
    uint8_t ov_delay_code,
    uint8_t *register_value);

/* ------------------------------------------------------------------ */
/* FET arbitration (errata H-04 single-writer rule)                    */
/* ------------------------------------------------------------------ */

/* Desired FET state bits, independent of register encoding. */
typedef enum
{
    BQ76940_FET_DESIRE_DISABLE = 0,
    BQ76940_FET_DESIRE_ENABLE = 1
} BQ76940_FetDesire_t;

typedef struct
{
    BQ76940_FetDesire_t chg;
    BQ76940_FetDesire_t dsg;
} BQ76940_FetRequest_t;

/* Observed register-level FET bits. */
typedef struct
{
    bool chg_on;
    bool dsg_on;
} BQ76940_FetObserved_t;

/*
 * Build a new SYS_CTRL2 byte from the current register value and the
 * desired CHG/DSG bits, preserving only CC_EN (bit 6). DELAY_DIS (bit 7) is
 * factory-test-only and is forced low in the production compositor, together
 * with CC_ONESHOT (bit 5) and reserved bits 4..2. NULL request is fail-safe
 * FET-off. This is the ONLY way control code may derive a new SYS_CTRL2 byte;
 * no module writes CHG/DSG bits directly.
 */
uint8_t BQ76940_Control_SysCtrl2WithFets(uint8_t current_ctrl2,
                                         const BQ76940_FetRequest_t *request);

/* Extract observed CHG/DSG bits from a SYS_CTRL2 register read. */
void BQ76940_Control_ObserveFets(uint8_t ctrl2,
                                 BQ76940_FetObserved_t *observed);

/*
 * Decide the safe effective FET state after a fault-inhibited request:
 * if either inhibit is set, the corresponding FET is forced OFF regardless
 * of the requested desire. A NULL request is fail-safe and writes both
 * effective desires OFF. A NULL effective output is ignored.
 */
void BQ76940_Control_ApplyInhibits(const BQ76940_FetRequest_t *request,
                                   bool inhibit_chg,
                                   bool inhibit_dsg,
                                   BQ76940_FetRequest_t *effective);

/* ------------------------------------------------------------------ */
/* Internal balancing (SLUSBK2I 8.3.1.1.1 + CELLBAL registers)         */
/* ------------------------------------------------------------------ */

/*
 * Explicit logical-cell -> CELLBAL bit mapping (spec §5.1):
 *   Cell1..8 -> CB1..CB8, Cell9..12 -> CB10..CB13, Cell13 -> CB15.
 * CB9 and CB14 are never used (short channels, same as VC mapping).
 * Returns 0 for out-of-range logical index.
 */
uint8_t BQ76940_Control_CellBalBitOfLogicalCell(uint8_t logical_cell_index);

/*
 * Compose the three CELLBAL1/2/3 register bytes from a desired balance
 * bitmap (bit n = logical cell n, n in 0..12). At most one bit may be
 * set (V1 policy: balance exactly one cell). Returns false if more than
 * one bit is set or the bitmap is out of range.
 */
bool BQ76940_Control_ComposeCellBal(
    uint16_t balance_bitmap,
    uint8_t *bal1,
    uint8_t *bal2,
    uint8_t *bal3);

/*
 * Decode the three CELLBAL register bytes back into a logical-cell
 * balance bitmap (inverse of ComposeCellBal). Illegal bits (CB9/CB14 or
 * reserved bits) are never reported.
 */
uint16_t BQ76940_Control_DecodeCellBal(uint8_t bal1,
                                       uint8_t bal2,
                                       uint8_t bal3);

#endif /* BQ76940_CONTROL_H */
