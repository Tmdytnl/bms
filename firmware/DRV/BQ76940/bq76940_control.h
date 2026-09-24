#ifndef BQ76940_CONTROL_H
#define BQ76940_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "bq76940_build_assert.h"
#include "bq76940.h"
#include "bq76940_regs.h"

#define BQ76940_CONTROL_LOGICAL_CELL_COUNT       (13U)

/*
 * BQ7694003 protection register encoding、FET bit composition 与 CELLBAL mapping。
 * 依据 TI SLUSBK2I Rev.I：OV_TRIP/UV_TRIP 保存 14-bit ADC code 的中间 8 bit，
 * 分别使用固定前缀/后缀 10…1000 与 01…0000；PROTECT1 编码 RSNS/SCD；
 * PROTECT2 编码 OCD；PROTECT3 编码 UV/OV delay；SYS_CTRL2 的 bit6/1/0 分别是
 * CC_EN/DSG_ON/CHG_ON；CELLBAL1..3 对应 CB1..CB15。
 *
 * 本层只提供纯 register encoding 与安全 composition primitive：不运行 SYS_STAT
 * service、ALERT、Task 或 RTOS object，也不选择产品 threshold。FML 调用者提供
 * 目标 mV/uV/delay，本层负责映射为 datasheet code 并严格检查合法范围。
 */

/* ------------------------------------------------------------------ */
/* OV / UV trip 编码（SLUSBK2I 8.3.1.2.1）。 */
/* ------------------------------------------------------------------ */

/*
 * 按官方公式把目标 cell mV 转为 8-bit OV_TRIP：
 *
 *   full_code = (target_mv - calibration.offset_mv) * 1000 / gain_uv
 *   trip      = (full_code >> 4) & 0xFF
 *
 * 使用 64-bit intermediate、无 float；calibration 非法或 full code 不在
 * OV 的 bits13:12==10b 窗口时返回 RANGE_ERROR。
 */
BQ76940_Status_t BQ76940_Control_EncodeOvTrip(
    uint16_t target_mv,
    const BQ76940_Calibration_t *calibration,
    uint8_t *trip_value);

/*
 * UV_TRIP 同理，但 14-bit full code 必须满足 bits13:12==01b。
 */
BQ76940_Status_t BQ76940_Control_EncodeUvTrip(
    uint16_t target_mv,
    const BQ76940_Calibration_t *calibration,
    uint8_t *trip_value);

/*
 * 为 readback verification 反解 OV_TRIP：用固定 10…1000 重建 14-bit code，
 * 再反向应用 calibration，返回 rounded mV threshold。
 */
uint16_t BQ76940_Control_DecodeOvTripMv(
    uint8_t trip_value,
    const BQ76940_Calibration_t *calibration);

/* 解码欠压跳闸阈值mV。 */
uint16_t BQ76940_Control_DecodeUvTripMv(
    uint8_t trip_value,
    const BQ76940_Calibration_t *calibration);

/* ------------------------------------------------------------------ */
/* OCD / SCD 编码（SLUSBK2I 8.3.1.2.2/8.3.1.2.3）。 */
/* ------------------------------------------------------------------ */

/* RSNS=1 官方 threshold table，单位为 SRP-SRN mV。 */
#define BQ76940_CONTROL_OCD_THRESHOLD_COUNT       (16U)
#define BQ76940_CONTROL_SCD_THRESHOLD_COUNT       (8U)

/* 官方 delay table。 */
#define BQ76940_CONTROL_OCD_DELAY_COUNT           (8U)
#define BQ76940_CONTROL_SCD_DELAY_COUNT           (4U)
#define BQ76940_CONTROL_OV_DELAY_COUNT            (4U)
#define BQ76940_CONTROL_UV_DELAY_COUNT            (4U)

/*
 * 按“不低于请求值”选择 OCD code：取 threshold>=requested_mv 的最小合法 code；
 * 超出 table 上限返回 RANGE_ERROR。rsns=true 选 17..100 mV upper range，
 * false 选 8..50 mV lower range。
 */
BQ76940_Status_t BQ76940_Control_SelectOcdThreshold(
    uint16_t requested_mv,
    bool rsns,
    uint8_t *code);

/* 从 OCD 离散延时表选出可编码毫秒值。 */
BQ76940_Status_t BQ76940_Control_SelectOcdDelayMs(
    uint16_t requested_ms,
    uint8_t *code);

/* 从 SCD 离散阈值表选出可编码值并返回寄存器码。 */
BQ76940_Status_t BQ76940_Control_SelectScdThreshold(
    uint16_t requested_mv,
    bool rsns,
    uint8_t *code);

/* 从 SCD 离散延时表选出可编码微秒值。 */
BQ76940_Status_t BQ76940_Control_SelectScdDelayUs(
    uint16_t requested_us,
    uint8_t *code);

/* 把秒单位过压延时映射到器件离散编码。 */
BQ76940_Status_t BQ76940_Control_SelectOvDelayS(
    uint8_t requested_s,
    uint8_t *code);

/* 把秒单位欠压延时映射到器件离散编码。 */
BQ76940_Status_t BQ76940_Control_SelectUvDelayS(
    uint8_t requested_s,
    uint8_t *code);

/*
 * 组合完整 PROTECT1（SCD）：rsns→bit7，delay_code→bits4:3，thresh_code→bits2:0，
 * bits6:5 保持 0；非法 code 返回 RANGE_ERROR 并保持输出不变。
 */
BQ76940_Status_t BQ76940_Control_ComposeProtect1(
    bool rsns,
    uint8_t delay_code,
    uint8_t thresh_code,
    uint8_t *register_value);

/* PROTECT2（OCD）：delay bits6:4、threshold bits3:0；非法时保持输出不变。 */
BQ76940_Status_t BQ76940_Control_ComposeProtect2(
    uint8_t delay_code,
    uint8_t thresh_code,
    uint8_t *register_value);

/* PROTECT3：UV delay bits7:6、OV delay bits5:4；非法时保持输出不变。 */
BQ76940_Status_t BQ76940_Control_ComposeProtect3(
    uint8_t uv_delay_code,
    uint8_t ov_delay_code,
    uint8_t *register_value);

/* ------------------------------------------------------------------ */
/* FET 仲裁基础类型（H-04 单写者规则）。 */
/* ------------------------------------------------------------------ */

/* 与 register encoding 解耦的 requested FET desire。 */
typedef enum
{
    BQ76940_FET_DESIRE_DISABLE = 0, /* fail-safe 默认值；没有授权即关闭 */
    BQ76940_FET_DESIRE_ENABLE = 1  /* 仅表达请求，仍需上层 inhibit 仲裁 */
} BQ76940_FetDesire_t;

typedef struct
{
    BQ76940_FetDesire_t chg; /* 充电通路期望，不等于寄存器实际状态 */
    BQ76940_FetDesire_t dsg; /* 放电通路期望，不等于寄存器实际状态 */
} BQ76940_FetRequest_t;

/* 从 SYS_CTRL2 readback 解码的 register-level observed FET bit。 */
typedef struct
{
    bool chg_on; /* 最近一次 SYS_CTRL2 readback 的 CHG_ON 位 */
    bool dsg_on; /* 最近一次 SYS_CTRL2 readback 的 DSG_ON 位 */
} BQ76940_FetObserved_t;

/*
 * 从 current register 与 CHG/DSG desire 构造新 SYS_CTRL2，只保留 CC_EN(bit6)。
 * factory-test DELAY_DIS(bit7)、CC_ONESHOT(bit5) 与 reserved bits4:2 强制为 0；
 * NULL request fail-safe 为双关。控制代码只能通过该 compositor 派生新 byte，
 * 禁止其他模块直接拼 CHG/DSG bit。
 */
uint8_t BQ76940_Control_SysCtrl2WithFets(uint8_t current_ctrl2,
                                         const BQ76940_FetRequest_t *request);

/* 从 SYS_CTRL2 readback 提取 observed CHG/DSG。 */
void BQ76940_Control_ObserveFets(uint8_t ctrl2,
                                 BQ76940_FetObserved_t *observed);

/*
 * 把 requested desire 与 direction inhibit 合成 effective state：对应 inhibit
 * 非零时无条件 force OFF；NULL request fail-safe 为双关，NULL output 忽略。
 */
void BQ76940_Control_ApplyInhibits(const BQ76940_FetRequest_t *request,
                                   bool inhibit_chg,
                                   bool inhibit_dsg,
                                   BQ76940_FetRequest_t *effective);

/* ------------------------------------------------------------------ */
/* AFE 内部均衡映射（SLUSBK2I 8.3.1.1.1 + CELLBAL）。 */
/* ------------------------------------------------------------------ */

/*
 * 显式 logical-cell→CELLBAL bit：Cell1..8→CB1..8，Cell9..12→CB10..13，
 * Cell13→CB15；CB9/CB14 对应 short channel，永不使用。越界 index 返回 0。
 */
uint8_t BQ76940_Control_CellBalBitOfLogicalCell(uint8_t logical_cell_index);

/*
 * 将 logical bitmap（bit0..12）映射为 CELLBAL1/2/3。此 frozen API 最多允许
 * 一节，多个 bit 或越界返回 false，并把输出保持为安全全零。
 */
bool BQ76940_Control_ComposeCellBal(
    uint16_t balance_bitmap,
    uint8_t *bal1,
    uint8_t *bal2,
    uint8_t *bal3);

/*
 * Balance owner 使用的 policy-aware additive compositor；上方 one-cell API 保持
 * 兼容。该版本限制 max_parallel_cells，并可在映射 AFE register 前拒绝相邻电芯。
 */
bool BQ76940_Control_ComposeCellBalPolicy(
    uint16_t balance_bitmap,
    uint8_t max_parallel_cells,
    bool adjacent_cells_permitted,
    uint8_t *bal1,
    uint8_t *bal2,
    uint8_t *bal3);

/*
 * 将 CELLBAL1/2/3 反解为 logical bitmap；CB9/CB14 与 reserved bit 永不报告。
 */
uint16_t BQ76940_Control_DecodeCellBal(uint8_t bal1,
                                       uint8_t bal2,
                                       uint8_t bal3);

#endif /* BQ76940_CONTROL_H：头文件防重复包含 */
