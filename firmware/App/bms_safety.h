#ifndef BMS_SAFETY_H
#define BMS_SAFETY_H

#include <stdint.h>

#include "bms_fault.h"

typedef uint32_t BMS_InhibitReasonBitmap_t;

/* Fault-backed reasons retain the stable fault-ID bit position. */
#define BMS_INHIBIT_REASON_FAULT(id_) \
    ((BMS_InhibitReasonBitmap_t)BMS_Fault_Mask((id_)))

/* Technical reasons occupy bits above the stable 20-fault range. */
#define BMS_INHIBIT_REASON_RECOVERY              \
    ((BMS_InhibitReasonBitmap_t)1UL << 20)
#define BMS_INHIBIT_REASON_POLICY_INVALID        \
    ((BMS_InhibitReasonBitmap_t)1UL << 21)
#define BMS_INHIBIT_REASON_FET_UNVERIFIED        \
    ((BMS_InhibitReasonBitmap_t)1UL << 22)
#define BMS_INHIBIT_REASON_DECISION_STALE        \
    ((BMS_InhibitReasonBitmap_t)1UL << 23)
#define BMS_INHIBIT_REASON_UNKNOWN_SOURCE        \
    ((BMS_InhibitReasonBitmap_t)1UL << 31)

#endif /* BMS_SAFETY_H */
