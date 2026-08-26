#ifndef BMS_PERSISTENCE_H
#define BMS_PERSISTENCE_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_policy.h"

#define BMS_PERSISTENCE_MAGIC                    (0x31534D42UL)
#define BMS_PERSISTENCE_MODEL_VERSION            (2U)
#define BMS_PERSISTENCE_RECORD_BYTES             (34U)
#define BMS_PERSISTENCE_BODY_BYTES               (32U)
#define BMS_PERSISTENCE_COMMIT_MARKER             (0xA55AU)

typedef struct
{
    uint32_t sequence;               /* wrap-safe newest-valid 选择序号 */
    uint16_t soc_permille;
    uint32_t remaining_capacity_mah;
    uint32_t persistent_counter0;
    uint32_t persistent_counter1;
} BMS_PersistencePayload_t;

typedef enum
{
    BMS_PERSISTENCE_SLOT_NONE = 0,
    BMS_PERSISTENCE_SLOT_A,
    BMS_PERSISTENCE_SLOT_B
} BMS_PersistenceSlot_t;

typedef struct
{
    bool (*read)(void *context, uint32_t address,
                 uint8_t *destination, uint16_t length);
    bool (*erase_page)(void *context, uint32_t page_address);
    bool (*program_halfword)(void *context, uint32_t address,
                             uint16_t value);
    void *context;
} BMS_PersistenceStorageOps_t;

typedef struct
{
    uint32_t load_io_failure_count;
    uint32_t both_invalid_count;
    uint32_t save_success_count;
    uint32_t save_io_failure_count;
    uint32_t save_verify_failure_count;
    uint32_t save_not_due_count;
} BMS_PersistenceDiagnostics_t;

typedef enum
{
    BMS_PERSISTENCE_STORE_NOT_DUE = 0,
    BMS_PERSISTENCE_STORE_SAVED,
    BMS_PERSISTENCE_STORE_REJECTED,
    BMS_PERSISTENCE_STORE_IO_ERROR,
    BMS_PERSISTENCE_STORE_VERIFY_ERROR
} BMS_PersistenceStoreResult_t;

typedef struct
{
    BMS_FlashPolicy_t policy;
    BMS_PersistenceStorageOps_t storage;
    BMS_PersistencePayload_t cached;
    BMS_PersistenceDiagnostics_t diagnostics;
    BMS_PersistenceSlot_t active_slot;
    uint32_t last_save_ms;
    bool initialized;
    bool have_active;
} BMS_PersistenceStore_t;

uint32_t BMS_Persistence_Crc32(const uint8_t *data, uint16_t length);
bool BMS_Persistence_Encode(
    const BMS_PersistencePayload_t *payload,
    uint8_t record[BMS_PERSISTENCE_RECORD_BYTES]);
bool BMS_Persistence_Decode(
    const uint8_t record[BMS_PERSISTENCE_RECORD_BYTES],
    BMS_PersistencePayload_t *payload);
BMS_PersistenceSlot_t BMS_Persistence_SelectNewest(
    const uint8_t slot_a[BMS_PERSISTENCE_RECORD_BYTES],
    const uint8_t slot_b[BMS_PERSISTENCE_RECORD_BYTES],
    BMS_PersistencePayload_t *payload);

bool BMS_Persistence_StoreInit(
    BMS_PersistenceStore_t *store,
    const BMS_FlashPolicy_t *policy,
    const BMS_PersistenceStorageOps_t *storage);
bool BMS_Persistence_StoreGetLatest(
    const BMS_PersistenceStore_t *store,
    BMS_PersistencePayload_t *payload);
BMS_PersistenceStoreResult_t BMS_Persistence_StoreSocIfDue(
    BMS_PersistenceStore_t *store,
    uint16_t soc_permille,
    uint32_t remaining_capacity_mah,
    uint32_t persistent_counter0,
    uint32_t persistent_counter1,
    bool valid,
    uint32_t now_ms);
BMS_PersistenceDiagnostics_t BMS_Persistence_StoreGetDiagnostics(
    const BMS_PersistenceStore_t *store);

/*
 * 正式 target adapter。init 只读取 A/B page，绝不为“修复”无效记录而写 Flash；
 * Task_SOC 是低频 save service 唯一调用者，避免多个任务竞争 erase/program。
 */
bool BMS_Persistence_TargetInit(const BMS_FlashPolicy_t *policy);
bool BMS_Persistence_TargetGetLatest(BMS_PersistencePayload_t *payload);
BMS_PersistenceStoreResult_t BMS_Persistence_TargetServiceSoc(
    uint16_t soc_permille,
    uint32_t remaining_capacity_mah,
    uint32_t persistent_counter0,
    uint32_t persistent_counter1,
    bool valid,
    uint32_t now_ms);
BMS_PersistenceDiagnostics_t BMS_Persistence_TargetGetDiagnostics(void);

#endif /* BMS_PERSISTENCE_H：include guard */
