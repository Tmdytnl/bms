#ifndef BMS_PERSISTENCE_H
#define BMS_PERSISTENCE_H

#include <stdbool.h>
#include <stdint.h>

#define BMS_PERSISTENCE_MAGIC                    (0x31534D42UL)
#define BMS_PERSISTENCE_MODEL_VERSION            (1U)
#define BMS_PERSISTENCE_RECORD_BYTES             (32U)

typedef struct
{
    uint32_t sequence;
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

/* Physical erase/program scheduling is intentionally not exposed until a
 * target-safe, bounded Flash execution strategy is demonstrated. */

#endif /* BMS_PERSISTENCE_H */
