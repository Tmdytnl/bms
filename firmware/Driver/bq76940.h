#ifndef BQ76940_H
#define BQ76940_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "soft_i2c.h"

#define BQ76940_MAX_BLOCK_LENGTH          (32U)

typedef enum
{
    BQ76940_STATUS_OK = 0,              /* 事务与最终 STOP 均已确认 */
    BQ76940_STATUS_INVALID_ARGUMENT,    /* 指针、长度或参数契约不成立 */
    BQ76940_STATUS_NOT_INITIALIZED,     /* device/bus 尚未完成绑定 */
    BQ76940_STATUS_I2C_TIMEOUT,         /* 时钟拉伸或总线空闲等待超时 */
    BQ76940_STATUS_I2C_NACK,            /* 地址、数据或寄存器阶段被拒绝 */
    BQ76940_STATUS_I2C_ERROR,           /* 其他协议状态/恢复错误 */
    BQ76940_STATUS_CRC_MISMATCH,        /* 读回数据未通过主机 CRC 校验 */
    BQ76940_STATUS_CRC_REJECTED,        /* 写入 CRC 被 AFE NACK */
    BQ76940_STATUS_CALIBRATION_INVALID, /* gain/offset 证据不可用于换算 */
    BQ76940_STATUS_RANGE_ERROR,         /* 长度、地址窗口或物理量越界 */
    /*
     * 追加在 enum 末尾以保持既有 ordinal。payload/CRC byte 全部 ACK，但最终
     * STOP generation/verification 失败，register side effect 的 commit point
     * 不明确；调用者既不能报告成功，也不能 blind replay W1C 等非幂等 write。
     */
    BQ76940_STATUS_WRITE_FINALIZATION_AMBIGUOUS
} BQ76940_Status_t;

typedef struct
{
    SoftI2C_t *bus;    /* caller-owned transport，完整 transaction 期间保持有效 */
    bool initialized;  /* 仅表示 handle/config 已绑定，不表示 AFE configuration ready */
} BQ76940_t;

typedef struct
{
    uint16_t gain_uv_per_lsb; /* ADCGAIN1/2 拼接后加基准值，单位 uV/LSB */
    int16_t offset_mv;        /* ADCOFFSET 的有符号二补数值，单位 mV */
    bool valid;               /* 两项均完成解码并通过数据手册范围检查 */
} BQ76940_Calibration_t;

/* Init 只绑定 transport，不写 AFE；配置完成身份由上层 startup 状态机持有。 */
BQ76940_Status_t BQ76940_Init(BQ76940_t *device, SoftI2C_t *bus);
bool BQ76940_IsInitialized(const BQ76940_t *device);

/*
 * 所有 block API 独占一条完整 I2C transaction；锁由调用者在 API 外持有。
 * 读操作先 staging，任一 CRC/STOP 失败都保持调用者输出不变。写操作若最终
 * STOP 失败返回 AMBIGUOUS：寄存器可能已经改变，调用者不得盲目重放 W1C。
 */
BQ76940_Status_t BQ76940_WriteByte(BQ76940_t *device,
                                    uint8_t register_address,
                                    uint8_t value);
BQ76940_Status_t BQ76940_WriteBlock(BQ76940_t *device,
                                     uint8_t start_register,
                                     const uint8_t *data,
                                     size_t length);
BQ76940_Status_t BQ76940_ReadByte(BQ76940_t *device,
                                   uint8_t register_address,
                                   uint8_t *value);
BQ76940_Status_t BQ76940_ReadBlock(BQ76940_t *device,
                                    uint8_t start_register,
                                    uint8_t *data,
                                    size_t length);
BQ76940_Status_t BQ76940_ReadAdjacentU16(BQ76940_t *device,
                                         uint8_t high_register,
                                         uint16_t *value);

/* DecodeCalibration 是纯函数；ReadCalibration 依次取三寄存器并只提交完整结果。 */
BQ76940_Status_t BQ76940_DecodeCalibration(uint8_t adc_gain1,
                                            uint8_t adc_offset,
                                            uint8_t adc_gain2,
                                            BQ76940_Calibration_t *calibration);
BQ76940_Status_t BQ76940_ReadCalibration(
    BQ76940_t *device,
    BQ76940_Calibration_t *calibration);

uint16_t BQ76940_DecodeRaw14(uint8_t high_byte, uint8_t low_byte);
int16_t BQ76940_DecodeSigned16(uint8_t high_byte, uint8_t low_byte);
/* cell 换算采用 GAIN×ADC+OFFSET 与 half-up mV 取整；失败不改写 cell_mv。 */
BQ76940_Status_t BQ76940_ConvertCellRawToMv(
    uint16_t raw14,
    const BQ76940_Calibration_t *calibration,
    uint16_t *cell_mv);

#endif /* BQ76940_H：头文件防重复包含 */
