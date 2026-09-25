#include "fml_fault.h"

/* 把 active 与 latched 故障位图初始化为空集合。 */
void FML_Fault_Init(BMS_FaultSummary_t *summary)
{
    if (summary != (BMS_FaultSummary_t *)0)
    {
        summary->active = (BMS_FaultBitmap_t)0U;
        summary->latched = (BMS_FaultBitmap_t)0U;
    }
}

/* 检查 fault ID 是否位于已定义枚举范围。 */
bool FML_Fault_IdIsValid(BMS_FaultId_t fault_id)
{
    return ((uint32_t)fault_id < (uint32_t)BMS_FAULT_ID_COUNT);
}

/* 把合法 fault ID 转为对应位图掩码。 */
BMS_FaultBitmap_t FML_Fault_Mask(BMS_FaultId_t fault_id)
{
    if (!FML_Fault_IdIsValid(fault_id))
    {
        return (BMS_FaultBitmap_t)0U;
    }

    return ((BMS_FaultBitmap_t)1U << (uint32_t)fault_id);
}

/* 检查 fault 位图是否包含指定合法 ID。 */
bool FML_Fault_Contains(BMS_FaultBitmap_t bitmap, BMS_FaultId_t fault_id)
{
    /* 当前检查或写入的位掩码。 */
    BMS_FaultBitmap_t mask;

    mask = FML_Fault_Mask(fault_id);
    return ((mask != (BMS_FaultBitmap_t)0U) &&
            ((bitmap & mask) != (BMS_FaultBitmap_t)0U));
}
