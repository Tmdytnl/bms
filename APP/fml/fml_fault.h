#ifndef FML_FAULT_H
#define FML_FAULT_H

#include <stdbool.h>
#include <stdint.h>

#include "fml_build_assert.h"

/* 稳定 fault ID；bit position 已进入显式协议/持久化语义，禁止重排。 */
typedef enum
{
    BMS_FAULT_ID_HW_OV = 0, /* AFE 硬件单体过压事件。 */
    BMS_FAULT_ID_HW_UV = 1, /* AFE 硬件单体欠压事件。 */
    BMS_FAULT_ID_HW_OCD = 2, /* AFE 放电过流事件。 */
    BMS_FAULT_ID_HW_SCD = 3, /* AFE 放电短路事件。 */
    BMS_FAULT_ID_AFE_XREADY = 4, /* AFE 需要完整重建的 XREADY 事件。 */
    BMS_FAULT_ID_AFE_OVRD_ALERT = 5, /* AFE 外部 ALERT override。 */
    BMS_FAULT_ID_AFE_COMM = 6, /* 与 AFE 通信持续失败。 */
    BMS_FAULT_ID_AFE_CRC = 7, /* AFE 帧 CRC 验证失败。 */
    BMS_FAULT_ID_AFE_STALE = 8, /* AFE 测量链缺乏新证据。 */
    BMS_FAULT_ID_SW_OV = 9, /* 软件阈值判定的单体过压。 */
    BMS_FAULT_ID_SW_UV = 10, /* 软件阈值判定的单体欠压。 */
    BMS_FAULT_ID_SW_OC_CHARGE = 11, /* 软件充电过流。 */
    BMS_FAULT_ID_SW_OC_DISCHARGE = 12, /* 软件放电过流。 */
    BMS_FAULT_ID_TEMPERATURE_HIGH = 13, /* 超出允许的高温范围。 */
    BMS_FAULT_ID_TEMPERATURE_LOW = 14, /* 超出允许的低温范围。 */
    BMS_FAULT_ID_DATA_STALE = 15, /* 安全决策所需测量已过期。 */
    BMS_FAULT_ID_CAN = 16, /* CAN 通道诊断故障。 */
    BMS_FAULT_ID_FLASH_CONFIG = 17, /* 持久化配置或页状态故障。 */
    BMS_FAULT_ID_CLOCK = 18, /* 启动时钟配置不符合目标要求。 */
    BMS_FAULT_ID_RTOS_HEALTH = 19, /* 必需任务 heartbeat 停滞。 */
    BMS_FAULT_ID_COUNT = 20 /* fault ID 数组及位图边界。 */
} BMS_FaultId_t;

typedef uint32_t BMS_FaultBitmap_t;

typedef struct
{
    /*
     * active 表示 source owner 尚未证明 recovery contract 的安全条件/捕获事件。
     * 对 W1C source，hardware status bit 变低本身不是 recovery evidence。
     */
    BMS_FaultBitmap_t active;
    /*
     * latched 保存 severe-event history，不随 active/status 变零自动清除；
     * 任何 clear 都必须由 authoritative source-specific explicit-reset policy 执行。
     */
    BMS_FaultBitmap_t latched;
} BMS_FaultSummary_t;

#define BMS_FAULT_BITMAP_WIDTH_BITS              (32U)
#define BMS_FAULT_DEFINED_MASK                   ((BMS_FaultBitmap_t)0x000FFFFFUL)

BMS_BUILD_ASSERT(BMS_FAULT_ID_COUNT <= BMS_FAULT_BITMAP_WIDTH_BITS,
                 fault_count_fits_bitmap);
BMS_BUILD_ASSERT(BMS_FAULT_ID_RTOS_HEALTH ==
                     (BMS_FAULT_ID_COUNT - 1),
                 final_fault_id_matches_count);
BMS_BUILD_ASSERT(BMS_FAULT_DEFINED_MASK ==
                     (((BMS_FaultBitmap_t)1U << BMS_FAULT_ID_COUNT) -
                      (BMS_FaultBitmap_t)1U),
                 fault_defined_mask_matches_count);

/* 把 active 与 latched 故障位图初始化为空集合。 */
void FML_Fault_Init(BMS_FaultSummary_t *summary);
/* 检查 fault ID 是否位于已定义枚举范围。 */
bool FML_Fault_IdIsValid(BMS_FaultId_t fault_id);
/* 把合法 fault ID 转为对应位图掩码。 */
BMS_FaultBitmap_t FML_Fault_Mask(BMS_FaultId_t fault_id);
/* 检查 fault 位图是否包含指定合法 ID。 */
bool FML_Fault_Contains(BMS_FaultBitmap_t bitmap, BMS_FaultId_t fault_id);

#endif /* FML_FAULT_H：include guard */
