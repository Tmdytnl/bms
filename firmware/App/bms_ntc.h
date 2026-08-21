#ifndef BMS_NTC_H
#define BMS_NTC_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_types.h"

/*
 * Calibration-owned NTC point. This module intentionally contains no
 * production curve: the physical 10 kOhm NTC curve is not yet validated.
 */
typedef struct
{
    uint32_t resistance_ohm;
    BMS_TemperatureDeciC_t temperature_decic;
} BMS_NtcPoint_t;

/*
 * A valid NTC table has at least two points, strictly monotonic resistance,
 * and strictly monotonic temperature in the opposite direction. Either
 * table order is accepted.
 */
bool BMS_Ntc_ValidateTable(const BMS_NtcPoint_t *points,
                           uint16_t point_count);

/*
 * Piecewise-linear interpolation. Out-of-range resistance, an invalid table,
 * or NULL output returns false and leaves temperature_decic unchanged.
 */
bool BMS_Ntc_Interpolate(const BMS_NtcPoint_t *points,
                         uint16_t point_count,
                         uint32_t resistance_ohm,
                         BMS_TemperatureDeciC_t *temperature_decic);

#endif /* BMS_NTC_H */
