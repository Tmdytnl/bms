#ifndef SOFT_I2C_H
#define SOFT_I2C_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    SOFT_I2C_STATUS_OK = 0,        /* 请求的线级动作和确认阶段均完成 */
    SOFT_I2C_STATUS_INVALID_ARGUMENT, /* 指针、回调或 timing 配置无效 */
    SOFT_I2C_STATUS_NOT_INITIALIZED,  /* 尚未绑定完整 line ops */
    SOFT_I2C_STATUS_STATE_ERROR,      /* START/读响应阶段调用顺序错误 */
    SOFT_I2C_STATUS_SCL_STUCK_LOW,    /* release 后超时仍低，含 clock stretch */
    SOFT_I2C_STATUS_SDA_STUCK_LOW,    /* 期望空闲/高电平时从机仍拉低 */
    SOFT_I2C_STATUS_TIMEOUT,          /* 延时源失败或其他有界等待超时 */
    SOFT_I2C_STATUS_NACK_ADDRESS,     /* 地址阶段未被任何从机应答 */
    SOFT_I2C_STATUS_NACK_DATA,        /* 数据/CRC byte 被从机拒绝 */
    SOFT_I2C_STATUS_RECOVERY_FAILED   /* 9 脉冲 + STOP 后总线仍不空闲 */
} SoftI2C_Status_t;

typedef enum
{
    SOFT_I2C_MASTER_ACK = 0,  /* 主机拉低第 9 位，请求从机继续发送 */
    SOFT_I2C_MASTER_NACK = 1 /* 主机释放第 9 位，声明最后一个 byte */
} SoftI2C_MasterResponse_t;

typedef void (*SoftI2C_LineActionFn)(void);
typedef bool (*SoftI2C_LineReadFn)(void);
typedef uint16_t (*SoftI2C_TimeUs16Fn)(void);
typedef bool (*SoftI2C_DelayUsFn)(uint32_t delay_us);

typedef struct
{
    /* Open-drain 只允许 drive-low 或 release；release 后必须读线电平确认。 */
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
    uint16_t half_cycle_us;       /* bit-bang 半周期，决定 nominal bus rate */
    uint16_t scl_high_timeout_us; /* release SCL 后等待 clock stretching 上升 */
    uint16_t bus_free_timeout_us; /* START 前等待 SDA/SCL 都 high 的上限 */
} SoftI2C_Config_t;

typedef struct
{
    SoftI2C_LineOps_t ops;
    SoftI2C_Config_t config;
    bool initialized;           /* ops/config 已复制且启动时总线空闲 */
    bool started;               /* 本 master 持有 START..STOP transaction */
    bool read_response_pending; /* data 已采样，ACK/NACK 第 9 位尚未发送 */
} SoftI2C_t;

/* 驱动不内置锁；一个 owner 必须在完整 START..STOP 期间独占同一 bus。 */
SoftI2C_Status_t SoftI2C_Init(SoftI2C_t *bus,
                              const SoftI2C_LineOps_t *ops,
                              const SoftI2C_Config_t *config);
bool SoftI2C_IsInitialized(const SoftI2C_t *bus);
SoftI2C_Status_t SoftI2C_WaitBusIdle(SoftI2C_t *bus);
/* START：SCL high 时 SDA high→low；Repeated START 不先释放 bus ownership。 */
SoftI2C_Status_t SoftI2C_Start(SoftI2C_t *bus);
SoftI2C_Status_t SoftI2C_RepeatedStart(SoftI2C_t *bus);
/* STOP：SCL high 时 SDA low→high；失败时 transaction finalization 不明确。 */
SoftI2C_Status_t SoftI2C_Stop(SoftI2C_t *bus);
SoftI2C_Status_t SoftI2C_WriteAddress(SoftI2C_t *bus, uint8_t address_byte);
SoftI2C_Status_t SoftI2C_WriteByte(SoftI2C_t *bus, uint8_t value);
SoftI2C_Status_t SoftI2C_ReadByteBegin(SoftI2C_t *bus, uint8_t *value);
/* master ACK 请求继续读，NACK 表示最后一个 byte；必须显式完成 response phase。 */
SoftI2C_Status_t SoftI2C_SendReadResponse(SoftI2C_t *bus,
                                          SoftI2C_MasterResponse_t response);
SoftI2C_Status_t SoftI2C_ReadByte(SoftI2C_t *bus,
                                  uint8_t *value,
                                  SoftI2C_MasterResponse_t response);
SoftI2C_Status_t SoftI2C_RecoverBus(SoftI2C_t *bus);

#endif /* SOFT_I2C_H：头文件防重复包含 */
