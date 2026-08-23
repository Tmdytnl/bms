#include "test_phase9_stub.h"

#include <stddef.h>
#include <string.h>

#include "app_rtos.h"
#include "bms_recovery.h"
#include "bq76940_regs.h"

struct QueueDefinition
{
    uint8_t unused;
};

static struct QueueDefinition s_mutex;
static BQ76940_t s_device;
static BMS_DataIdentity_t s_identity;
static BMS_DataSnapshot_t s_measurement;
static BMS_ProtectSafetySnapshot_t s_protect;
static BMS_ProtectXreadyState_t s_xready;
static BMS_ProtectXreadyClearAck_t s_xready_ack;
static uint8_t s_registers[0x60U];
static BQ76940_Status_t s_next_write_status;
static BQ76940_Status_t s_next_read_status;
static bool s_readback_corruption;
static uint32_t s_write_count;
static uint32_t s_handoff_count;
static uint32_t s_invalidation_count;

SemaphoreHandle_t xI2CMutex;
SemaphoreHandle_t xDataMutex;
SemaphoreHandle_t xAfeAlertSem;
QueueHandle_t xCanTxQueue;
QueueHandle_t xCanRxQueue;
QueueHandle_t xCcSampleQueue;
EventGroupHandle_t xSysEvents;

void TestP9_StubReset(void)
{
    (void)memset(&s_measurement, 0, sizeof(s_measurement));
    (void)memset(&s_protect, 0, sizeof(s_protect));
    (void)memset(s_registers, 0, sizeof(s_registers));
    s_device.bus = NULL;
    s_device.initialized = true;
    s_identity.sample_sequence = 1UL;
    s_identity.afe_generation = 0UL;
    s_xready.xready_generation = 0UL;
    s_xready.active = false;
    s_xready_ack.xready_generation = 0UL;
    s_xready_ack.recovery_revision = 0UL;
    s_xready_ack.protect_revision = 0UL;
    s_xready_ack.accepted = false;
    s_xready_ack.finalization_ambiguous = false;
    s_registers[BQ76940_REG_SYS_CTRL2] = 0x40U;
    s_registers[BQ76940_REG_ADCGAIN1] = 0U;
    s_registers[BQ76940_REG_ADCOFFSET] = 0U;
    s_registers[BQ76940_REG_ADCGAIN2] = 0U;
    s_next_write_status = BQ76940_STATUS_OK;
    s_next_read_status = BQ76940_STATUS_OK;
    s_readback_corruption = false;
    s_write_count = 0UL;
    s_handoff_count = 0UL;
    s_invalidation_count = 0UL;
    xI2CMutex = (SemaphoreHandle_t)&s_mutex;
    xDataMutex = (SemaphoreHandle_t)&s_mutex;
    xAfeAlertSem = (SemaphoreHandle_t)&s_mutex;
    xCanTxQueue = NULL;
    xCanRxQueue = NULL;
    xCcSampleQueue = NULL;
    xSysEvents = NULL;
}

void TestP9_SetIdentity(uint32_t sequence, uint32_t afe_generation)
{
    s_identity.sample_sequence = sequence;
    s_identity.afe_generation = afe_generation;
}

void TestP9_SetMeasurement(const BMS_DataSnapshot_t *measurement)
{
    if (measurement != NULL)
    {
        s_measurement = *measurement;
        s_identity.sample_sequence = measurement->sample_sequence;
        s_identity.afe_generation = measurement->afe_generation;
    }
}

void TestP9_SetProtectSnapshot(
    const BMS_ProtectSafetySnapshot_t *snapshot)
{
    if (snapshot != NULL)
    {
        s_protect = *snapshot;
        s_xready.xready_generation = snapshot->xready_generation;
        s_xready.active = snapshot->xready_active;
    }
}

void TestP9_SetXready(uint32_t generation, bool active)
{
    s_xready.xready_generation = generation;
    s_xready.active = active;
    s_protect.xready_generation = generation;
    s_protect.xready_active = active;
    ++s_protect.publication_revision;
}

void TestP9_SetXreadyAck(uint32_t generation,
                         uint32_t recovery_revision,
                         bool accepted,
                         bool ambiguous)
{
    s_xready_ack.xready_generation = generation;
    s_xready_ack.recovery_revision = recovery_revision;
    s_xready_ack.protect_revision = s_protect.publication_revision;
    s_xready_ack.accepted = accepted;
    s_xready_ack.finalization_ambiguous = ambiguous;
}

void TestP9_SetNextWriteStatus(BQ76940_Status_t status)
{
    s_next_write_status = status;
}

void TestP9_SetNextReadStatus(BQ76940_Status_t status)
{
    s_next_read_status = status;
}

void TestP9_SetReadbackCorruption(bool enabled)
{
    s_readback_corruption = enabled;
}

uint8_t TestP9_GetRegister(uint8_t address)
{
    return address < sizeof(s_registers) ? s_registers[address] : 0U;
}

uint32_t TestP9_GetWriteCount(void)
{
    return s_write_count;
}

uint32_t TestP9_GetCalibrationHandoffCount(void)
{
    return s_handoff_count;
}

uint32_t TestP9_GetCalibrationInvalidationCount(void)
{
    return s_invalidation_count;
}

BQ76940_t *TestP9_GetDevice(void)
{
    return &s_device;
}

void vTaskSuspendAll(void)
{
}

BaseType_t xTaskResumeAll(void)
{
    return pdFALSE;
}

BaseType_t xQueueSemaphoreTake(QueueHandle_t semaphore,
                               TickType_t wait_ticks)
{
    (void)wait_ticks;
    return semaphore != NULL ? pdTRUE : pdFALSE;
}

BaseType_t xQueueGenericSend(QueueHandle_t semaphore,
                             const void *const item,
                             TickType_t wait_ticks,
                             BaseType_t copy_position)
{
    (void)item;
    (void)wait_ticks;
    (void)copy_position;
    return semaphore != NULL ? pdTRUE : pdFALSE;
}

BaseType_t xQueueReceive(QueueHandle_t queue,
                         void *const item,
                         TickType_t wait_ticks)
{
    (void)queue;
    (void)item;
    (void)wait_ticks;
    return pdFAIL;
}

EventBits_t xEventGroupClearBits(EventGroupHandle_t event_group,
                                 const EventBits_t bits_to_clear)
{
    (void)event_group;
    (void)bits_to_clear;
    return 0U;
}

void App_Rtos_NotifyStateUrgent(void)
{
}

void App_Rtos_RequestProtectService(void)
{
}

bool BMS_Data_GetIdentity(BMS_DataIdentity_t *identity)
{
    if (identity == NULL)
    {
        return false;
    }
    *identity = s_identity;
    return true;
}

bool BMS_Data_GetSnapshot(BMS_DataSnapshot_t *snapshot,
                          BMS_TimestampMs_t now_ms)
{
    (void)now_ms;
    if (snapshot == NULL)
    {
        return false;
    }
    *snapshot = s_measurement;
    snapshot->sample_sequence = s_identity.sample_sequence;
    snapshot->afe_generation = s_identity.afe_generation;
    return true;
}

bool BMS_Data_PublishSocDiagnostic(BMS_CapacityMah_t capacity_mah,
                                   BMS_SocPermille_t soc_permille,
                                   BMS_TimestampMs_t now_ms,
                                   bool valid)
{
    s_measurement.remaining_capacity_mah = capacity_mah;
    s_measurement.soc_permille = valid ? soc_permille :
        BMS_SOC_UNKNOWN_PERMILLE;
    s_measurement.soc_metadata.valid = valid;
    s_measurement.soc_metadata.timestamp_ms = now_ms;
    return true;
}

bool BMS_Data_IsFresh(bool valid,
                      bool stale_latched,
                      BMS_DataAgeMs_t age_ms,
                      BMS_DataAgeMs_t max_age_ms)
{
    return valid && !stale_latched && (age_ms <= max_age_ms);
}

BMS_ProtectSafetySnapshot_t BMS_Protect_GetSafetySnapshot(void)
{
    return s_protect;
}

bool BMS_Protect_GetXreadyState(BMS_ProtectXreadyState_t *snapshot)
{
    if (snapshot == NULL)
    {
        return false;
    }
    *snapshot = s_xready;
    return true;
}

bool BMS_Protect_SubmitServiceResetRequest(
    const BMS_ServiceResetRequest_t *request)
{
    return (request != NULL) && request->valid && !s_xready.active &&
        (request->evaluated_sample_sequence == s_identity.sample_sequence) &&
        (request->evaluated_afe_generation == s_identity.afe_generation);
}

bool BMS_Protect_AuthorizeXreadyClear(uint32_t generation,
                                     uint32_t revision)
{
    return s_xready.active &&
        (generation == s_xready.xready_generation) &&
        (revision != 0UL);
}

bool BMS_Protect_GetXreadyClearAck(BMS_ProtectXreadyClearAck_t *ack)
{
    if (ack == NULL)
    {
        return false;
    }
    *ack = s_xready_ack;
    return true;
}

bool BMS_Protect_ReleaseXreadyActionLatch(uint32_t generation,
                                         uint32_t revision)
{
    return !s_xready.active &&
        (generation == s_xready.xready_generation) &&
        (revision == s_xready_ack.recovery_revision) &&
        s_xready_ack.accepted;
}

void BMS_Sample_InvalidateCalibrationForXready(uint32_t generation)
{
    (void)generation;
    ++s_invalidation_count;
}

bool BMS_Sample_SetRecoveryCalibration(
    const BMS_SampleCalibrationEvidence_t *evidence,
    uint32_t current_recovery_revision,
    bool handoff_permitted)
{
    if ((evidence == NULL) || !handoff_permitted ||
        !evidence->post_clear_verified || !evidence->calibration.valid ||
        (evidence->xready_generation != s_xready.xready_generation) ||
        (evidence->recovery_revision != current_recovery_revision) ||
        s_xready.active)
    {
        return false;
    }
    ++s_handoff_count;
    return true;
}

bool BQ76940_IsInitialized(const BQ76940_t *device)
{
    return (device == &s_device) && device->initialized;
}

BQ76940_Status_t BQ76940_ConvertCcRawToCurrentMa(
    int16_t cc_raw,
    uint32_t rsense_uohm,
    int8_t polarity,
    int32_t *current_ma)
{
    int64_t value;

    if ((current_ma == NULL) || (rsense_uohm == 0UL) ||
        ((polarity != 1) && (polarity != -1)))
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    value = ((int64_t)cc_raw * 8440LL * (int64_t)polarity) /
        (int64_t)rsense_uohm;
    *current_ma = (int32_t)value;
    return BQ76940_STATUS_OK;
}

BQ76940_Status_t BQ76940_ReadByte(BQ76940_t *device,
                                  uint8_t address,
                                  uint8_t *value)
{
    BQ76940_Status_t status;

    if ((device != &s_device) || (value == NULL) ||
        (address >= sizeof(s_registers)))
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    status = s_next_read_status;
    s_next_read_status = BQ76940_STATUS_OK;
    if (status != BQ76940_STATUS_OK)
    {
        return status;
    }
    *value = s_registers[address];
    if (s_readback_corruption && (address == BQ76940_REG_SYS_CTRL2))
    {
        *value ^= 0x01U;
        s_readback_corruption = false;
    }
    return BQ76940_STATUS_OK;
}

BQ76940_Status_t BQ76940_WriteByte(BQ76940_t *device,
                                   uint8_t address,
                                   uint8_t value)
{
    BQ76940_Status_t status;

    if ((device != &s_device) || (address >= sizeof(s_registers)))
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    status = s_next_write_status;
    s_next_write_status = BQ76940_STATUS_OK;
    ++s_write_count;
    if (status != BQ76940_STATUS_OK)
    {
        return status;
    }
    s_registers[address] = value;
    return BQ76940_STATUS_OK;
}

BQ76940_Status_t BQ76940_DecodeCalibration(
    uint8_t adc_gain1,
    uint8_t adc_offset,
    uint8_t adc_gain2,
    BQ76940_Calibration_t *calibration)
{
    uint16_t gain_trim;

    if (calibration == NULL)
    {
        return BQ76940_STATUS_INVALID_ARGUMENT;
    }
    gain_trim = (uint16_t)(((adc_gain1 & BQ76940_ADCGAIN1_MASK) << 1U) |
                           ((adc_gain2 & BQ76940_ADCGAIN2_MASK) >> 5U));
    calibration->gain_uv_per_lsb =
        (uint16_t)(BQ76940_ADC_GAIN_BASE_UV_PER_LSB + gain_trim);
    calibration->offset_mv = (int8_t)adc_offset;
    calibration->valid = true;
    return BQ76940_STATUS_OK;
}
