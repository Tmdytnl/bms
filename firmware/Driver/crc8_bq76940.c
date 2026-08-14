#include "crc8_bq76940.h"

uint8_t BQ76940_CRC8_Update(uint8_t crc, uint8_t value)
{
    uint8_t bit;

    crc ^= value;
    for (bit = 0U; bit < 8U; ++bit)
    {
        if ((crc & 0x80U) != 0U)
        {
            crc = (uint8_t)((crc << 1) ^ BQ76940_CRC8_POLYNOMIAL);
        }
        else
        {
            crc <<= 1;
        }
    }
    return crc;
}

uint8_t BQ76940_CRC8_Calculate(const uint8_t *data, size_t length)
{
    uint8_t crc;
    size_t index;

    crc = BQ76940_CRC8_INITIAL;
    if (data == NULL)
    {
        return crc;
    }
    for (index = 0U; index < length; ++index)
    {
        crc = BQ76940_CRC8_Update(crc, data[index]);
    }
    return crc;
}

uint8_t BQ76940_CRC8_FirstWrite(uint8_t wire_write_address,
                                uint8_t register_address,
                                uint8_t data)
{
    uint8_t frame[3];

    frame[0] = wire_write_address;
    frame[1] = register_address;
    frame[2] = data;
    return BQ76940_CRC8_Calculate(frame, 3U);
}

uint8_t BQ76940_CRC8_NextByte(uint8_t data)
{
    return BQ76940_CRC8_Calculate(&data, 1U);
}

uint8_t BQ76940_CRC8_FirstRead(uint8_t wire_read_address, uint8_t data)
{
    uint8_t frame[2];

    frame[0] = wire_read_address;
    frame[1] = data;
    return BQ76940_CRC8_Calculate(frame, 2U);
}
