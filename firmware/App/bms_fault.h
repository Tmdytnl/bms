#ifndef BMS_FAULT_H
#define BMS_FAULT_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_build_assert.h"

/*
 * Stable Phase 1 fault identifiers. Do not reorder after protocol or
 * persistence encodings are introduced; those encodings remain explicit.
 */
typedef enum
{
    BMS_FAULT_ID_HW_OV = 0,
    BMS_FAULT_ID_HW_UV = 1,
    BMS_FAULT_ID_HW_OCD = 2,
    BMS_FAULT_ID_HW_SCD = 3,
    BMS_FAULT_ID_AFE_XREADY = 4,
    BMS_FAULT_ID_AFE_OVRD_ALERT = 5,
    BMS_FAULT_ID_AFE_COMM = 6,
    BMS_FAULT_ID_AFE_CRC = 7,
    BMS_FAULT_ID_AFE_STALE = 8,
    BMS_FAULT_ID_SW_OV = 9,
    BMS_FAULT_ID_SW_UV = 10,
    BMS_FAULT_ID_SW_OC_CHARGE = 11,
    BMS_FAULT_ID_SW_OC_DISCHARGE = 12,
    BMS_FAULT_ID_TEMPERATURE_HIGH = 13,
    BMS_FAULT_ID_TEMPERATURE_LOW = 14,
    BMS_FAULT_ID_DATA_STALE = 15,
    BMS_FAULT_ID_CAN = 16,
    BMS_FAULT_ID_FLASH_CONFIG = 17,
    BMS_FAULT_ID_CLOCK = 18,
    BMS_FAULT_ID_RTOS_HEALTH = 19,
    BMS_FAULT_ID_COUNT = 20
} BMS_FaultId_t;

typedef uint32_t BMS_FaultBitmap_t;

typedef struct
{
    /* An unresolved safety condition or captured event whose source owner has
     * not yet proved the configured recovery contract. For W1C event sources,
     * a cleared hardware status bit alone is not proof of physical recovery. */
    BMS_FaultBitmap_t active;
    /* Retained severe-event history. It is never cleared merely because the
     * corresponding active bit or hardware status bit became zero; an
     * authoritative, source-specific explicit-reset policy owns any clear. */
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

void BMS_Fault_Init(BMS_FaultSummary_t *summary);
bool BMS_Fault_IdIsValid(BMS_FaultId_t fault_id);
BMS_FaultBitmap_t BMS_Fault_Mask(BMS_FaultId_t fault_id);
bool BMS_Fault_Contains(BMS_FaultBitmap_t bitmap, BMS_FaultId_t fault_id);

#endif /* BMS_FAULT_H */
