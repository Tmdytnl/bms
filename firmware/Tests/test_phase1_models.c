#include <assert.h>
#include <stdint.h>

#include "bms_config.h"
#include "bms_data.h"
#include "bms_fault.h"
#include "bms_memory_map.h"
#include "bms_state.h"
#include "bms_types.h"

static void TestFaultIdsAreUnique(void)
{
    BMS_FaultBitmap_t seen;
    BMS_FaultBitmap_t mask;
    uint32_t fault_index;

    seen = (BMS_FaultBitmap_t)0U;
    for (fault_index = 0U;
         fault_index < (uint32_t)BMS_FAULT_ID_COUNT;
         fault_index++)
    {
        mask = BMS_Fault_Mask((BMS_FaultId_t)fault_index);
        assert(mask != (BMS_FaultBitmap_t)0U);
        assert((seen & mask) == (BMS_FaultBitmap_t)0U);
        seen |= mask;
    }

    assert(seen == BMS_FAULT_DEFINED_MASK);
    assert(BMS_Fault_Mask(BMS_FAULT_ID_COUNT) == (BMS_FaultBitmap_t)0U);
    assert(BMS_Fault_Contains(BMS_FAULT_DEFINED_MASK,
                             BMS_FAULT_ID_HW_OV) == true);
    assert(BMS_Fault_Contains(BMS_FAULT_DEFINED_MASK,
                             BMS_FAULT_ID_COUNT) == false);
}

static void TestSafeDataDefaults(void)
{
    uint32_t cell_index;

    BMS_Data_Init();

    assert(g_bms_data.state == BMS_STATE_INIT);
    assert(g_bms_data.soc_permille == BMS_SOC_UNKNOWN_PERMILLE);
    assert(g_bms_data.soc_metadata.valid == false);
    assert(g_bms_data.soc_metadata.in_range == false);
    assert(g_bms_data.soc_metadata.age_ms == BMS_DATA_AGE_UNKNOWN_MS);
    assert(g_bms_data.soc_metadata.timestamp_ms == (BMS_TimestampMs_t)0U);
    assert(g_bms_data.pack_voltage_mv == (BMS_PackVoltageMv_t)0U);
    assert(g_bms_data.current_ma == (BMS_CurrentMa_t)0);
    assert(g_bms_data.temperature_decic == (BMS_TemperatureDeciC_t)0);
    assert(g_bms_data.remaining_capacity_mah == (BMS_CapacityMah_t)0U);
    assert(g_bms_data.pack_metadata.timestamp_ms == (BMS_TimestampMs_t)0U);
    assert(g_bms_data.pack_metadata.age_ms == BMS_DATA_AGE_UNKNOWN_MS);
    assert(g_bms_data.pack_metadata.valid == false);
    assert(g_bms_data.pack_metadata.in_range == false);
    assert(g_bms_data.current_metadata.timestamp_ms == (BMS_TimestampMs_t)0U);
    assert(g_bms_data.current_metadata.age_ms == BMS_DATA_AGE_UNKNOWN_MS);
    assert(g_bms_data.current_metadata.valid == false);
    assert(g_bms_data.current_metadata.in_range == false);
    assert(g_bms_data.temperature_metadata.timestamp_ms ==
           (BMS_TimestampMs_t)0U);
    assert(g_bms_data.temperature_metadata.age_ms == BMS_DATA_AGE_UNKNOWN_MS);
    assert(g_bms_data.temperature_metadata.valid == false);
    assert(g_bms_data.temperature_metadata.in_range == false);
    assert(g_bms_data.cell_metadata.valid_bitmap == (uint16_t)0U);
    assert(g_bms_data.cell_metadata.in_range_bitmap == (uint16_t)0U);
    assert(g_bms_data.faults.active == (BMS_FaultBitmap_t)0U);
    assert(g_bms_data.faults.latched == (BMS_FaultBitmap_t)0U);
    assert(g_bms_data.snapshot_timestamp_ms == (BMS_TimestampMs_t)0U);
    assert(g_bms_data.sample_sequence == (uint32_t)0U);

    for (cell_index = 0U;
         cell_index < (uint32_t)BMS_CELL_COUNT;
         cell_index++)
    {
        assert(g_bms_data.cell_voltage_mv[cell_index] ==
               (BMS_CellVoltageMv_t)0U);
        assert(g_bms_data.cell_metadata.timestamp_ms[cell_index] ==
               (BMS_TimestampMs_t)0U);
        assert(g_bms_data.cell_metadata.age_ms[cell_index] ==
               BMS_DATA_AGE_UNKNOWN_MS);
    }

    g_bms_data.state = BMS_STATE_FAULT;
    g_bms_data.soc_permille = (BMS_SocPermille_t)500U;
    g_bms_data.soc_metadata.valid = true;
    BMS_Data_Init();
    assert(g_bms_data.state == BMS_STATE_INIT);
    assert(g_bms_data.soc_permille == BMS_SOC_UNKNOWN_PERMILLE);
    assert(g_bms_data.soc_metadata.valid == false);
}

int main(void)
{
    assert(BMS_CELL_COUNT == 13U);
    assert(BMS_STATE_COUNT == 5);
    assert(BMS_SOC_PERMILLE_MAX == 1000U);
    assert(BMS_APP_FLASH_SIZE == 0x0000F400UL);
    assert(BMS_SOC_LOG_ADDR == 0x0800F400UL);
    assert(BMS_PARAM_A_ADDR == 0x0800F800UL);
    assert(BMS_PARAM_B_ADDR == 0x0800FC00UL);

    TestFaultIdsAreUnique();
    TestSafeDataDefaults();
    return 0;
}
