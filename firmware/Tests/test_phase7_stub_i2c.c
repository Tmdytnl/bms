#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "soft_i2c.h"

/*
 * Stub SoftI2C backend. Phase 7 tests exercise only the pure decision
 * logic (BMS_Protect_Decide); the transport entry points are linked to
 * satisfy bq76940.c but never executed in the simulator. This keeps the
 * test image valid while avoiding the simulator limitation that a
 * FreeRTOS + real-software-I2C transport combination cannot run here.
 */

bool SoftI2C_IsInitialized(const SoftI2C_t *bus)
{
    return (bus != NULL) && bus->initialized;
}

SoftI2C_Status_t SoftI2C_Start(SoftI2C_t *bus)
{
    (void)bus;
    return SOFT_I2C_STATUS_OK;
}

SoftI2C_Status_t SoftI2C_RepeatedStart(SoftI2C_t *bus)
{
    (void)bus;
    return SOFT_I2C_STATUS_OK;
}

SoftI2C_Status_t SoftI2C_Stop(SoftI2C_t *bus)
{
    (void)bus;
    return SOFT_I2C_STATUS_OK;
}

SoftI2C_Status_t SoftI2C_WriteAddress(SoftI2C_t *bus, uint8_t address_byte)
{
    (void)bus;
    (void)address_byte;
    return SOFT_I2C_STATUS_OK;
}

SoftI2C_Status_t SoftI2C_WriteByte(SoftI2C_t *bus, uint8_t value)
{
    (void)bus;
    (void)value;
    return SOFT_I2C_STATUS_OK;
}

SoftI2C_Status_t SoftI2C_ReadByteBegin(SoftI2C_t *bus, uint8_t *value)
{
    (void)bus;
    if (value != NULL)
    {
        *value = 0U;
    }
    return SOFT_I2C_STATUS_OK;
}

SoftI2C_Status_t SoftI2C_SendReadResponse(
    SoftI2C_t *bus,
    SoftI2C_MasterResponse_t response)
{
    (void)bus;
    (void)response;
    return SOFT_I2C_STATUS_OK;
}
