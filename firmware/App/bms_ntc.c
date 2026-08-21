#include "bms_ntc.h"

#include <stddef.h>

static bool BMS_Ntc_IsAscendingResistance(const BMS_NtcPoint_t *points)
{
    return points[1].resistance_ohm > points[0].resistance_ohm;
}

bool BMS_Ntc_ValidateTable(const BMS_NtcPoint_t *points,
                           uint16_t point_count)
{
    bool resistance_ascending;
    uint16_t index;

    if ((points == NULL) || (point_count < 2U))
    {
        return false;
    }

    resistance_ascending = BMS_Ntc_IsAscendingResistance(points);
    for (index = 1U; index < point_count; ++index)
    {
        if (resistance_ascending)
        {
            if ((points[index].resistance_ohm <=
                 points[index - 1U].resistance_ohm) ||
                (points[index].temperature_decic >=
                 points[index - 1U].temperature_decic))
            {
                return false;
            }
        }
        else
        {
            if ((points[index].resistance_ohm >=
                 points[index - 1U].resistance_ohm) ||
                (points[index].temperature_decic <=
                 points[index - 1U].temperature_decic))
            {
                return false;
            }
        }
    }
    return true;
}

static bool BMS_Ntc_ResistanceInSegment(const BMS_NtcPoint_t *left,
                                        const BMS_NtcPoint_t *right,
                                        uint32_t resistance_ohm)
{
    if (left->resistance_ohm < right->resistance_ohm)
    {
        return (resistance_ohm >= left->resistance_ohm) &&
               (resistance_ohm <= right->resistance_ohm);
    }
    return (resistance_ohm <= left->resistance_ohm) &&
           (resistance_ohm >= right->resistance_ohm);
}

bool BMS_Ntc_Interpolate(const BMS_NtcPoint_t *points,
                         uint16_t point_count,
                         uint32_t resistance_ohm,
                         BMS_TemperatureDeciC_t *temperature_decic)
{
    uint16_t index;
    int64_t resistance_delta;
    int64_t resistance_offset;
    int64_t temperature_delta;
    int64_t interpolated;

    if ((temperature_decic == NULL) ||
        !BMS_Ntc_ValidateTable(points, point_count))
    {
        return false;
    }

    for (index = 1U; index < point_count; ++index)
    {
        if (BMS_Ntc_ResistanceInSegment(&points[index - 1U],
                                        &points[index],
                                        resistance_ohm))
        {
            resistance_delta =
                (int64_t)points[index].resistance_ohm -
                (int64_t)points[index - 1U].resistance_ohm;
            resistance_offset =
                (int64_t)resistance_ohm -
                (int64_t)points[index - 1U].resistance_ohm;
            temperature_delta =
                (int64_t)points[index].temperature_decic -
                (int64_t)points[index - 1U].temperature_decic;
            interpolated =
                (int64_t)points[index - 1U].temperature_decic +
                ((temperature_delta * resistance_offset) /
                 resistance_delta);
            *temperature_decic = (BMS_TemperatureDeciC_t)interpolated;
            return true;
        }
    }

    return false;
}
