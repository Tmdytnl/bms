#ifndef CRC8_BQ76940_H
#define CRC8_BQ76940_H

#include <stddef.h>
#include <stdint.h>

#define BQ76940_CRC8_POLYNOMIAL    (0x07U)
#define BQ76940_CRC8_INITIAL       (0x00U)

uint8_t BQ76940_CRC8_Update(uint8_t crc, uint8_t value);
uint8_t BQ76940_CRC8_Calculate(const uint8_t *data, size_t length);
uint8_t BQ76940_CRC8_FirstWrite(uint8_t wire_write_address,
                                uint8_t register_address,
                                uint8_t data);
uint8_t BQ76940_CRC8_NextByte(uint8_t data);
uint8_t BQ76940_CRC8_FirstRead(uint8_t wire_read_address, uint8_t data);

#endif /* CRC8_BQ76940_H：include guard */
