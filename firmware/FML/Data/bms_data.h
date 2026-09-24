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
    BMS_TimestampMs_t timestamp_ms; /* 最近一次成功/明确发布该组数据的时刻。 */
    BMS_DataAgeMs_t age_ms;         /* 读取快照时由 now-timestamp 推导，不是独立采样值。 */
    bool valid;                     /* 采集与换算链是否成功；不代表数值安全。 */
    bool in_range;                  /* 有效数值是否落在允许域；不代表数据足够新。 */
    /* 一旦跨过 freshness 门限就锁存 stale，时间戳回绕不能让旧数据复活。 */
    bool stale_latched;
} BMS_MeasurementMetadata_t;

/* 13 节电芯并非严格同时完成转换，因此逐节保存时间与质量 bitmap。 */
typedef struct
{
    BMS_TimestampMs_t timestamp_ms[BMS_CELL_COUNT]; /* 每节数据的发布时刻。 */
    BMS_DataAgeMs_t age_ms[BMS_CELL_COUNT];         /* 每节数据在读取时的年龄。 */
    uint16_t valid_bitmap;                          /* bit=1：对应电芯采集/换算成功。 */
    uint16_t in_range_bitmap;                       /* bit=1：对应有效值位于配置范围。 */
    uint16_t stale_bitmap;                          /* bit=1：曾跨 freshness 门限并锁存。 */
} BMS_CellMetadata_t;

/*
 * 仅供内存共享的一致快照模型，不是 CAN frame 或 Flash record。
 * struct padding、endianness 与版本布局都不属于协议，禁止直接复制裸内存序列化。
 * BMS_Data 只做诊断聚合；FET 安全仲裁必须读取各 owner 的权威快照。
 */
struct BMS_DataSnapshot
{
    BMS_CellVoltageMv_t cell_voltage_mv[BMS_CELL_COUNT]; /* 同一 mandatory core 的 13S 电压。 */
    /* 安全计算使用 13 节 cell sum；BQ BAT 读数只做独立诊断交叉检查。 */
    BMS_PackVoltageMv_t pack_voltage_mv;
    BMS_PackVoltageMv_t bq_pack_voltage_mv; /* BQ BAT 通道诊断值，不替代 cell sum。 */
    BMS_CurrentMa_t current_ma;             /* 最近一次绑定到本 generation 的 CC 电流。 */
    uint16_t ts1_raw14;                     /* TS1 ADC 原始 14-bit 诊断值。 */
    uint32_t ts1_resistance_ohm;            /* 由 TS1 比值反推的 NTC 电阻。 */
    BMS_TemperatureDeciC_t temperature_decic; /* NTC 表换算温度，单位 0.1 °C。 */
    BMS_CapacityMah_t remaining_capacity_mah; /* SOC owner 发布的诊断容量。 */
    BMS_SocPermille_t soc_permille;           /* 0..1000 对应 0%..100%，无效时为哨兵。 */

    BMS_State_t state; /* State owner 发布的运行分类，仅作诊断投影。 */
    BMS_FaultSummary_t faults; /* Protect/State 汇总的诊断故障，不是 FET authority。 */

    BMS_CellMetadata_t cell_metadata; /* 各电芯的逐节质量和年龄信息。 */
    BMS_MeasurementMetadata_t pack_metadata; /* 13S 求和电压的质量信息。 */
    BMS_MeasurementMetadata_t bq_pack_metadata; /* BAT 诊断电压的质量信息。 */
    BMS_MeasurementMetadata_t current_metadata; /* CC 电流的质量信息。 */
    /* TS1 raw/resistance 有效不代表校准温度有效，两层证据必须分别保存。 */
    BMS_MeasurementMetadata_t ts1_metadata;
    BMS_MeasurementMetadata_t temperature_metadata; /* 校准温度的质量信息。 */
    BMS_MeasurementMetadata_t soc_metadata; /* SOC 估计的有效性和时效。 */

    BMS_TimestampMs_t snapshot_timestamp_ms; /* 本帧 mandatory core 的统一采样时刻。 */
    uint32_t sample_sequence;   /* 每次完整 core 成功发布递增，用于区分相邻快照。 */
    uint32_t afe_generation;    /* AFE 生命周期；XREADY 后改变，旧帧不得跨代使用。 */
};

/*
 * 只需要 freshness 的消费者使用这个有界栈投影。所有字段在持有 runtime data port
 * 时一次复制，因此三组元数据与 identity 必然来自同一发布代；刻意省略电芯
 * 数组，避免 SampleTask 为一次 stale 判断占用完整 snapshot 栈空间。
 */
typedef struct
{
    BMS_MeasurementMetadata_t pack_metadata; /* 电压组的时效投影。 */
    BMS_MeasurementMetadata_t current_metadata; /* 电流组的时效投影。 */
    BMS_MeasurementMetadata_t temperature_metadata; /* 温度组的时效投影。 */
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
    BMS_TimestampMs_t timestamp_ms;                  /* mandatory core 完成时刻。 */
    uint32_t afe_generation;                         /* 捕获并复核过的 AFE identity。 */
    BMS_CellVoltageMv_t cell_voltage_mv[BMS_CELL_COUNT]; /* 本地 staging 的完整 13S。 */
    uint16_t cell_valid_bitmap;                      /* 所有定义位都必须有效才能发布。 */
    uint16_t cell_in_range_bitmap;                   /* 越界可随有效帧发布供保护判断。 */

    BMS_PackVoltageMv_t bq_pack_voltage_mv; /* 同轮 BAT 独立诊断值，单位 mV。 */
    bool bq_pack_valid; /* BAT 通道读取和换算成功。 */
    bool bq_pack_in_range; /* BAT 值落在合法范围；不代表数据足够新。 */

    bool update_current;                 /* 本周期是否用新 CC 更新电流组。 */
    BMS_CurrentMa_t current_ma; /* 与当前 AFE 世代绑定的 CC 电流，单位 mA。 */
    BMS_TimestampMs_t current_timestamp_ms; /* CC 样本接纳的毫秒时刻。 */
    bool current_valid; /* 当前帧携带可用的同代 CC 电流。 */
    bool current_in_range; /* 可用 CC 电流落在数据模型允许范围。 */

    bool update_temperature;             /* 本周期是否达到温度分频节拍。 */
    uint16_t ts1_raw14; /* 本轮 TS1 ADC 的 14 位原始码。 */
    uint32_t ts1_resistance_ohm; /* TS1 原始码换算的 NTC 电阻，单位 Ω。 */
    BMS_TimestampMs_t temperature_timestamp_ms; /* 本次 TS1 读取的毫秒时刻。 */
    bool ts1_valid; /* TS1 原始码和电阻换算成功。 */
    BMS_TemperatureDeciC_t temperature_decic; /* NTC 插值得到的 0.1 °C 温度。 */
    bool temperature_valid; /* 插值链成功，区别于 ts1_valid。 */
    bool temperature_in_range; /* 有效温度落在数据模型允许范围。 */
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

/* 仅启动期初始化；调用时 RTOS 对象与任务尚未创建。 */
void BMS_Data_Init(void);

#if defined(TEST_PHASE8_DATA_IMAGE) || defined(BMS_PHASE8_HOST_TEST)
/*
 * Phase 8 data-image fault injection 专用入口。production build 不声明也不编译
 * 该接口；正式 writer 只能使用下列窄 publish API，不能直接修改 backing store。
 */
BMS_DataSnapshot_t *BMS_Data_TestMutableStorage(void);
#endif

/*
 * 使用 zero-wait data port 尝试发布完整 staging frame。NULL、frame 不合法或
 * mutex 忙都返回 false，并保持旧快照逐字不变；只改 measurement-owned 字段，
 * State/fault/SOC/capacity 等其他 owner 的诊断投影保持不变。
 */
bool BMS_Data_PublishMeasurement(const BMS_MeasurementFrame_t *frame);

/*
 * 调用者：State、FET、Balance、CAN/Debug 等执行上下文；API 内部获取 data port，
 * 调用者不得预先持有它。持锁时把同一 generation 直接复制到调用者，释放后再用无符号减法
 * 计算 wrap-safe age；不在栈上再放第二个完整 snapshot。valid 表示采样/换算成功，
 * fresh 表示尚在时效窗口，in_range 表示数值位于配置域，三者不能互相代替。
 * stale 不会清除 valid，失败也不改调用者输出。周期读者一旦观察到门限跨越就
 * 锁存 stale，直到该测量组重新发布；完整 2^32 ms 停顿由 watchdog 约束。
 *
 * 必须一次读取完整 snapshot，不能分别 GetVoltage/GetCurrent/GetTemperature 后
 * 自行拼接；独立 getter 可能跨越两个 sample_sequence，构造出现实中从未同时
 * 存在的电压、电流和温度组合，进而破坏安全决策的一致性。
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

/*
 * 在 data port 内只复制测量 identity，供轻量 compare-and-publish 检查。
 * 调用者只读，返回 false 表示参数/handle 无效或 mutex 当下忙；API 不等待，
 * 因为身份检查宁可稍后重试，也不能阻塞高优先级测量发布。
 */
bool BMS_Data_GetIdentity(BMS_DataIdentity_t *identity);

/* 仅更新诊断投影；两个 API 都不会生成或转移 FET safety authority。 */
bool BMS_Data_PublishStateDiagnostic(BMS_State_t state,
                                     const BMS_FaultSummary_t *faults);
/* 把 SOC owner 的容量和千分比写入只读诊断投影。 */
bool BMS_Data_PublishSocDiagnostic(BMS_CapacityMah_t capacity_mah,
                                   BMS_SocPermille_t soc_permille,
                                   BMS_TimestampMs_t now_ms,
                                   bool valid);

/* valid 与 fresh 刻意分离；只有该测量组的新发布可以清除 stale latch。 */
bool BMS_Data_IsFresh(bool valid,
                      bool stale_latched,
                      uint32_t age_ms,
                      uint32_t max_age_ms);

#endif /* BMS_DATA_H：头文件防重复包含 */
