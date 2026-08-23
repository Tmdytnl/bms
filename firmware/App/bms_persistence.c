#include "bms_persistence.h"

#include <stddef.h>
#include <string.h>

#define BMS_PERSISTENCE_CRC_OFFSET               (28U)

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
        (payload->soc_permille > 1000U) ||
        (payload->remaining_capacity_mah == 0UL))
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
         BMS_PERSISTENCE_RECORD_BYTES))
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
    if ((decoded.soc_permille > 1000U) ||
        (decoded.remaining_capacity_mah == 0UL))
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
        ((int32_t)(decoded_a.sequence - decoded_b.sequence) >= 0)))
    {
        *payload = decoded_a;
        return BMS_PERSISTENCE_SLOT_A;
    }
    *payload = decoded_b;
    return BMS_PERSISTENCE_SLOT_B;
}
