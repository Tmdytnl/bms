#ifndef SOFT_I2C_H
#define SOFT_I2C_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    SOFT_I2C_STATUS_OK = 0,
    SOFT_I2C_STATUS_INVALID_ARGUMENT,
    SOFT_I2C_STATUS_NOT_INITIALIZED,
    SOFT_I2C_STATUS_STATE_ERROR,
    SOFT_I2C_STATUS_SCL_STUCK_LOW,
    SOFT_I2C_STATUS_SDA_STUCK_LOW,
    SOFT_I2C_STATUS_TIMEOUT,
    SOFT_I2C_STATUS_NACK_ADDRESS,
    SOFT_I2C_STATUS_NACK_DATA,
    SOFT_I2C_STATUS_RECOVERY_FAILED
} SoftI2C_Status_t;

typedef enum
{
    SOFT_I2C_MASTER_ACK = 0,
    SOFT_I2C_MASTER_NACK = 1
} SoftI2C_MasterResponse_t;

typedef void (*SoftI2C_LineActionFn)(void);
typedef bool (*SoftI2C_LineReadFn)(void);
typedef uint16_t (*SoftI2C_TimeUs16Fn)(void);
typedef bool (*SoftI2C_DelayUsFn)(uint32_t delay_us);

typedef struct
{
    SoftI2C_LineActionFn scl_drive_low;
    SoftI2C_LineActionFn scl_release;
    SoftI2C_LineReadFn scl_read;
    SoftI2C_LineActionFn sda_drive_low;
    SoftI2C_LineActionFn sda_release;
    SoftI2C_LineReadFn sda_read;
    SoftI2C_TimeUs16Fn time_us16;
    SoftI2C_DelayUsFn delay_us;
} SoftI2C_LineOps_t;

typedef struct
{
    uint16_t half_cycle_us;
    uint16_t scl_high_timeout_us;
    uint16_t bus_free_timeout_us;
} SoftI2C_Config_t;

typedef struct
{
    SoftI2C_LineOps_t ops;
    SoftI2C_Config_t config;
    bool initialized;
    bool started;
    bool read_response_pending;
} SoftI2C_t;

SoftI2C_Status_t SoftI2C_Init(SoftI2C_t *bus,
                              const SoftI2C_LineOps_t *ops,
                              const SoftI2C_Config_t *config);
bool SoftI2C_IsInitialized(const SoftI2C_t *bus);
SoftI2C_Status_t SoftI2C_WaitBusIdle(SoftI2C_t *bus);
SoftI2C_Status_t SoftI2C_Start(SoftI2C_t *bus);
SoftI2C_Status_t SoftI2C_RepeatedStart(SoftI2C_t *bus);
SoftI2C_Status_t SoftI2C_Stop(SoftI2C_t *bus);
SoftI2C_Status_t SoftI2C_WriteAddress(SoftI2C_t *bus, uint8_t address_byte);
SoftI2C_Status_t SoftI2C_WriteByte(SoftI2C_t *bus, uint8_t value);
SoftI2C_Status_t SoftI2C_ReadByteBegin(SoftI2C_t *bus, uint8_t *value);
SoftI2C_Status_t SoftI2C_SendReadResponse(SoftI2C_t *bus,
                                          SoftI2C_MasterResponse_t response);
SoftI2C_Status_t SoftI2C_ReadByte(SoftI2C_t *bus,
                                  uint8_t *value,
                                  SoftI2C_MasterResponse_t response);
SoftI2C_Status_t SoftI2C_RecoverBus(SoftI2C_t *bus);

#endif /* SOFT_I2C_H */
