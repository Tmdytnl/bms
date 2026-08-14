#include "bms_data.h"

BMS_DataSnapshot_t g_bms_data;

static void BMS_Data_InitMeasurement(BMS_MeasurementMetadata_t *metadata)
{
    metadata->timestamp_ms = (BMS_TimestampMs_t)0U;
    metadata->age_ms = BMS_DATA_AGE_UNKNOWN_MS;
    metadata->valid = false;
    metadata->in_range = false;
}

void BMS_Data_Init(void)
{
    uint32_t cell_index;

    for (cell_index = 0U;
         cell_index < (uint32_t)BMS_CELL_COUNT;
         cell_index++)
    {
        g_bms_data.cell_voltage_mv[cell_index] = (BMS_CellVoltageMv_t)0U;
        g_bms_data.cell_metadata.timestamp_ms[cell_index] =
            (BMS_TimestampMs_t)0U;
        g_bms_data.cell_metadata.age_ms[cell_index] =
            BMS_DATA_AGE_UNKNOWN_MS;
    }

    g_bms_data.cell_metadata.valid_bitmap = (uint16_t)0U;
    g_bms_data.cell_metadata.in_range_bitmap = (uint16_t)0U;

    g_bms_data.pack_voltage_mv = (BMS_PackVoltageMv_t)0U;
    g_bms_data.current_ma = (BMS_CurrentMa_t)0;
    g_bms_data.temperature_decic = (BMS_TemperatureDeciC_t)0;
    g_bms_data.remaining_capacity_mah = (BMS_CapacityMah_t)0U;
    g_bms_data.soc_permille = BMS_SOC_UNKNOWN_PERMILLE;

    g_bms_data.state = BMS_STATE_INIT;
    BMS_Fault_Init(&g_bms_data.faults);

    BMS_Data_InitMeasurement(&g_bms_data.pack_metadata);
    BMS_Data_InitMeasurement(&g_bms_data.current_metadata);
    BMS_Data_InitMeasurement(&g_bms_data.temperature_metadata);
    BMS_Data_InitMeasurement(&g_bms_data.soc_metadata);

    g_bms_data.snapshot_timestamp_ms = (BMS_TimestampMs_t)0U;
    g_bms_data.sample_sequence = (uint32_t)0U;
}
