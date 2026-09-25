#include "fml_persistence.h"

/*
 * A/B page record 使用 magic+version+sequence+payload+CRC32+commit marker。
 * decode 只有在格式、CRC 与 commit 全部有效时才接纳；两个 slot 都有效时用
 * wrap-safe sequence 选择 newest-valid。保存永远写 inactive slot，因此掉电前
 * active old slot 保持不动。典型过程：A(seq=10) 正常运行 -> 擦除并写 B(seq=11)
 * -> 校验 body -> 最后提交 B。若在提交前掉电，B 没有 marker，重启仍选择 A。
 */

#include <stddef.h>
#include <string.h>

#define BMS_PERSISTENCE_CRC_OFFSET               (28U)
#define BMS_PERSISTENCE_COMMIT_OFFSET            (32U)

/* 以固定小端字节序编码持久化 16 位字段。 */
static void FML_Persistence_PutU16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value & 0xFFU);
    destination[1] = (uint8_t)(value >> 8U);
}

/* 以固定小端字节序编码持久化 32 位字段。 */
static void FML_Persistence_PutU32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & 0xFFUL);
    destination[1] = (uint8_t)((value >> 8U) & 0xFFUL);
    destination[2] = (uint8_t)((value >> 16U) & 0xFFUL);
    destination[3] = (uint8_t)((value >> 24U) & 0xFFUL);
}

/* 从小端记录字节恢复 16 位持久化字段。 */
static uint16_t FML_Persistence_GetU16(const uint8_t *source)
{
    return (uint16_t)((uint16_t)source[0] |
        (uint16_t)((uint16_t)source[1] << 8U));
}

/* 从小端记录字节恢复 32 位持久化字段。 */
static uint32_t FML_Persistence_GetU32(const uint8_t *source)
{
    return (uint32_t)source[0] |
        ((uint32_t)source[1] << 8U) |
        ((uint32_t)source[2] << 16U) |
        ((uint32_t)source[3] << 24U);
}

/* 对编码记录计算 CRC32，用于掉电后有效性判断。 */
uint32_t FML_Persistence_Crc32(const uint8_t *data, uint16_t length)
{
    /* 当前 CRC 累积值。 */
    uint32_t crc;
    /* 当前持久化记录中的字节索引。 */
    uint16_t index;
    /* 当前检查的单个位。 */
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

/* 按固定版本与字节序编码持久化记录及 CRC。 */
bool FML_Persistence_Encode(
    const BMS_PersistencePayload_t *payload,
    uint8_t record[BMS_PERSISTENCE_RECORD_BYTES])
{
    /* 当前 CRC 累积值。 */
    uint32_t crc;

    if ((payload == NULL) || (record == NULL) ||
        (payload->soc_permille > 1000U))
    {
        return false;
    }
    (void)memset(record, 0, BMS_PERSISTENCE_RECORD_BYTES);
    FML_Persistence_PutU32(&record[0], BMS_PERSISTENCE_MAGIC);
    FML_Persistence_PutU16(&record[4], BMS_PERSISTENCE_MODEL_VERSION);
    FML_Persistence_PutU16(&record[6], BMS_PERSISTENCE_RECORD_BYTES);
    FML_Persistence_PutU32(&record[8], payload->sequence);
    FML_Persistence_PutU16(&record[12], payload->soc_permille);
    FML_Persistence_PutU32(&record[16], payload->remaining_capacity_mah);
    FML_Persistence_PutU32(&record[20], payload->persistent_counter0);
    FML_Persistence_PutU32(&record[24], payload->persistent_counter1);
    crc = FML_Persistence_Crc32(record, BMS_PERSISTENCE_CRC_OFFSET);
    FML_Persistence_PutU32(&record[BMS_PERSISTENCE_CRC_OFFSET], crc);
    FML_Persistence_PutU16(&record[BMS_PERSISTENCE_COMMIT_OFFSET],
                           BMS_PERSISTENCE_COMMIT_MARKER);
    return true;
}

/* 校验记录版本、提交标记与 CRC 后解码载荷。 */
bool FML_Persistence_Decode(
    const uint8_t record[BMS_PERSISTENCE_RECORD_BYTES],
    BMS_PersistencePayload_t *payload)
{
    /* 从原始记录解码得到的值。 */
    BMS_PersistencePayload_t decoded;
    /* 依据数据重新计算的 CRC 值。 */
    uint32_t expected_crc;

    if ((record == NULL) || (payload == NULL) ||
        (FML_Persistence_GetU32(&record[0]) != BMS_PERSISTENCE_MAGIC) ||
        (FML_Persistence_GetU16(&record[4]) !=
         BMS_PERSISTENCE_MODEL_VERSION) ||
        (FML_Persistence_GetU16(&record[6]) !=
         BMS_PERSISTENCE_RECORD_BYTES) ||
        (FML_Persistence_GetU16(&record[
            BMS_PERSISTENCE_COMMIT_OFFSET]) !=
         BMS_PERSISTENCE_COMMIT_MARKER))
    {
        return false;
    }
    expected_crc = FML_Persistence_Crc32(
        record, BMS_PERSISTENCE_CRC_OFFSET);
    if (FML_Persistence_GetU32(
            &record[BMS_PERSISTENCE_CRC_OFFSET]) != expected_crc)
    {
        return false;
    }
    decoded.sequence = FML_Persistence_GetU32(&record[8]);
    decoded.soc_permille = FML_Persistence_GetU16(&record[12]);
    decoded.remaining_capacity_mah = FML_Persistence_GetU32(&record[16]);
    decoded.persistent_counter0 = FML_Persistence_GetU32(&record[20]);
    decoded.persistent_counter1 = FML_Persistence_GetU32(&record[24]);
    if (decoded.soc_permille > 1000U)
    {
        return false;
    }
    *payload = decoded;
    return true;
}

/* 按提交标记与回绕序号选出 A/B 页中的最新有效记录。 */
BMS_PersistenceSlot_t FML_Persistence_SelectNewest(
    const uint8_t slot_a[BMS_PERSISTENCE_RECORD_BYTES],
    const uint8_t slot_b[BMS_PERSISTENCE_RECORD_BYTES],
    BMS_PersistencePayload_t *payload)
{
    /* 从持久化 A 槽解码得到的载荷。 */
    BMS_PersistencePayload_t decoded_a;
    /* 从持久化 B 槽解码得到的载荷。 */
    BMS_PersistencePayload_t decoded_b;
    /* A 槽持久化记录是否通过校验。 */
    bool valid_a;
    /* B 槽持久化记录是否通过校验。 */
    bool valid_b;

    if (payload == NULL)
    {
        return BMS_PERSISTENCE_SLOT_NONE;
    }
    valid_a = FML_Persistence_Decode(slot_a, &decoded_a);
    valid_b = FML_Persistence_Decode(slot_b, &decoded_b);
    /* 单页有效时直接选它；双页有效时才比较序号，坏页绝不参与“新旧”判断。 */
    if (!valid_a && !valid_b)
    {
        return BMS_PERSISTENCE_SLOT_NONE;
    }
    if (valid_a && (!valid_b ||
        ((int32_t)(decoded_a.sequence - decoded_b.sequence) > 0)))
    {
        /* 有符号差让 UINT32_MAX -> 0 的自然回绕仍保持正确先后关系。 */
        *payload = decoded_a;
        return BMS_PERSISTENCE_SLOT_A;
    }
    *payload = decoded_b;
    return BMS_PERSISTENCE_SLOT_B;
}

/* 饱和累计 Flash 诊断计数，防止长期运行时回绕。 */
static void FML_Persistence_SaturatingIncrement(uint32_t *value)
{
    if (*value != UINT32_MAX)
    {
        ++(*value);
    }
}

/* 校验存储页地址、大小及回调集合满足 A/B 事务要求。 */
static bool FML_Persistence_StoreConfigurationValid(
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

/* 建立 A/B 页持久化上下文和未提交初始状态。 */
bool FML_Persistence_StoreInit(
    BMS_PersistenceStore_t *store,
    const BMS_FlashPolicy_t *policy,
    const BMS_PersistenceStorageOps_t *storage)
{
    /* 从持久化 A 槽读取的原始记录。 */
    uint8_t slot_a[BMS_PERSISTENCE_RECORD_BYTES];
    /* 从持久化 B 槽读取的原始记录。 */
    uint8_t slot_b[BMS_PERSISTENCE_RECORD_BYTES];
    /* A 槽记录是否读取成功。 */
    bool read_a;
    /* B 槽记录是否读取成功。 */
    bool read_b;

    if ((store == NULL) ||
        !FML_Persistence_StoreConfigurationValid(policy, storage))
    {
        return false;
    }
    (void)memset(store, 0, sizeof(*store));
    store->policy = *policy;
    store->storage = *storage;
    /*
     * 启动阶段只读两页：即使一页损坏，也不立即擦写。这样上电恢复不会制造
     * 一次额外的 Flash 磨损，也不会在供电仍不稳定时破坏仅存的恢复证据。
     */
    read_a = store->storage.read(store->storage.context,
        policy->slot_a_address, slot_a, sizeof(slot_a));
    read_b = store->storage.read(store->storage.context,
        policy->slot_b_address, slot_b, sizeof(slot_b));
    if (!read_a || !read_b)
    {
        FML_Persistence_SaturatingIncrement(
            &store->diagnostics.load_io_failure_count);
        if (!read_a) { (void)memset(slot_a, 0, sizeof(slot_a)); }
        if (!read_b) { (void)memset(slot_b, 0, sizeof(slot_b)); }
    }
    store->active_slot = FML_Persistence_SelectNewest(
        slot_a, slot_b, &store->cached);
    /* cached 只在 SelectNewest 成功时有意义，have_active 是其有效性所有者。 */
    store->have_active = store->active_slot != BMS_PERSISTENCE_SLOT_NONE;
    if (!store->have_active)
    {
        FML_Persistence_SaturatingIncrement(
            &store->diagnostics.both_invalid_count);
    }
    store->initialized = true;
    return true;
}

/* 从两个页中选择最新有效记录，不把损坏页当作新状态。 */
bool FML_Persistence_StoreGetLatest(
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

/* 向备用 Flash 页写入记录体、回读验证，再最后写提交标记。 */
static BMS_PersistenceStoreResult_t FML_Persistence_StoreRecord(
    BMS_PersistenceStore_t *store,
    const BMS_PersistencePayload_t *requested)
{
    /* 待校验的候选值。 */
    BMS_PersistencePayload_t candidate;
    /* 读回结果是否通过校验。 */
    BMS_PersistencePayload_t verified;
    /* 准备写入 Flash 的完整持久化记录。 */
    uint8_t record[BMS_PERSISTENCE_RECORD_BYTES];
    /* 写入后从 Flash 读回的校验缓冲区。 */
    uint8_t readback[BMS_PERSISTENCE_RECORD_BYTES];
    /* 本次写入目标的 Flash 地址。 */
    uint32_t target_address;
    /* 本次持久化写入选择的目标槽。 */
    BMS_PersistenceSlot_t target_slot;
    /* 当前数据或寄存器字段的偏移量。 */
    uint16_t offset;
    /* 本次写入 Flash 的 16 位半字。 */
    uint16_t halfword;

    candidate = *requested;
    /* sequence 属于存储层；调用者不能伪造“更新”的记录身份。 */
    candidate.sequence = store->have_active ?
        store->cached.sequence + 1UL : 1UL;
    if (!FML_Persistence_Encode(&candidate, record))
    {
        return BMS_PERSISTENCE_STORE_REJECTED;
    }
    target_slot = store->active_slot == BMS_PERSISTENCE_SLOT_A ?
        BMS_PERSISTENCE_SLOT_B : BMS_PERSISTENCE_SLOT_A;
    target_address = target_slot == BMS_PERSISTENCE_SLOT_A ?
        store->policy.slot_a_address : store->policy.slot_b_address;

    /* 永远选择 inactive 页，整个失败窗口内都不触碰当前 active 页。 */
    if (!store->storage.erase_page(store->storage.context, target_address))
    {
        FML_Persistence_SaturatingIncrement(
            &store->diagnostics.save_io_failure_count);
        return BMS_PERSISTENCE_STORE_IO_ERROR;
    }
    /*
     * 先 erase inactive page，再写 CRC 覆盖的 payload/body 并逐字节 readback；
     * commit halfword 在最后一步前保持 erased 0xFFFF，使任一中途掉电得到的
     * candidate 都是 invalid。只有 body 完整验证后才写 commit marker，因此旧
     * slot 至少一直可用到新 slot 真正提交。这里第一次 Decode 必须失败：body
     * 虽然正确，但 marker 尚未写入；若意外成功，说明后端未真正擦除提交位置。
     */
    for (offset = 0U; offset < BMS_PERSISTENCE_BODY_BYTES; offset += 2U)
    {
        halfword = FML_Persistence_GetU16(&record[offset]);
        if (!store->storage.program_halfword(store->storage.context,
                target_address + offset, halfword))
        {
            FML_Persistence_SaturatingIncrement(
                &store->diagnostics.save_io_failure_count);
            return BMS_PERSISTENCE_STORE_IO_ERROR;
        }
    }
    if (!store->storage.read(store->storage.context, target_address,
                             readback, sizeof(readback)) ||
        (memcmp(readback, record, BMS_PERSISTENCE_BODY_BYTES) != 0) ||
        FML_Persistence_Decode(readback, &verified))
    {
        FML_Persistence_SaturatingIncrement(
            &store->diagnostics.save_verify_failure_count);
        return BMS_PERSISTENCE_STORE_VERIFY_ERROR;
    }
    halfword = FML_Persistence_GetU16(
        &record[BMS_PERSISTENCE_COMMIT_OFFSET]);
    /* commit marker 是唯一“发布点”；此前新页只是候选记录。 */
    if (!store->storage.program_halfword(store->storage.context,
            target_address + BMS_PERSISTENCE_COMMIT_OFFSET, halfword))
    {
        FML_Persistence_SaturatingIncrement(
            &store->diagnostics.save_io_failure_count);
        return BMS_PERSISTENCE_STORE_IO_ERROR;
    }
    if (!store->storage.read(store->storage.context, target_address,
                             readback, sizeof(readback)) ||
        !FML_Persistence_Decode(readback, &verified) ||
        (verified.sequence != candidate.sequence) ||
        (verified.soc_permille != candidate.soc_permille) ||
        (verified.remaining_capacity_mah !=
         candidate.remaining_capacity_mah) ||
        (verified.persistent_counter0 != candidate.persistent_counter0) ||
        (verified.persistent_counter1 != candidate.persistent_counter1))
    {
        FML_Persistence_SaturatingIncrement(
            &store->diagnostics.save_verify_failure_count);
        return BMS_PERSISTENCE_STORE_VERIFY_ERROR;
    }

    /* 只有提交后完整读回一致，RAM 的 active/cached 身份才原子式切换到新页。 */
    store->cached = verified;
    store->active_slot = target_slot;
    store->have_active = true;
    FML_Persistence_SaturatingIncrement(
        &store->diagnostics.save_success_count);
    return BMS_PERSISTENCE_STORE_SAVED;
}

/* 按时间与变化门限决定是否提交 SOC，避免无意义的 Flash 擦写。 */
BMS_PersistenceStoreResult_t FML_Persistence_StoreSocIfDue(
    BMS_PersistenceStore_t *store,
    uint16_t soc_permille,
    uint32_t remaining_capacity_mah,
    uint32_t persistent_counter0,
    uint32_t persistent_counter1,
    bool valid,
    uint32_t now_ms)
{
    /* 等待保存的最新持久化载荷。 */
    BMS_PersistencePayload_t requested;
    /* 本轮持久化载荷发生变化的字段位。 */
    uint16_t change;
    /* 本次持久化写入结果，用于决定是否保留待写请求。 */
    BMS_PersistenceStoreResult_t result;

    if ((store == NULL) || !store->initialized || !valid ||
        (soc_permille > 1000U))
    {
        return BMS_PERSISTENCE_STORE_REJECTED;
    }
    if ((uint32_t)(now_ms - store->last_save_ms) <
        store->policy.minimum_save_interval_ms)
    {
        /* 时间门限先限流，避免 SOC 高频抖动把 Flash 当作运行时日志使用。 */
        FML_Persistence_SaturatingIncrement(
            &store->diagnostics.save_not_due_count);
        return BMS_PERSISTENCE_STORE_NOT_DUE;
    }
    if (store->have_active)
    {
        /* 已有基准时还需跨过 SOC 变化门限；首次有效样本不受该门限阻挡。 */
        change = soc_permille >= store->cached.soc_permille ?
            (uint16_t)(soc_permille - store->cached.soc_permille) :
            (uint16_t)(store->cached.soc_permille - soc_permille);
        if (change < store->policy.soc_change_trigger_permille)
        {
            FML_Persistence_SaturatingIncrement(
                &store->diagnostics.save_not_due_count);
            return BMS_PERSISTENCE_STORE_NOT_DUE;
        }
    }
    (void)memset(&requested, 0, sizeof(requested));
    requested.soc_permille = soc_permille;
    requested.remaining_capacity_mah = remaining_capacity_mah;
    requested.persistent_counter0 = persistent_counter0;
    requested.persistent_counter1 = persistent_counter1;
    result = FML_Persistence_StoreRecord(store, &requested);
    if (result == BMS_PERSISTENCE_STORE_SAVED)
    {
        /* 失败不能推进节流时间，否则会掩盖下一次本应立即进行的重试。 */
        store->last_save_ms = now_ms;
    }
    return result;
}

/* 复制 A/B 页存储算法的诊断计数。 */
BMS_PersistenceDiagnostics_t FML_Persistence_StoreGetDiagnostics(
    const BMS_PersistenceStore_t *store)
{
    /* 本轮汇总的诊断计数快照。 */
    BMS_PersistenceDiagnostics_t diagnostics;

    (void)memset(&diagnostics, 0, sizeof(diagnostics));
    if (store != NULL)
    {
        diagnostics = store->diagnostics;
    }
    return diagnostics;
}

/* 目标 Flash A/B 页的单一持久化上下文与诊断状态。 */
static BMS_PersistenceStore_t s_target_store;

/* 绑定目标 Flash 操作回调并初始化持久化状态。 */
bool FML_Persistence_TargetInit(
    const BMS_FlashPolicy_t *policy,
    const BMS_PersistenceStorageOps_t *storage)
{
    return FML_Persistence_StoreInit(&s_target_store, policy, storage);
}

/* 从目标 Flash 的 A/B 页读取最新有效载荷。 */
bool FML_Persistence_TargetGetLatest(BMS_PersistencePayload_t *payload)
{
    return FML_Persistence_StoreGetLatest(&s_target_store, payload);
}

/* 用当前 SOC 快照服务受节流约束的目标 Flash 写入。 */
BMS_PersistenceStoreResult_t FML_Persistence_TargetServiceSoc(
    uint16_t soc_permille,
    uint32_t remaining_capacity_mah,
    uint32_t persistent_counter0,
    uint32_t persistent_counter1,
    bool valid,
    uint32_t now_ms)
{
    return FML_Persistence_StoreSocIfDue(
        &s_target_store, soc_permille, remaining_capacity_mah,
        persistent_counter0, persistent_counter1, valid, now_ms);
}

/* 复制目标持久化服务的诊断状态。 */
BMS_PersistenceDiagnostics_t FML_Persistence_TargetGetDiagnostics(void)
{
    return FML_Persistence_StoreGetDiagnostics(&s_target_store);
}
