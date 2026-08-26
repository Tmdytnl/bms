#include "bms_persistence.h"

/*
 * A/B page record 使用 magic+version+sequence+payload+CRC32+commit marker。
 * decode 只有在格式、CRC 与 commit 全部有效时才接纳；两个 slot 都有效时用
 * wrap-safe sequence 选择 newest-valid。保存永远写 inactive slot，因此掉电前
 * active old slot 保持不动。
 */

#include <stddef.h>
#include <string.h>

#if !defined(TEST_PHASE9_IMAGE)
#include "bsp_flash.h"
#endif

#define BMS_PERSISTENCE_CRC_OFFSET               (28U)
#define BMS_PERSISTENCE_COMMIT_OFFSET            (32U)

static void BMS_Persistence_PutU16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value & 0xFFU);
    destination[1] = (uint8_t)(value >> 8U);
}

static void BMS_Persistence_PutU32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & 0xFFUL);
    destination[1] = (uint8_t)((value >> 8U) & 0xFFUL);
    destination[2] = (uint8_t)((value >> 16U) & 0xFFUL);
    destination[3] = (uint8_t)((value >> 24U) & 0xFFUL);
}

static uint16_t BMS_Persistence_GetU16(const uint8_t *source)
{
    return (uint16_t)((uint16_t)source[0] |
        (uint16_t)((uint16_t)source[1] << 8U));
}

static uint32_t BMS_Persistence_GetU32(const uint8_t *source)
{
    return (uint32_t)source[0] |
        ((uint32_t)source[1] << 8U) |
        ((uint32_t)source[2] << 16U) |
        ((uint32_t)source[3] << 24U);
}

uint32_t BMS_Persistence_Crc32(const uint8_t *data, uint16_t length)
{
    uint32_t crc;
    uint16_t index;
    uint8_t bit;

    if ((data == NULL) && (length != 0U))
    {
        return 0UL;
    }
    crc = 0xFFFFFFFFUL;
    for (index = 0U; index < length; ++index)
    {
        crc ^= data[index];
        for (bit = 0U; bit < 8U; ++bit)
        {
            crc = (crc >> 1U) ^
                ((crc & 1UL) != 0UL ? 0xEDB88320UL : 0UL);
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

bool BMS_Persistence_Encode(
    const BMS_PersistencePayload_t *payload,
    uint8_t record[BMS_PERSISTENCE_RECORD_BYTES])
{
    uint32_t crc;

    if ((payload == NULL) || (record == NULL) ||
        (payload->soc_permille > 1000U))
    {
        return false;
    }
    (void)memset(record, 0, BMS_PERSISTENCE_RECORD_BYTES);
    BMS_Persistence_PutU32(&record[0], BMS_PERSISTENCE_MAGIC);
    BMS_Persistence_PutU16(&record[4], BMS_PERSISTENCE_MODEL_VERSION);
    BMS_Persistence_PutU16(&record[6], BMS_PERSISTENCE_RECORD_BYTES);
    BMS_Persistence_PutU32(&record[8], payload->sequence);
    BMS_Persistence_PutU16(&record[12], payload->soc_permille);
    BMS_Persistence_PutU32(&record[16], payload->remaining_capacity_mah);
    BMS_Persistence_PutU32(&record[20], payload->persistent_counter0);
    BMS_Persistence_PutU32(&record[24], payload->persistent_counter1);
    crc = BMS_Persistence_Crc32(record, BMS_PERSISTENCE_CRC_OFFSET);
    BMS_Persistence_PutU32(&record[BMS_PERSISTENCE_CRC_OFFSET], crc);
    BMS_Persistence_PutU16(&record[BMS_PERSISTENCE_COMMIT_OFFSET],
                           BMS_PERSISTENCE_COMMIT_MARKER);
    return true;
}

bool BMS_Persistence_Decode(
    const uint8_t record[BMS_PERSISTENCE_RECORD_BYTES],
    BMS_PersistencePayload_t *payload)
{
    BMS_PersistencePayload_t decoded;
    uint32_t expected_crc;

    if ((record == NULL) || (payload == NULL) ||
        (BMS_Persistence_GetU32(&record[0]) != BMS_PERSISTENCE_MAGIC) ||
        (BMS_Persistence_GetU16(&record[4]) !=
         BMS_PERSISTENCE_MODEL_VERSION) ||
        (BMS_Persistence_GetU16(&record[6]) !=
         BMS_PERSISTENCE_RECORD_BYTES) ||
        (BMS_Persistence_GetU16(&record[
            BMS_PERSISTENCE_COMMIT_OFFSET]) !=
         BMS_PERSISTENCE_COMMIT_MARKER))
    {
        return false;
    }
    expected_crc = BMS_Persistence_Crc32(
        record, BMS_PERSISTENCE_CRC_OFFSET);
    if (BMS_Persistence_GetU32(
            &record[BMS_PERSISTENCE_CRC_OFFSET]) != expected_crc)
    {
        return false;
    }
    decoded.sequence = BMS_Persistence_GetU32(&record[8]);
    decoded.soc_permille = BMS_Persistence_GetU16(&record[12]);
    decoded.remaining_capacity_mah = BMS_Persistence_GetU32(&record[16]);
    decoded.persistent_counter0 = BMS_Persistence_GetU32(&record[20]);
    decoded.persistent_counter1 = BMS_Persistence_GetU32(&record[24]);
    if (decoded.soc_permille > 1000U)
    {
        return false;
    }
    *payload = decoded;
    return true;
}

BMS_PersistenceSlot_t BMS_Persistence_SelectNewest(
    const uint8_t slot_a[BMS_PERSISTENCE_RECORD_BYTES],
    const uint8_t slot_b[BMS_PERSISTENCE_RECORD_BYTES],
    BMS_PersistencePayload_t *payload)
{
    BMS_PersistencePayload_t decoded_a;
    BMS_PersistencePayload_t decoded_b;
    bool valid_a;
    bool valid_b;

    if (payload == NULL)
    {
        return BMS_PERSISTENCE_SLOT_NONE;
    }
    valid_a = BMS_Persistence_Decode(slot_a, &decoded_a);
    valid_b = BMS_Persistence_Decode(slot_b, &decoded_b);
    if (!valid_a && !valid_b)
    {
        return BMS_PERSISTENCE_SLOT_NONE;
    }
    if (valid_a && (!valid_b ||
        ((int32_t)(decoded_a.sequence - decoded_b.sequence) > 0)))
    {
        *payload = decoded_a;
        return BMS_PERSISTENCE_SLOT_A;
    }
    *payload = decoded_b;
    return BMS_PERSISTENCE_SLOT_B;
}

static void BMS_Persistence_SaturatingIncrement(uint32_t *value)
{
    if (*value != UINT32_MAX)
    {
        ++(*value);
    }
}

static bool BMS_Persistence_StoreConfigurationValid(
    const BMS_FlashPolicy_t *policy,
    const BMS_PersistenceStorageOps_t *storage)
{
    return (policy != NULL) && (storage != NULL) &&
        (storage->read != NULL) && (storage->erase_page != NULL) &&
        (storage->program_halfword != NULL) &&
        (policy->page_size_bytes >= BMS_PERSISTENCE_RECORD_BYTES) &&
        ((policy->slot_a_address & 1UL) == 0UL) &&
        ((policy->slot_b_address & 1UL) == 0UL) &&
        ((policy->slot_b_address - policy->slot_a_address) ==
         (uint32_t)policy->page_size_bytes) &&
        (policy->minimum_save_interval_ms != 0UL) &&
        (policy->soc_change_trigger_permille != 0U) &&
        (policy->soc_change_trigger_permille <= 1000U);
}

bool BMS_Persistence_StoreInit(
    BMS_PersistenceStore_t *store,
    const BMS_FlashPolicy_t *policy,
    const BMS_PersistenceStorageOps_t *storage)
{
    uint8_t slot_a[BMS_PERSISTENCE_RECORD_BYTES];
    uint8_t slot_b[BMS_PERSISTENCE_RECORD_BYTES];
    bool read_a;
    bool read_b;

    if ((store == NULL) ||
        !BMS_Persistence_StoreConfigurationValid(policy, storage))
    {
        return false;
    }
    (void)memset(store, 0, sizeof(*store));
    store->policy = *policy;
    store->storage = *storage;
    read_a = store->storage.read(store->storage.context,
        policy->slot_a_address, slot_a, sizeof(slot_a));
    read_b = store->storage.read(store->storage.context,
        policy->slot_b_address, slot_b, sizeof(slot_b));
    if (!read_a || !read_b)
    {
        BMS_Persistence_SaturatingIncrement(
            &store->diagnostics.load_io_failure_count);
        if (!read_a) { (void)memset(slot_a, 0, sizeof(slot_a)); }
        if (!read_b) { (void)memset(slot_b, 0, sizeof(slot_b)); }
    }
    store->active_slot = BMS_Persistence_SelectNewest(
        slot_a, slot_b, &store->cached);
    store->have_active = store->active_slot != BMS_PERSISTENCE_SLOT_NONE;
    if (!store->have_active)
    {
        BMS_Persistence_SaturatingIncrement(
            &store->diagnostics.both_invalid_count);
    }
    store->initialized = true;
    return true;
}

bool BMS_Persistence_StoreGetLatest(
    const BMS_PersistenceStore_t *store,
    BMS_PersistencePayload_t *payload)
{
    if ((store == NULL) || (payload == NULL) ||
        !store->initialized || !store->have_active)
    {
        return false;
    }
    *payload = store->cached;
    return true;
}

static BMS_PersistenceStoreResult_t BMS_Persistence_StoreRecord(
    BMS_PersistenceStore_t *store,
    const BMS_PersistencePayload_t *requested)
{
    BMS_PersistencePayload_t candidate;
    BMS_PersistencePayload_t verified;
    uint8_t record[BMS_PERSISTENCE_RECORD_BYTES];
    uint8_t readback[BMS_PERSISTENCE_RECORD_BYTES];
    uint32_t target_address;
    BMS_PersistenceSlot_t target_slot;
    uint16_t offset;
    uint16_t halfword;

    candidate = *requested;
    candidate.sequence = store->have_active ?
        store->cached.sequence + 1UL : 1UL;
    if (!BMS_Persistence_Encode(&candidate, record))
    {
        return BMS_PERSISTENCE_STORE_REJECTED;
    }
    target_slot = store->active_slot == BMS_PERSISTENCE_SLOT_A ?
        BMS_PERSISTENCE_SLOT_B : BMS_PERSISTENCE_SLOT_A;
    target_address = target_slot == BMS_PERSISTENCE_SLOT_A ?
        store->policy.slot_a_address : store->policy.slot_b_address;

    if (!store->storage.erase_page(store->storage.context, target_address))
    {
        BMS_Persistence_SaturatingIncrement(
            &store->diagnostics.save_io_failure_count);
        return BMS_PERSISTENCE_STORE_IO_ERROR;
    }
    /*
     * 先 erase inactive page，再写 CRC 覆盖的 payload/body 并逐字节 readback；
     * commit halfword 在最后一步前保持 erased 0xFFFF，使任一中途掉电得到的
     * candidate 都是 invalid。只有 body 完整验证后才写 commit marker，因此旧
     * slot 至少一直可用到新 slot 真正提交。
     */
    for (offset = 0U; offset < BMS_PERSISTENCE_BODY_BYTES; offset += 2U)
    {
        halfword = BMS_Persistence_GetU16(&record[offset]);
        if (!store->storage.program_halfword(store->storage.context,
                target_address + offset, halfword))
        {
            BMS_Persistence_SaturatingIncrement(
                &store->diagnostics.save_io_failure_count);
            return BMS_PERSISTENCE_STORE_IO_ERROR;
        }
    }
    if (!store->storage.read(store->storage.context, target_address,
                             readback, sizeof(readback)) ||
        (memcmp(readback, record, BMS_PERSISTENCE_BODY_BYTES) != 0) ||
        BMS_Persistence_Decode(readback, &verified))
    {
        BMS_Persistence_SaturatingIncrement(
            &store->diagnostics.save_verify_failure_count);
        return BMS_PERSISTENCE_STORE_VERIFY_ERROR;
    }
    halfword = BMS_Persistence_GetU16(
        &record[BMS_PERSISTENCE_COMMIT_OFFSET]);
    if (!store->storage.program_halfword(store->storage.context,
            target_address + BMS_PERSISTENCE_COMMIT_OFFSET, halfword))
    {
        BMS_Persistence_SaturatingIncrement(
            &store->diagnostics.save_io_failure_count);
        return BMS_PERSISTENCE_STORE_IO_ERROR;
    }
    if (!store->storage.read(store->storage.context, target_address,
                             readback, sizeof(readback)) ||
        !BMS_Persistence_Decode(readback, &verified) ||
        (verified.sequence != candidate.sequence) ||
        (verified.soc_permille != candidate.soc_permille) ||
        (verified.remaining_capacity_mah !=
         candidate.remaining_capacity_mah) ||
        (verified.persistent_counter0 != candidate.persistent_counter0) ||
        (verified.persistent_counter1 != candidate.persistent_counter1))
    {
        BMS_Persistence_SaturatingIncrement(
            &store->diagnostics.save_verify_failure_count);
        return BMS_PERSISTENCE_STORE_VERIFY_ERROR;
    }

    store->cached = verified;
    store->active_slot = target_slot;
    store->have_active = true;
    BMS_Persistence_SaturatingIncrement(
        &store->diagnostics.save_success_count);
    return BMS_PERSISTENCE_STORE_SAVED;
}

BMS_PersistenceStoreResult_t BMS_Persistence_StoreSocIfDue(
    BMS_PersistenceStore_t *store,
    uint16_t soc_permille,
    uint32_t remaining_capacity_mah,
    uint32_t persistent_counter0,
    uint32_t persistent_counter1,
    bool valid,
    uint32_t now_ms)
{
    BMS_PersistencePayload_t requested;
    uint16_t change;
    BMS_PersistenceStoreResult_t result;

    if ((store == NULL) || !store->initialized || !valid ||
        (soc_permille > 1000U))
    {
        return BMS_PERSISTENCE_STORE_REJECTED;
    }
    if ((uint32_t)(now_ms - store->last_save_ms) <
        store->policy.minimum_save_interval_ms)
    {
        BMS_Persistence_SaturatingIncrement(
            &store->diagnostics.save_not_due_count);
        return BMS_PERSISTENCE_STORE_NOT_DUE;
    }
    if (store->have_active)
    {
        change = soc_permille >= store->cached.soc_permille ?
            (uint16_t)(soc_permille - store->cached.soc_permille) :
            (uint16_t)(store->cached.soc_permille - soc_permille);
        if (change < store->policy.soc_change_trigger_permille)
        {
            BMS_Persistence_SaturatingIncrement(
                &store->diagnostics.save_not_due_count);
            return BMS_PERSISTENCE_STORE_NOT_DUE;
        }
    }
    (void)memset(&requested, 0, sizeof(requested));
    requested.soc_permille = soc_permille;
    requested.remaining_capacity_mah = remaining_capacity_mah;
    requested.persistent_counter0 = persistent_counter0;
    requested.persistent_counter1 = persistent_counter1;
    result = BMS_Persistence_StoreRecord(store, &requested);
    if (result == BMS_PERSISTENCE_STORE_SAVED)
    {
        store->last_save_ms = now_ms;
    }
    return result;
}

BMS_PersistenceDiagnostics_t BMS_Persistence_StoreGetDiagnostics(
    const BMS_PersistenceStore_t *store)
{
    BMS_PersistenceDiagnostics_t diagnostics;

    (void)memset(&diagnostics, 0, sizeof(diagnostics));
    if (store != NULL)
    {
        diagnostics = store->diagnostics;
    }
    return diagnostics;
}

static BMS_PersistenceStore_t s_target_store;

#if !defined(TEST_PHASE9_IMAGE)
static bool BMS_Persistence_TargetRead(void *context, uint32_t address,
                                       uint8_t *destination, uint16_t length)
{
    (void)context;
    return BSP_Flash_Read(address, destination, length);
}

static bool BMS_Persistence_TargetErase(void *context,
                                        uint32_t page_address)
{
    (void)context;
    return BSP_Flash_ErasePersistencePage(page_address);
}

static bool BMS_Persistence_TargetProgram(void *context, uint32_t address,
                                          uint16_t value)
{
    (void)context;
    return BSP_Flash_ProgramPersistenceHalfWord(address, value);
}
#endif

bool BMS_Persistence_TargetInit(const BMS_FlashPolicy_t *policy)
{
#if defined(TEST_PHASE9_IMAGE)
    (void)policy;
    return false;
#else
    BMS_PersistenceStorageOps_t storage;

    storage.read = BMS_Persistence_TargetRead;
    storage.erase_page = BMS_Persistence_TargetErase;
    storage.program_halfword = BMS_Persistence_TargetProgram;
    storage.context = NULL;
    return BMS_Persistence_StoreInit(&s_target_store, policy, &storage);
#endif
}

bool BMS_Persistence_TargetGetLatest(BMS_PersistencePayload_t *payload)
{
    return BMS_Persistence_StoreGetLatest(&s_target_store, payload);
}

BMS_PersistenceStoreResult_t BMS_Persistence_TargetServiceSoc(
    uint16_t soc_permille,
    uint32_t remaining_capacity_mah,
    uint32_t persistent_counter0,
    uint32_t persistent_counter1,
    bool valid,
    uint32_t now_ms)
{
    return BMS_Persistence_StoreSocIfDue(
        &s_target_store, soc_permille, remaining_capacity_mah,
        persistent_counter0, persistent_counter1, valid, now_ms);
}

BMS_PersistenceDiagnostics_t BMS_Persistence_TargetGetDiagnostics(void)
{
    return BMS_Persistence_StoreGetDiagnostics(&s_target_store);
}
