#include "bms_fault.h"

void BMS_Fault_Init(BMS_FaultSummary_t *summary)
{
    if (summary != (BMS_FaultSummary_t *)0)
    {
        summary->active = (BMS_FaultBitmap_t)0U;
        summary->latched = (BMS_FaultBitmap_t)0U;
    }
}

bool BMS_Fault_IdIsValid(BMS_FaultId_t fault_id)
{
    return ((uint32_t)fault_id < (uint32_t)BMS_FAULT_ID_COUNT);
}

BMS_FaultBitmap_t BMS_Fault_Mask(BMS_FaultId_t fault_id)
{
    if (!BMS_Fault_IdIsValid(fault_id))
    {
        return (BMS_FaultBitmap_t)0U;
    }

    return ((BMS_FaultBitmap_t)1U << (uint32_t)fault_id);
}

bool BMS_Fault_Contains(BMS_FaultBitmap_t bitmap, BMS_FaultId_t fault_id)
{
    BMS_FaultBitmap_t mask;

    mask = BMS_Fault_Mask(fault_id);
    return ((mask != (BMS_FaultBitmap_t)0U) &&
            ((bitmap & mask) != (BMS_FaultBitmap_t)0U));
}
