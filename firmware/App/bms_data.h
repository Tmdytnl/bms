#ifndef BMS_DATA_H
#define BMS_DATA_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_config.h"
#include "bms_fault.h"
#include "bms_state.h"
#include "bms_types.h"

/* 一组逻辑测量的质量元数据；数值与“能否参与安全决策”分开表达。 */
typedef struct
{
    BMS_TimestampMs_t timestamp_ms;
    BMS_DataAgeMs_t age_ms;
    bool valid;
    bool in_range;
    /* 一旦跨过 freshness 门限就锁存 stale，时间戳回绕不能让旧数据复活。 */
    bool stale_latched;
} BMS_MeasurementMetadata_t;

/* 13 节电芯并非严格同时完成转换，因此逐节保存时间与质量 bitmap。 */
typedef struct
{
    BMS_TimestampMs_t timestamp_ms[BMS_CELL_COUNT];
    BMS_DataAgeMs_t age_ms[BMS_CELL_COUNT];
    uint16_t valid_bitmap;
    uint16_t in_range_bitmap;
    uint16_t stale_bitmap;
} BMS_CellMetadata_t;

/*
 * 仅供内存共享的一致快照模型，不是 CAN frame 或 Flash record。
 * struct padding、endianness 与版本布局都不属于协议，禁止直接复制裸内存序列化。
 * BMS_Data 只做诊断聚合；FET 安全仲裁必须读取各 owner 的权威快照。
 */
struct BMS_DataSnapshot
{
    BMS_CellVoltageMv_t cell_voltage_mv[BMS_CELL_COUNT];
    /* 安全计算使用 13 节 cell sum；BQ BAT 读数只做独立诊断交叉检查。 */
    BMS_PackVoltageMv_t pack_voltage_mv;
    BMS_PackVoltageMv_t bq_pack_voltage_mv;
    BMS_CurrentMa_t current_ma;
    uint16_t ts1_raw14;
    uint32_t ts1_resistance_ohm;
    BMS_TemperatureDeciC_t temperature_decic;
    BMS_CapacityMah_t remaining_capacity_mah;
    BMS_SocPermille_t soc_permille;

    BMS_State_t state;
    BMS_FaultSummary_t faults;

    BMS_CellMetadata_t cell_metadata;
    BMS_MeasurementMetadata_t pack_metadata;
    BMS_MeasurementMetadata_t bq_pack_metadata;
    BMS_MeasurementMetadata_t current_metadata;
    /* TS1 raw/resistance 有效不代表校准温度有效，两层证据必须分别保存。 */
    BMS_MeasurementMetadata_t ts1_metadata;
    BMS_MeasurementMetadata_t temperature_metadata;
    BMS_MeasurementMetadata_t soc_metadata;

    BMS_TimestampMs_t snapshot_timestamp_ms;
    uint32_t sample_sequence;   /* 每次完整 core measurement 发布后单调递增 */
    uint32_t afe_generation;    /* 当前 AFE/XREADY 生命周期代号 */
};

/*
 * 只需要 freshness 的消费者使用这个有界栈投影。所有字段在持有 xDataMutex
 * 时一次复制，因此三组元数据与 identity 必然来自同一发布代；刻意省略电芯
 * 数组，避免 SampleTask 为一次 stale 判断占用完整 snapshot 栈空间。
 */
typedef struct
{
    BMS_MeasurementMetadata_t pack_metadata;
    BMS_MeasurementMetadata_t current_metadata;
    BMS_MeasurementMetadata_t temperature_metadata;
    uint32_t sample_sequence;   /* 投影所绑定的完整采样序号 */
    uint32_t afe_generation;    /* 投影所绑定的 AFE 生命周期 */
} BMS_DataFreshnessSnapshot_t;

typedef struct
{
    uint32_t sample_sequence;   /* compare-and-publish 使用的测量身份 */
    uint32_t afe_generation;    /* generation 改变后旧 identity 立即失效 */
} BMS_DataIdentity_t;

/*
 * SampleTask 在本地完整 staging 后一次发布的 measurement frame。
 * 13 节 cell 与 BQ pack 构成 mandatory core；current、TS1 按各自节拍可选更新。
 * mandatory transaction 任一失败时整帧不得发布，避免其他任务看到半更新数据。
 *
 * bq_pack_voltage_mv 只用于诊断。frame 刻意不暴露 pack_voltage_mv，发布函数
 * 必须由同一批 13 节电压求和，阻止调用者拼出互不对应的 cell/pack 组合。
 */
typedef struct
{
    BMS_TimestampMs_t timestamp_ms;
    uint32_t afe_generation;
    BMS_CellVoltageMv_t cell_voltage_mv[BMS_CELL_COUNT];
    uint16_t cell_valid_bitmap;
    uint16_t cell_in_range_bitmap;

    BMS_PackVoltageMv_t bq_pack_voltage_mv;
    bool bq_pack_valid;
    bool bq_pack_in_range;

    bool update_current;
    BMS_CurrentMa_t current_ma;
    BMS_TimestampMs_t current_timestamp_ms;
    bool current_valid;
    bool current_in_range;

    bool update_temperature;
    uint16_t ts1_raw14;
    uint32_t ts1_resistance_ohm;
    BMS_TimestampMs_t temperature_timestamp_ms;
    bool ts1_valid;
    BMS_TemperatureDeciC_t temperature_decic;
    bool temperature_valid;
    bool temperature_in_range;
} BMS_MeasurementFrame_t;

#define BMS_DATA_MODEL_VERSION                    (1U)
#define BMS_CELL_BITMAP_WIDTH_BITS               (16U)
#define BMS_CELL_DEFINED_MASK                    ((uint16_t)0x1FFFU)
#define BMS_DATA_VOLTAGE_FRESH_MAX_MS            \
    (BMS_VOLTAGE_FRESH_LIMIT_MS)
#define BMS_DATA_CURRENT_FRESH_MAX_MS            \
    (BMS_CURRENT_FRESH_LIMIT_MS)
#define BMS_DATA_TEMPERATURE_FRESH_MAX_MS        \
    (BMS_TEMPERATURE_FRESH_LIMIT_MS)
#define BMS_DATA_FRESHNESS_SNAPSHOT_MAX_BYTES    (44U)

BMS_BUILD_ASSERT(BMS_DATA_MODEL_VERSION == 1U,
                 data_model_version_is_one);
BMS_BUILD_ASSERT(BMS_CELL_COUNT <= BMS_CELL_BITMAP_WIDTH_BITS,
                 cell_count_fits_validity_bitmap);
BMS_BUILD_ASSERT(BMS_CELL_DEFINED_MASK ==
                     (uint16_t)(((uint16_t)1U << BMS_CELL_COUNT) -
                                (uint16_t)1U),
                 cell_defined_mask_matches_count);
BMS_BUILD_ASSERT(sizeof(BMS_DataFreshnessSnapshot_t) <=
                     BMS_DATA_FRESHNESS_SNAPSHOT_MAX_BYTES,
                 freshness_snapshot_stack_bound);

/*
 * 该全局存储只为启动兼容保留。运行期消费者必须读取 snapshot；写者只能调用
 * 自己的窄 API 并由模块持有 xDataMutex，禁止跨 owner 直接改字段。
 */
extern BMS_DataSnapshot_t g_bms_data;

/* 仅启动期初始化；调用时 RTOS 对象与任务尚未创建。 */
void BMS_Data_Init(void);

/*
 * 使用 zero-wait xDataMutex 尝试发布完整 staging frame。NULL、frame 不合法或
 * mutex 忙都返回 false，并保持旧快照逐字不变；只改 measurement-owned 字段，
 * State/fault/SOC/capacity 等其他 owner 的诊断投影保持不变。
 */
bool BMS_Data_PublishMeasurement(const BMS_MeasurementFrame_t *frame);

/*
 * 持有 xDataMutex 时把同一 generation 直接复制到调用者，释放后再用无符号减法
 * 计算 wrap-safe age；不在栈上再放第二个完整 snapshot。valid 表示采样/换算成功，
 * fresh 表示尚在时效窗口，in_range 表示数值位于配置域，三者不能互相代替。
 * stale 不会清除 valid，失败也不改调用者输出。周期读者一旦观察到门限跨越就
 * 锁存 stale，直到该测量组重新发布；完整 2^32 ms 停顿由 watchdog 约束。
 */
bool BMS_Data_GetSnapshot(BMS_DataSnapshot_t *snapshot,
                          BMS_TimestampMs_t now_ms);

/*
 * 只复制 stale 决策需要的 pack/current/temperature 元数据。投影仍绑定同一
 * generation，age 在释放 mutex 后计算；这是 SampleTask 的有界栈读取路径。
 */
bool BMS_Data_GetFreshnessSnapshot(
    BMS_DataFreshnessSnapshot_t *snapshot,
    BMS_TimestampMs_t now_ms);

/* 在 xDataMutex 内只复制测量 identity，供轻量 compare-and-publish 检查。 */
bool BMS_Data_GetIdentity(BMS_DataIdentity_t *identity);

/* 仅更新诊断投影；两个 API 都不会生成或转移 FET safety authority。 */
bool BMS_Data_PublishStateDiagnostic(BMS_State_t state,
                                     const BMS_FaultSummary_t *faults);
bool BMS_Data_PublishSocDiagnostic(BMS_CapacityMah_t capacity_mah,
                                   BMS_SocPermille_t soc_permille,
                                   BMS_TimestampMs_t now_ms,
                                   bool valid);

/* valid 与 fresh 刻意分离；只有该测量组的新发布可以清除 stale latch。 */
bool BMS_Data_IsFresh(bool valid,
                      bool stale_latched,
                      uint32_t age_ms,
                      uint32_t max_age_ms);

#endif /* BMS_DATA_H：include guard */
