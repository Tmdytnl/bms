#include "soft_i2c.h"

#include <stddef.h>

#define SOFT_I2C_TIMEOUT_MAX_US     (32767U)
#define SOFT_I2C_RECOVERY_PULSES    (9U)

static bool SoftI2C_OpsAreValid(const SoftI2C_LineOps_t *ops)
{
    return (ops != NULL) &&
           (ops->scl_drive_low != NULL) &&
           (ops->scl_release != NULL) &&
           (ops->scl_read != NULL) &&
           (ops->sda_drive_low != NULL) &&
           (ops->sda_release != NULL) &&
           (ops->sda_read != NULL) &&
           (ops->time_us16 != NULL) &&
           (ops->delay_us != NULL);
}

static bool SoftI2C_ConfigIsValid(const SoftI2C_Config_t *config)
{
    return (config != NULL) &&
           (config->half_cycle_us != 0U) &&
           (config->scl_high_timeout_us != 0U) &&
           (config->scl_high_timeout_us <= SOFT_I2C_TIMEOUT_MAX_US) &&
           (config->bus_free_timeout_us != 0U) &&
           (config->bus_free_timeout_us <= SOFT_I2C_TIMEOUT_MAX_US);
}

static SoftI2C_Status_t SoftI2C_RequireReady(const SoftI2C_t *bus)
{
    if (bus == NULL)
    {
        return SOFT_I2C_STATUS_INVALID_ARGUMENT;
    }
    if (!bus->initialized)
    {
        return SOFT_I2C_STATUS_NOT_INITIALIZED;
    }
    return SOFT_I2C_STATUS_OK;
}

static SoftI2C_Status_t SoftI2C_WaitSclHigh(SoftI2C_t *bus)
{
    uint16_t start;
    uint32_t guard;

    bus->ops.scl_release();
    start = bus->ops.time_us16();
    guard = (uint32_t)bus->config.scl_high_timeout_us + 1UL;
    while (guard != 0UL)
    {
        if (bus->ops.scl_read())
        {
            return SOFT_I2C_STATUS_OK;
        }
        if ((uint16_t)(bus->ops.time_us16() - start) >=
            bus->config.scl_high_timeout_us)
        {
            break;
        }
        if (!bus->ops.delay_us(1UL))
        {
            return SOFT_I2C_STATUS_TIMEOUT;
        }
        --guard;
    }
    return SOFT_I2C_STATUS_SCL_STUCK_LOW;
}

static SoftI2C_Status_t SoftI2C_Delay(const SoftI2C_t *bus,
                                      uint32_t delay_us)
{
    return bus->ops.delay_us(delay_us) ? SOFT_I2C_STATUS_OK :
                                        SOFT_I2C_STATUS_TIMEOUT;
}

static SoftI2C_Status_t SoftI2C_HalfCycle(const SoftI2C_t *bus)
{
    return SoftI2C_Delay(bus, (uint32_t)bus->config.half_cycle_us);
}

#define SOFT_I2C_HALF_CYCLE_OR_RETURN(bus_, status_)                 \
    do                                                               \
    {                                                                \
        (status_) = SoftI2C_HalfCycle((bus_));                       \
        if ((status_) != SOFT_I2C_STATUS_OK)                         \
        {                                                            \
            return (status_);                                        \
        }                                                            \
    } while (0)

SoftI2C_Status_t SoftI2C_Init(SoftI2C_t *bus,
                              const SoftI2C_LineOps_t *ops,
                              const SoftI2C_Config_t *config)
{
    if ((bus == NULL) || !SoftI2C_OpsAreValid(ops) ||
        !SoftI2C_ConfigIsValid(config))
    {
        return SOFT_I2C_STATUS_INVALID_ARGUMENT;
    }

    bus->ops = *ops;
    bus->config = *config;
    bus->initialized = true;
    bus->started = false;
    bus->read_response_pending = false;
    bus->ops.sda_release();
    bus->ops.scl_release();
    return SoftI2C_WaitBusIdle(bus);
}

bool SoftI2C_IsInitialized(const SoftI2C_t *bus)
{
    return (bus != NULL) && bus->initialized;
}

SoftI2C_Status_t SoftI2C_WaitBusIdle(SoftI2C_t *bus)
{
    SoftI2C_Status_t status;
    uint16_t start;
    uint32_t guard;

    status = SoftI2C_RequireReady(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }
    if (bus->started || bus->read_response_pending)
    {
        return SOFT_I2C_STATUS_STATE_ERROR;
    }

    bus->ops.sda_release();
    bus->ops.scl_release();
    start = bus->ops.time_us16();
    guard = (uint32_t)bus->config.bus_free_timeout_us + 1UL;
    while (guard != 0UL)
    {
        if (bus->ops.scl_read() && bus->ops.sda_read())
        {
            return SOFT_I2C_STATUS_OK;
        }
        if ((uint16_t)(bus->ops.time_us16() - start) >=
            bus->config.bus_free_timeout_us)
        {
            break;
        }
        if (!bus->ops.delay_us(1UL))
        {
            return SOFT_I2C_STATUS_TIMEOUT;
        }
        --guard;
    }

    if (!bus->ops.scl_read())
    {
        return SOFT_I2C_STATUS_SCL_STUCK_LOW;
    }
    if (!bus->ops.sda_read())
    {
        return SOFT_I2C_STATUS_SDA_STUCK_LOW;
    }
    return SOFT_I2C_STATUS_TIMEOUT;
}

SoftI2C_Status_t SoftI2C_Start(SoftI2C_t *bus)
{
    SoftI2C_Status_t status;

    status = SoftI2C_WaitBusIdle(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }

    SOFT_I2C_HALF_CYCLE_OR_RETURN(bus, status);
    bus->ops.sda_drive_low();
    SOFT_I2C_HALF_CYCLE_OR_RETURN(bus, status);
    bus->ops.scl_drive_low();
    bus->started = true;
    return SOFT_I2C_STATUS_OK;
}

SoftI2C_Status_t SoftI2C_RepeatedStart(SoftI2C_t *bus)
{
    SoftI2C_Status_t status;

    status = SoftI2C_RequireReady(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }
    if (!bus->started || bus->read_response_pending)
    {
        return SOFT_I2C_STATUS_STATE_ERROR;
    }

    bus->ops.sda_release();
    SOFT_I2C_HALF_CYCLE_OR_RETURN(bus, status);
    status = SoftI2C_WaitSclHigh(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }
    if (!bus->ops.sda_read())
    {
        bus->ops.scl_drive_low();
        return SOFT_I2C_STATUS_SDA_STUCK_LOW;
    }
    SOFT_I2C_HALF_CYCLE_OR_RETURN(bus, status);
    bus->ops.sda_drive_low();
    SOFT_I2C_HALF_CYCLE_OR_RETURN(bus, status);
    bus->ops.scl_drive_low();
    return SOFT_I2C_STATUS_OK;
}

SoftI2C_Status_t SoftI2C_Stop(SoftI2C_t *bus)
{
    SoftI2C_Status_t status;

    status = SoftI2C_RequireReady(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }

    bus->ops.sda_drive_low();
    status = SoftI2C_HalfCycle(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        goto cleanup;
    }
    status = SoftI2C_WaitSclHigh(bus);
    if (status == SOFT_I2C_STATUS_OK)
    {
        status = SoftI2C_HalfCycle(bus);
        if (status != SOFT_I2C_STATUS_OK)
        {
            goto cleanup;
        }
        bus->ops.sda_release();
        status = SoftI2C_HalfCycle(bus);
        if (status != SOFT_I2C_STATUS_OK)
        {
            goto cleanup;
        }
        if (!bus->ops.sda_read())
        {
            status = SOFT_I2C_STATUS_SDA_STUCK_LOW;
        }
    }
    else
    {
        bus->ops.sda_release();
    }

cleanup:
    bus->ops.scl_release();
    bus->ops.sda_release();
    bus->started = false;
    bus->read_response_pending = false;
    return status;
}

SoftI2C_Status_t SoftI2C_WriteByte(SoftI2C_t *bus, uint8_t value)
{
    SoftI2C_Status_t status;
    uint8_t bit_index;
    bool acknowledged;

    status = SoftI2C_RequireReady(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }
    if (!bus->started || bus->read_response_pending)
    {
        return SOFT_I2C_STATUS_STATE_ERROR;
    }

    for (bit_index = 0U; bit_index < 8U; ++bit_index)
    {
        if ((value & 0x80U) != 0U)
        {
            bus->ops.sda_release();
        }
        else
        {
            bus->ops.sda_drive_low();
        }
        SOFT_I2C_HALF_CYCLE_OR_RETURN(bus, status);
        status = SoftI2C_WaitSclHigh(bus);
        if (status != SOFT_I2C_STATUS_OK)
        {
            return status;
        }
        SOFT_I2C_HALF_CYCLE_OR_RETURN(bus, status);
        bus->ops.scl_drive_low();
        value <<= 1;
    }

    bus->ops.sda_release();
    SOFT_I2C_HALF_CYCLE_OR_RETURN(bus, status);
    status = SoftI2C_WaitSclHigh(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }
    SOFT_I2C_HALF_CYCLE_OR_RETURN(bus, status);
    acknowledged = !bus->ops.sda_read();
    bus->ops.scl_drive_low();
    return acknowledged ? SOFT_I2C_STATUS_OK : SOFT_I2C_STATUS_NACK_DATA;
}

SoftI2C_Status_t SoftI2C_WriteAddress(SoftI2C_t *bus, uint8_t address_byte)
{
    SoftI2C_Status_t status;

    status = SoftI2C_WriteByte(bus, address_byte);
    if (status == SOFT_I2C_STATUS_NACK_DATA)
    {
        return SOFT_I2C_STATUS_NACK_ADDRESS;
    }
    return status;
}

SoftI2C_Status_t SoftI2C_ReadByteBegin(SoftI2C_t *bus, uint8_t *value)
{
    SoftI2C_Status_t status;
    uint8_t bit_index;
    uint8_t received;

    status = SoftI2C_RequireReady(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }
    if ((value == NULL) || !bus->started || bus->read_response_pending)
    {
        return (value == NULL) ? SOFT_I2C_STATUS_INVALID_ARGUMENT :
                                 SOFT_I2C_STATUS_STATE_ERROR;
    }

    received = 0U;
    bus->ops.sda_release();
    for (bit_index = 0U; bit_index < 8U; ++bit_index)
    {
        SOFT_I2C_HALF_CYCLE_OR_RETURN(bus, status);
        status = SoftI2C_WaitSclHigh(bus);
        if (status != SOFT_I2C_STATUS_OK)
        {
            return status;
        }
        received = (uint8_t)((received << 1) |
                             (bus->ops.sda_read() ? 1U : 0U));
        SOFT_I2C_HALF_CYCLE_OR_RETURN(bus, status);
        bus->ops.scl_drive_low();
    }

    *value = received;
    bus->read_response_pending = true;
    return SOFT_I2C_STATUS_OK;
}

SoftI2C_Status_t SoftI2C_SendReadResponse(SoftI2C_t *bus,
                                          SoftI2C_MasterResponse_t response)
{
    SoftI2C_Status_t status;

    status = SoftI2C_RequireReady(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }
    if (!bus->started || !bus->read_response_pending ||
        ((response != SOFT_I2C_MASTER_ACK) &&
         (response != SOFT_I2C_MASTER_NACK)))
    {
        return SOFT_I2C_STATUS_STATE_ERROR;
    }

    if (response == SOFT_I2C_MASTER_ACK)
    {
        bus->ops.sda_drive_low();
    }
    else
    {
        bus->ops.sda_release();
    }
    SOFT_I2C_HALF_CYCLE_OR_RETURN(bus, status);
    status = SoftI2C_WaitSclHigh(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }
    SOFT_I2C_HALF_CYCLE_OR_RETURN(bus, status);
    bus->ops.scl_drive_low();
    bus->ops.sda_release();
    bus->read_response_pending = false;
    return SOFT_I2C_STATUS_OK;
}

SoftI2C_Status_t SoftI2C_ReadByte(SoftI2C_t *bus,
                                  uint8_t *value,
                                  SoftI2C_MasterResponse_t response)
{
    SoftI2C_Status_t status;

    status = SoftI2C_ReadByteBegin(bus, value);
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }
    return SoftI2C_SendReadResponse(bus, response);
}

SoftI2C_Status_t SoftI2C_RecoverBus(SoftI2C_t *bus)
{
    SoftI2C_Status_t status;
    uint8_t pulse;

    status = SoftI2C_RequireReady(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }

    /*
     * Nine clocks plus STOP is a generic I2C bus-clear strategy only. It is
     * not a TI/BQ76940 device-level recovery guarantee; an unpowered AFE,
     * SHIP/POR state, pull-up/rise-time fault, or hard-stuck line still needs
     * board-level diagnosis and validation.
     */
    bus->started = false;
    bus->read_response_pending = false;
    bus->ops.sda_release();
    status = SoftI2C_WaitSclHigh(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        goto cleanup;
    }

    for (pulse = 0U; (pulse < SOFT_I2C_RECOVERY_PULSES) &&
                      !bus->ops.sda_read(); ++pulse)
    {
        bus->ops.scl_drive_low();
        status = SoftI2C_HalfCycle(bus);
        if (status != SOFT_I2C_STATUS_OK)
        {
            goto cleanup;
        }
        status = SoftI2C_WaitSclHigh(bus);
        if (status != SOFT_I2C_STATUS_OK)
        {
            goto cleanup;
        }
        status = SoftI2C_HalfCycle(bus);
        if (status != SOFT_I2C_STATUS_OK)
        {
            goto cleanup;
        }
    }

    bus->ops.sda_drive_low();
    status = SoftI2C_HalfCycle(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        goto cleanup;
    }
    status = SoftI2C_WaitSclHigh(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        goto cleanup;
    }
    status = SoftI2C_HalfCycle(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        goto cleanup;
    }
    bus->ops.sda_release();
    status = SoftI2C_HalfCycle(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        goto cleanup;
    }

    if (!bus->ops.scl_read() || !bus->ops.sda_read())
    {
        status = SOFT_I2C_STATUS_RECOVERY_FAILED;
        goto cleanup;
    }
    status = SOFT_I2C_STATUS_OK;

cleanup:
    bus->ops.scl_release();
    bus->ops.sda_release();
    bus->started = false;
    bus->read_response_pending = false;
    return status;
}
