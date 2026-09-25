#ifndef FML_PERSISTENCE_H
#define FML_PERSISTENCE_H

#include <stdbool.h>
#include <stdint.h>

#include "fml_policy.h"

#define BMS_PERSISTENCE_MAGIC                    (0x31534D42UL)
#define BMS_PERSISTENCE_MODEL_VERSION            (2U)
#define BMS_PERSISTENCE_RECORD_BYTES             (34U)
#define BMS_PERSISTENCE_BODY_BYTES               (32U)
#define BMS_PERSISTENCE_COMMIT_MARKER             (0xA55AU)

typedef struct
{
    uint32_t sequence;               /* 单调提交序号；比较时按有符号差处理回绕 */
    uint16_t soc_permille;           /* 0..1000，千分比而非百分比 */
    uint32_t remaining_capacity_mah; /* 与 SOC 同一次快照对应的剩余容量 */
    uint32_t persistent_counter0;    /* 上层定义的掉电后仍需延续的计数器 0 */
    uint32_t persistent_counter1;    /* 上层定义的掉电后仍需延续的计数器 1 */
} BMS_PersistencePayload_t;

typedef enum
{
    BMS_PERSISTENCE_SLOT_NONE = 0, /* 两页都不是可恢复的已提交记录 */
    BMS_PERSISTENCE_SLOT_A,        /* 当前最新有效记录位于 A 页 */
    BMS_PERSISTENCE_SLOT_B         /* 当前最新有效记录位于 B 页 */
} BMS_PersistenceSlot_t;

/*
 * 存储后端接口把“掉电安全协议”与具体 MCU Flash 驱动解耦。
 * program_halfword 必须遵守目标 Flash 的 1 -> 0 编程约束；擦除粒度由
 * erase_page 承担。context 让主机测试可注入 RAM Flash/故障模型。
 */
typedef struct
{
    /* 从目标 Flash 读取指定范围；失败时输出不作有效记录。 */
    bool (*read)(void *context, uint32_t address,
                 uint8_t *destination, uint16_t length);
    /* 擦除指定持久化页，成功后该页不再含已提交记录。 */
    bool (*erase_page)(void *context, uint32_t page_address);
    /* 按目标 Flash 半字粒度编程，遵守 1→0 约束。 */
    bool (*program_halfword)(void *context, uint32_t address,
                             uint16_t value);
    void *context; /* 传给每个存储回调的目标端上下文。 */
} BMS_PersistenceStorageOps_t;

typedef struct
{
    uint32_t load_io_failure_count;   /* 启动读取任一页失败 */
    uint32_t both_invalid_count;      /* 两页都未通过格式/CRC/提交标志 */
    uint32_t save_success_count;      /* 新页完整提交且二次校验成功 */
    uint32_t save_io_failure_count;   /* 擦除、编程或读取操作失败 */
    uint32_t save_verify_failure_count; /* 写入内容或提交后解码不一致 */
    uint32_t save_not_due_count;      /* 周期/变化量门限尚未满足 */
} BMS_PersistenceDiagnostics_t;

typedef enum
{
    BMS_PERSISTENCE_STORE_NOT_DUE = 0, /* 合法请求，但尚无需磨损 Flash */
    BMS_PERSISTENCE_STORE_SAVED,       /* 新记录已经成为最新有效页 */
    BMS_PERSISTENCE_STORE_REJECTED,    /* 参数、初始化状态或数据无效 */
    BMS_PERSISTENCE_STORE_IO_ERROR,    /* 底层擦写/读取失败，旧页仍保留 */
    BMS_PERSISTENCE_STORE_VERIFY_ERROR /* 写后校验失败，不切换 active */
} BMS_PersistenceStoreResult_t;

typedef struct
{
    BMS_FlashPolicy_t policy;          /* 地址、页大小、节流周期与变化门限 */
    BMS_PersistenceStorageOps_t storage; /* 目标 Flash 或测试替身 */
    BMS_PersistencePayload_t cached;   /* 最近一次完整提交并验证的内容 */
    BMS_PersistenceDiagnostics_t diagnostics; /* 饱和累计的可观测性计数 */
    BMS_PersistenceSlot_t active_slot; /* cached 对应的当前有效页 */
    uint32_t last_save_ms;             /* 只在成功提交后推进 */
    bool initialized;                  /* 后端配置和启动扫描已经完成 */
    bool have_active;                  /* A/B 中至少存在一条有效记录 */
} BMS_PersistenceStore_t;

/* 记录格式工具无全局状态，可由单元测试直接验证 CRC、编码和损坏拒绝语义。 */
uint32_t FML_Persistence_Crc32(const uint8_t *data, uint16_t length);
/* 按固定版本与字节序编码持久化记录及 CRC。 */
bool FML_Persistence_Encode(
    const BMS_PersistencePayload_t *payload,
    uint8_t record[BMS_PERSISTENCE_RECORD_BYTES]);
/* 校验记录版本、提交标记与 CRC 后解码载荷。 */
bool FML_Persistence_Decode(
    const uint8_t record[BMS_PERSISTENCE_RECORD_BYTES],
    BMS_PersistencePayload_t *payload);
/* 按提交标记与回绕序号选出 A/B 页中的最新有效记录。 */
BMS_PersistenceSlot_t FML_Persistence_SelectNewest(
    const uint8_t slot_a[BMS_PERSISTENCE_RECORD_BYTES],
    const uint8_t slot_b[BMS_PERSISTENCE_RECORD_BYTES],
    BMS_PersistencePayload_t *payload);

/*
 * StoreInit 只扫描 A/B 页并建立 RAM 缓存，不会为了“修复”坏页而写 Flash。
 * StoreSocIfDue 由单一任务串行调用；store 自身不提供锁，调用者不得并发访问。
 * 返回 NOT_DUE 表示正常节流，不是故障；失败返回时 active/cached 保持旧值。
 */
bool FML_Persistence_StoreInit(
    BMS_PersistenceStore_t *store,
    const BMS_FlashPolicy_t *policy,
    const BMS_PersistenceStorageOps_t *storage);
/* 从两个页中选择最新有效记录，不把损坏页当作新状态。 */
bool FML_Persistence_StoreGetLatest(
    const BMS_PersistenceStore_t *store,
    BMS_PersistencePayload_t *payload);
/* 按时间与变化门限决定是否提交 SOC，避免无意义的 Flash 擦写。 */
BMS_PersistenceStoreResult_t FML_Persistence_StoreSocIfDue(
    BMS_PersistenceStore_t *store,
    uint16_t soc_permille,
    uint32_t remaining_capacity_mah,
    uint32_t persistent_counter0,
    uint32_t persistent_counter1,
    bool valid,
    uint32_t now_ms);
/* 复制 A/B 页存储算法的诊断计数。 */
BMS_PersistenceDiagnostics_t FML_Persistence_StoreGetDiagnostics(
    const BMS_PersistenceStore_t *store);

/*
 * 正式 target adapter。init 只读取 A/B page，绝不为“修复”无效记录而写 Flash；
 * APL SOC task 是低频 save service 唯一调用者，避免多个任务竞争 erase/program。
 * 例如 A 页 VALID/seq=10、B 页 VALID/seq=11 时启动选择 B；下一次保存擦写 A，
 * 写入 body+CRC、读回验证，最后才写 commit marker。任一步掉电仍可回退到 B。
 */
bool FML_Persistence_TargetInit(
    const BMS_FlashPolicy_t *policy,
    const BMS_PersistenceStorageOps_t *storage);
/* 从目标 Flash 的 A/B 页读取最新有效载荷。 */
bool FML_Persistence_TargetGetLatest(BMS_PersistencePayload_t *payload);
/* 用当前 SOC 快照服务受节流约束的目标 Flash 写入。 */
BMS_PersistenceStoreResult_t FML_Persistence_TargetServiceSoc(
    uint16_t soc_permille,
    uint32_t remaining_capacity_mah,
    uint32_t persistent_counter0,
    uint32_t persistent_counter1,
    bool valid,
    uint32_t now_ms);
/* 复制目标持久化服务的诊断状态。 */
BMS_PersistenceDiagnostics_t FML_Persistence_TargetGetDiagnostics(void);

#endif /* FML_PERSISTENCE_H：头文件防重复包含 */
