#ifndef BSP_BQ76940_CRC8_H
#define BSP_BQ76940_CRC8_H

#include <stddef.h>
#include <stdint.h>

#define BQ76940_CRC8_POLYNOMIAL    (0x07U)
#define BQ76940_CRC8_INITIAL       (0x00U)

/* 按 BQ 多项式把一个字节并入当前 CRC8。 */
uint8_t BSP_BQ76940_CRC8_Update(uint8_t crc, uint8_t value);
/* 对连续字节计算完整 CRC8，供读写事务校验。 */
uint8_t BSP_BQ76940_CRC8_Calculate(const uint8_t *data, size_t length);
/* 计算写事务首字节的 CRC8 累积值。 */
uint8_t BSP_BQ76940_CRC8_FirstWrite(uint8_t wire_write_address,
                                uint8_t register_address,
                                uint8_t data);
/* 把后续字节并入当前 CRC8 累积值。 */
uint8_t BSP_BQ76940_CRC8_NextByte(uint8_t data);
/* 计算读事务首字节的 CRC8 累积值。 */
uint8_t BSP_BQ76940_CRC8_FirstRead(uint8_t wire_read_address, uint8_t data);

#endif /* BSP_BQ76940_CRC8_H：include guard */
