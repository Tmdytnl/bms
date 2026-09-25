#include "bsp_soft_i2c.h"

/*
 * 软件 I2C 按 open-drain 时序实现 START/STOP、address/data ACK/NACK、read response
 * 与 clock-stretch timeout。驱动本身不持 RTOS mutex；完整 BQ transaction 的
 * 互斥边界由上层总线 owner 包围，避免逐 byte 加锁后发生 transaction 交叉。
 */

#include <stddef.h>

#define SOFT_I2C_TIMEOUT_MAX_US     (32767U)
#define SOFT_I2C_RECOVERY_PULSES    (9U)

/* 确认调用方提供完整的开漏读写与微秒时基回调。 */
static bool BSP_SoftI2C_OpsAreValid(const SoftI2C_LineOps_t *ops)
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

/* 确认半周期和超时配置落在软件 I2C 的合法范围。 */
static bool BSP_SoftI2C_ConfigIsValid(const SoftI2C_Config_t *config)
{
    return (config != NULL) &&
           (config->half_cycle_us != 0U) &&
           (config->scl_high_timeout_us != 0U) &&
           (config->scl_high_timeout_us <= SOFT_I2C_TIMEOUT_MAX_US) &&
           (config->bus_free_timeout_us != 0U) &&
           (config->bus_free_timeout_us <= SOFT_I2C_TIMEOUT_MAX_US);
}

/* 拒绝未完成初始化或缺失引脚回调的总线操作。 */
static SoftI2C_Status_t BSP_SoftI2C_RequireReady(const SoftI2C_t *bus)
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

/* 等待开漏 SCL 实际变高，超时表示时钟拉伸或总线故障。 */
static SoftI2C_Status_t BSP_SoftI2C_WaitSclHigh(SoftI2C_t *bus)
{
    /* 当前硬件延时或总线操作的起始计数。 */
    uint16_t start;
    /* 限制硬件等待或总线恢复循环的计数器。 */
    uint32_t guard;

    /* open-drain 的“写 1”是 release；随后实读 high 才证明上拉/从机允许上升。 */
    bus->ops.scl_release();
    start = bus->ops.time_us16();
    /* 时间差负责真实超时，guard 防止测试/故障 time source 永不前进时死循环。 */
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

/* 通过板级回调等待指定微秒数，并传播时基失败。 */
static SoftI2C_Status_t BSP_SoftI2C_Delay(const SoftI2C_t *bus,
                                      uint32_t delay_us)
{
    return bus->ops.delay_us(delay_us) ? SOFT_I2C_STATUS_OK :
                                        SOFT_I2C_STATUS_TIMEOUT;
}

/* 等待一次配置的 SCL 半周期，供位级事务复用。 */
static SoftI2C_Status_t BSP_SoftI2C_HalfCycle(const SoftI2C_t *bus)
{
    return BSP_SoftI2C_Delay(bus, (uint32_t)bus->config.half_cycle_us);
}

#define SOFT_I2C_HALF_CYCLE_OR_RETURN(bus_, status_)                 \
    do                                                               \
    {                                                                \
        (status_) = BSP_SoftI2C_HalfCycle((bus_));                       \
        if ((status_) != SOFT_I2C_STATUS_OK)                         \
        {                                                            \
            return (status_);                                        \
        }                                                            \
    } while (0)

/* 绑定开漏操作与时序参数；总线不满足初始条件时返回具体状态。 */
SoftI2C_Status_t BSP_SoftI2C_Init(SoftI2C_t *bus,
                              const SoftI2C_LineOps_t *ops,
                              const SoftI2C_Config_t *config)
{
    if ((bus == NULL) || !BSP_SoftI2C_OpsAreValid(ops) ||
        !BSP_SoftI2C_ConfigIsValid(config))
    {
        return SOFT_I2C_STATUS_INVALID_ARGUMENT;
    }

    bus->ops = *ops;
    bus->config = *config;
    bus->initialized = true;
    bus->started = false;
    bus->read_response_pending = false;
    /* 初始化结束前释放双线并确认 idle；“对象已填充”不等于物理总线可用。 */
    bus->ops.sda_release();
    bus->ops.scl_release();
    return BSP_SoftI2C_WaitBusIdle(bus);
}

/* 确认总线句柄已绑定引脚回调并完成初始化。 */
bool BSP_SoftI2C_IsInitialized(const SoftI2C_t *bus)
{
    return (bus != NULL) && bus->initialized;
}

/* 等待 SCL/SDA 同时释放，必要时尝试受限总线恢复。 */
SoftI2C_Status_t BSP_SoftI2C_WaitBusIdle(SoftI2C_t *bus)
{
    /* 当前总线阶段的 I²C 线级操作状态。 */
    SoftI2C_Status_t status;
    /* 当前硬件延时或总线操作的起始计数。 */
    uint16_t start;
    /* 限制硬件等待或总线恢复循环的计数器。 */
    uint32_t guard;

    status = BSP_SoftI2C_RequireReady(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }
    if (bus->started || bus->read_response_pending)
    {
        return SOFT_I2C_STATUS_STATE_ERROR;
    }

    /* START 前不得覆盖尚未完成的 transaction/read-response 状态。 */
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

/* 在总线空闲后产生 START 条件，保持事务独占前提。 */
SoftI2C_Status_t BSP_SoftI2C_Start(SoftI2C_t *bus)
{
    /* 当前总线阶段的 I²C 线级操作状态。 */
    SoftI2C_Status_t status;

    status = BSP_SoftI2C_WaitBusIdle(bus);
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

/* 在不释放总线的情况下产生重复 START 条件。 */
SoftI2C_Status_t BSP_SoftI2C_RepeatedStart(SoftI2C_t *bus)
{
    /* 当前总线阶段的 I²C 线级操作状态。 */
    SoftI2C_Status_t status;

    status = BSP_SoftI2C_RequireReady(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }
    if (!bus->started || bus->read_response_pending)
    {
        return SOFT_I2C_STATUS_STATE_ERROR;
    }

    /* 先在 SCL low 释放 SDA，再把 SCL 升高并验证 SDA high，最后制造 high→low。 */
    bus->ops.sda_release();
    SOFT_I2C_HALF_CYCLE_OR_RETURN(bus, status);
    status = BSP_SoftI2C_WaitSclHigh(bus);
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

/* 产生 STOP 条件并报告确认结果；不替上层猜测写提交点。 */
SoftI2C_Status_t BSP_SoftI2C_Stop(SoftI2C_t *bus)
{
    /* 当前总线阶段的 I²C 线级操作状态。 */
    SoftI2C_Status_t status;

    status = BSP_SoftI2C_RequireReady(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }

    bus->ops.sda_drive_low();
    status = BSP_SoftI2C_HalfCycle(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        goto cleanup;
    }
    status = BSP_SoftI2C_WaitSclHigh(bus);
    if (status == SOFT_I2C_STATUS_OK)
    {
        status = BSP_SoftI2C_HalfCycle(bus);
        if (status != SOFT_I2C_STATUS_OK)
        {
            goto cleanup;
        }
        bus->ops.sda_release();
        status = BSP_SoftI2C_HalfCycle(bus);
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
    /* 无论 STOP 哪一相失败，软件状态都退休；上层据返回值决定是否恢复总线。 */
    bus->ops.scl_release();
    bus->ops.sda_release();
    bus->started = false;
    bus->read_response_pending = false;
    return status;
}

/* 按 MSB 优先发送八位并采样从机 ACK。 */
SoftI2C_Status_t BSP_SoftI2C_WriteByte(SoftI2C_t *bus, uint8_t value)
{
    /* 当前总线阶段的 I²C 线级操作状态。 */
    SoftI2C_Status_t status;
    /* 当前寄存器字段中的位序号。 */
    uint8_t bit_index;
    /* 当前操作是否得到硬件应答。 */
    bool acknowledged;

    status = BSP_SoftI2C_RequireReady(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }
    if (!bus->started || bus->read_response_pending)
    {
        return SOFT_I2C_STATUS_STATE_ERROR;
    }

    /* MSB first；SDA 只在 SCL low 时改变，SCL high 期间保持稳定供从机采样。 */
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
        status = BSP_SoftI2C_WaitSclHigh(bus);
        if (status != SOFT_I2C_STATUS_OK)
        {
            return status;
        }
        SOFT_I2C_HALF_CYCLE_OR_RETURN(bus, status);
        bus->ops.scl_drive_low();
        value <<= 1;
    }

    /* 第 9 位必须释放 SDA，把应答线的所有权交给从机。 */
    bus->ops.sda_release();
    SOFT_I2C_HALF_CYCLE_OR_RETURN(bus, status);
    status = BSP_SoftI2C_WaitSclHigh(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }
    SOFT_I2C_HALF_CYCLE_OR_RETURN(bus, status);
    acknowledged = !bus->ops.sda_read();
    bus->ops.scl_drive_low();
    return acknowledged ? SOFT_I2C_STATUS_OK : SOFT_I2C_STATUS_NACK_DATA;
}

/* 按读写方向发送七位设备地址并确认 ACK。 */
SoftI2C_Status_t BSP_SoftI2C_WriteAddress(SoftI2C_t *bus, uint8_t address_byte)
{
    /* 当前总线阶段的 I²C 线级操作状态。 */
    SoftI2C_Status_t status;

    status = BSP_SoftI2C_WriteByte(bus, address_byte);
    if (status == SOFT_I2C_STATUS_NACK_DATA)
    {
        return SOFT_I2C_STATUS_NACK_ADDRESS;
    }
    return status;
}

/* 按 MSB 优先采样八位数据，暂不发送 ACK/NACK。 */
SoftI2C_Status_t BSP_SoftI2C_ReadByteBegin(SoftI2C_t *bus, uint8_t *value)
{
    /* 当前总线阶段的 I²C 线级操作状态。 */
    SoftI2C_Status_t status;
    /* 当前寄存器字段中的位序号。 */
    uint8_t bit_index;
    /* 本次线级操作是否接收到目标字节。 */
    uint8_t received;

    status = BSP_SoftI2C_RequireReady(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }
    if ((value == NULL) || !bus->started || bus->read_response_pending)
    {
        return (value == NULL) ? SOFT_I2C_STATUS_INVALID_ARGUMENT :
                                 SOFT_I2C_STATUS_STATE_ERROR;
    }

    /* 读数据时主机全程释放 SDA；每次 SCL high 采样一位并按 MSB first 拼接。 */
    received = 0U;
    bus->ops.sda_release();
    for (bit_index = 0U; bit_index < 8U; ++bit_index)
    {
        SOFT_I2C_HALF_CYCLE_OR_RETURN(bus, status);
        status = BSP_SoftI2C_WaitSclHigh(bus);
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
    /* 把第 9 位拆成显式阶段，供 BQ 层在检查 CRC 后决定 ACK 还是 NACK。 */
    bus->read_response_pending = true;
    return SOFT_I2C_STATUS_OK;
}

/* 发送读字节后的 ACK/NACK 应答并推进总线时序。 */
SoftI2C_Status_t BSP_SoftI2C_SendReadResponse(SoftI2C_t *bus,
                                          SoftI2C_MasterResponse_t response)
{
    /* 当前总线阶段的 I²C 线级操作状态。 */
    SoftI2C_Status_t status;

    status = BSP_SoftI2C_RequireReady(bus);
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

    /* 此时第 9 位由 master 驱动：ACK=拉低，NACK=释放。 */
    if (response == SOFT_I2C_MASTER_ACK)
    {
        bus->ops.sda_drive_low();
    }
    else
    {
        bus->ops.sda_release();
    }
    SOFT_I2C_HALF_CYCLE_OR_RETURN(bus, status);
    status = BSP_SoftI2C_WaitSclHigh(bus);
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

/* 完成字节采样及调用者指定的 ACK/NACK 应答。 */
SoftI2C_Status_t BSP_SoftI2C_ReadByte(SoftI2C_t *bus,
                                  uint8_t *value,
                                  SoftI2C_MasterResponse_t response)
{
    /* 当前总线阶段的 I²C 线级操作状态。 */
    SoftI2C_Status_t status;

    status = BSP_SoftI2C_ReadByteBegin(bus, value);
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }
    return BSP_SoftI2C_SendReadResponse(bus, response);
}

/* 对明确的 SDA stuck-low 总线执行九个时钟恢复与 STOP。 */
SoftI2C_Status_t BSP_SoftI2C_RecoverBus(SoftI2C_t *bus)
{
    /* 当前总线阶段的 I²C 线级操作状态。 */
    SoftI2C_Status_t status;
    /* 软件 I²C 总线恢复期间的 SCL 脉冲序号。 */
    uint8_t pulse;

    status = BSP_SoftI2C_RequireReady(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        return status;
    }

    /*
     * 9 个 SCL pulse + STOP 是通用 bus-clear：给可能停在输出 byte 的 slave 提供
     * 完成机会，再释放 transaction。它只恢复总线协议状态；AFE 无电、SHIP/POR、
     * pull-up/rise-time 异常或 hard-stuck line 不会被该算法伪装成成功。
     */
    bus->started = false;
    bus->read_response_pending = false;
    bus->ops.sda_release();
    status = BSP_SoftI2C_WaitSclHigh(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        goto cleanup;
    }

    for (pulse = 0U; (pulse < SOFT_I2C_RECOVERY_PULSES) &&
                      !bus->ops.sda_read(); ++pulse)
    {
        bus->ops.scl_drive_low();
        status = BSP_SoftI2C_HalfCycle(bus);
        if (status != SOFT_I2C_STATUS_OK)
        {
            goto cleanup;
        }
        status = BSP_SoftI2C_WaitSclHigh(bus);
        if (status != SOFT_I2C_STATUS_OK)
        {
            goto cleanup;
        }
        status = BSP_SoftI2C_HalfCycle(bus);
        if (status != SOFT_I2C_STATUS_OK)
        {
            goto cleanup;
        }
    }

    bus->ops.sda_drive_low();
    status = BSP_SoftI2C_HalfCycle(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        goto cleanup;
    }
    status = BSP_SoftI2C_WaitSclHigh(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        goto cleanup;
    }
    status = BSP_SoftI2C_HalfCycle(bus);
    if (status != SOFT_I2C_STATUS_OK)
    {
        goto cleanup;
    }
    bus->ops.sda_release();
    status = BSP_SoftI2C_HalfCycle(bus);
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
