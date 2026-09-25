#ifndef FML_SAFETY_H
#define FML_SAFETY_H

#include <stdint.h>

#include "fml_fault.h"

typedef uint32_t BMS_InhibitReasonBitmap_t;

/* fault-backed inhibit reason 保持稳定 fault-ID bit position。 */
#define BMS_INHIBIT_REASON_FAULT(id_) \
    ((BMS_InhibitReasonBitmap_t)FML_Fault_Mask((id_)))

/* technical inhibit 使用稳定 20-fault 区间以上 bit，避免与诊断 fault 混淆。 */
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

#endif /* FML_SAFETY_H：include guard */
