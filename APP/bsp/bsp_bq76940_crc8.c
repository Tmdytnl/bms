#include "bsp_bq76940_crc8.h"

/* 按 BQ 多项式把一个字节并入当前 CRC8。 */
uint8_t BSP_BQ76940_CRC8_Update(uint8_t crc, uint8_t value)
{
    /* 当前检查的单个位。 */
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

/* 对连续字节计算完整 CRC8，供读写事务校验。 */
uint8_t BSP_BQ76940_CRC8_Calculate(const uint8_t *data, size_t length)
{
    /* 当前 CRC 累积值。 */
    uint8_t crc;
    /* 当前参与 CRC 计算的数据字节索引。 */
    size_t index;

    crc = BQ76940_CRC8_INITIAL;
    if (data == NULL)
    {
        return crc;
    }
    for (index = 0U; index < length; ++index)
    {
        crc = BSP_BQ76940_CRC8_Update(crc, data[index]);
    }
    return crc;
}

/* 计算写事务首字节的 CRC8 累积值。 */
uint8_t BSP_BQ76940_CRC8_FirstWrite(uint8_t wire_write_address,
                                uint8_t register_address,
                                uint8_t data)
{
    /* 当前接收或发送的一帧报文。 */
    uint8_t frame[3];

    frame[0] = wire_write_address;
    frame[1] = register_address;
    frame[2] = data;
    return BSP_BQ76940_CRC8_Calculate(frame, 3U);
}

/* 把后续字节并入当前 CRC8 累积值。 */
uint8_t BSP_BQ76940_CRC8_NextByte(uint8_t data)
{
    return BSP_BQ76940_CRC8_Calculate(&data, 1U);
}

/* 计算读事务首字节的 CRC8 累积值。 */
uint8_t BSP_BQ76940_CRC8_FirstRead(uint8_t wire_read_address, uint8_t data)
{
    /* 当前接收或发送的一帧报文。 */
    uint8_t frame[2];

    frame[0] = wire_read_address;
    frame[1] = data;
    return BSP_BQ76940_CRC8_Calculate(frame, 2U);
}
