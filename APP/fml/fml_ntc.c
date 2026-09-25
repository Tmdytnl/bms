#include "fml_ntc.h"

#include <stddef.h>

/* 从前两个节点判定电阻排序方向，供整表单调性检查。 */
static bool FML_Ntc_IsAscendingResistance(const BMS_NtcPoint_t *points)
{
    return points[1].resistance_ohm > points[0].resistance_ohm;
}

/* 验证节点数、单调性和温度范围，拒绝不可插值的表。 */
bool FML_Ntc_ValidateTable(const BMS_NtcPoint_t *points,
                           uint16_t point_count)
{
    /* NTC 标定电阻是否按升序排列。 */
    bool resistance_ascending;
    /* 当前 NTC 标定表节点的索引。 */
    uint16_t index;

    if ((points == NULL) || (point_count < 2U))
    {
        return false;
    }

    resistance_ascending = FML_Ntc_IsAscendingResistance(points);
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

/* 检查电阻是否落在 NTC 表的当前插值区间。 */
static bool FML_Ntc_ResistanceInSegment(const BMS_NtcPoint_t *left,
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

/* 在合法单调 NTC 表的相邻点之间插值温度，不外推范围。 */
bool FML_Ntc_Interpolate(const BMS_NtcPoint_t *points,
                         uint16_t point_count,
                         uint32_t resistance_ohm,
                         BMS_TemperatureDeciC_t *temperature_decic)
{
    /* 当前 NTC 标定表节点的索引。 */
    uint16_t index;
    /* 两个 NTC 标定点之间的电阻差值。 */
    int64_t resistance_delta;
    /* NTC 分压计算的电阻校准偏移。 */
    int64_t resistance_offset;
    /* 相邻 NTC 标定点的温度差值。 */
    int64_t temperature_delta;
    /* NTC 相邻标定点之间的插值结果。 */
    int64_t interpolated;

    if ((temperature_decic == NULL) ||
        !FML_Ntc_ValidateTable(points, point_count))
    {
        return false;
    }

    for (index = 1U; index < point_count; ++index)
    {
        if (FML_Ntc_ResistanceInSegment(&points[index - 1U],
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
