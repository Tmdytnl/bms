#ifndef FML_NTC_H
#define FML_NTC_H

#include <stdbool.h>
#include <stdint.h>

#include "fml_types.h"

/* calibration-owned NTC point；模块不硬编码曲线，实际 table 由配置层注入。 */
typedef struct
{
    uint32_t resistance_ohm; /* 插值表节点电阻，单位 Ω。 */
    BMS_TemperatureDeciC_t temperature_decic; /* 对应节点温度，单位 0.1 °C。 */
} BMS_NtcPoint_t;

/*
 * 合法 NTC table 至少两点，resistance 严格单调，temperature 必须反向严格单调；
 * 支持正序或逆序，重复/折返会被整体拒绝。
 */
bool FML_Ntc_ValidateTable(const BMS_NtcPoint_t *points,
                           uint16_t point_count);

/* 分段线性插值；超出 resistance 域、table 非法或输出 NULL 时保持输出不变。 */
bool FML_Ntc_Interpolate(const BMS_NtcPoint_t *points,
                         uint16_t point_count,
                         uint32_t resistance_ohm,
                         BMS_TemperatureDeciC_t *temperature_decic);

#endif /* FML_NTC_H：include guard */
