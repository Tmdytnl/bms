#ifndef TEST_PHASE9_STUB_H
#define TEST_PHASE9_STUB_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_data.h"
#include "bms_protect.h"
#include "bms_sample.h"

void TestP9_StubReset(void);
void TestP9_SetIdentity(uint32_t sequence, uint32_t afe_generation);
void TestP9_SetMeasurement(const BMS_DataSnapshot_t *measurement);
void TestP9_SetProtectSnapshot(
    const BMS_ProtectSafetySnapshot_t *snapshot);
void TestP9_SetXready(uint32_t generation, bool active);
void TestP9_SetXreadyAck(uint32_t generation,
                         uint32_t recovery_revision,
                         bool accepted,
                         bool ambiguous);
void TestP9_SetNextWriteStatus(BQ76940_Status_t status);
void TestP9_SetNextReadStatus(BQ76940_Status_t status);
void TestP9_SetReadbackCorruption(bool enabled);
uint8_t TestP9_GetRegister(uint8_t address);
uint32_t TestP9_GetWriteCount(void);
uint32_t TestP9_GetCalibrationHandoffCount(void);
uint32_t TestP9_GetCalibrationInvalidationCount(void);
BQ76940_t *TestP9_GetDevice(void);

#endif /* TEST_PHASE9_STUB_H */
