#include "bsp_flash.h"

/*
 * BSP 只提供受地址边界保护的 read、page erase、halfword program。每次修改都
 * 临时 unlock 并在返回前 lock；不在此层选择 slot，也不把写成功等同于 record
 * 有效，最终 CRC/readback/commit 判断属于 persistence owner。
 */

#include <stddef.h>

#include "bms_memory_map.h"
#include "stm32f10x_flash.h"

static bool BSP_Flash_RangeInsidePersistence(uint32_t address,
                                             uint16_t length)
{
    uint32_t end;

    if ((length == 0U) || (address < BMS_PARAM_A_ADDR))
    {
        return false;
    }
    end = address + (uint32_t)length;
    return (end >= address) && (end <= BMS_PARAM_B_END_EXCLUSIVE);
}

bool BSP_Flash_Read(uint32_t address, uint8_t *destination, uint16_t length)
{
    const volatile uint8_t *source;
    uint16_t index;

    if ((destination == NULL) ||
        !BSP_Flash_RangeInsidePersistence(address, length))
    {
        return false;
    }
    source = (const volatile uint8_t *)address;
    for (index = 0U; index < length; ++index)
    {
        destination[index] = source[index];
    }
    return true;
}

bool BSP_Flash_ErasePersistencePage(uint32_t page_address)
{
    FLASH_Status status;
    const volatile uint16_t *verify;
    uint16_t index;

    if ((page_address != BMS_PARAM_A_ADDR) &&
        (page_address != BMS_PARAM_B_ADDR))
    {
        return false;
    }
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR |
                    FLASH_FLAG_WRPRTERR);
    status = FLASH_ErasePage(page_address);
    FLASH_Lock();
    if (status != FLASH_COMPLETE)
    {
        return false;
    }
    verify = (const volatile uint16_t *)page_address;
    for (index = 0U; index < (BMS_FLASH_PAGE_SIZE / 2UL); ++index)
    {
        if (verify[index] != 0xFFFFU)
        {
            return false;
        }
    }
    return true;
}

bool BSP_Flash_ProgramPersistenceHalfWord(uint32_t address, uint16_t value)
{
    FLASH_Status status;

    if (((address & 1UL) != 0UL) ||
        !BSP_Flash_RangeInsidePersistence(address, 2U) ||
        (*(const volatile uint16_t *)address != 0xFFFFU))
    {
        return false;
    }
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR |
                    FLASH_FLAG_WRPRTERR);
    status = FLASH_ProgramHalfWord(address, value);
    FLASH_Lock();
    return (status == FLASH_COMPLETE) &&
        (*(const volatile uint16_t *)address == value);
}
