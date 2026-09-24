#include "bsp_flash.h"

/*
 * BSP 只提供受地址边界保护的 read、page erase、halfword program。每次修改都
 * 临时 unlock 并在返回前 lock；不在此层选择 slot，也不把写成功等同于 record
 * 有效，最终 CRC/readback/commit 判断属于 persistence owner。
 *
 * STM32F1 Flash 平时由控制器锁保护，修改前必须 unlock，结束后立即重新 lock，
 * 以缩短误写窗口。物理擦除最小单位是整页，作用是把所有 bit 恢复为 1；编程
 * 最小单位是对齐 halfword，只能把 1 写成 0。因此上层必须先擦 inactive page，
 * 再逐 halfword 编程，不能像 RAM 一样覆盖任意 byte。
 */

#include <stddef.h>

#include "bsp_board_config.h"
#include "stm32f10x_flash.h"

/* 检查读写地址完整落在保留的持久化页内。 */
static bool BSP_Flash_RangeInsidePersistence(uint32_t address,
                                             uint16_t length)
{
    uint32_t end;

    if ((length == 0U) || (address < BSP_BOARD_PERSISTENCE_A_ADDR))
    {
        return false;
    }
    /* end>=address 显式拒绝 32-bit 加法回绕，不能让大地址绕回合法窗口。 */
    end = address + (uint32_t)length;
    return (end >= address) &&
        (end <= BSP_BOARD_PERSISTENCE_END_EXCLUSIVE);
}

/* 从指定 Flash 地址复制字节，不改变持久化内容。 */
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

/* 仅擦除预留的持久化页，拒绝应用代码区地址。 */
bool BSP_Flash_ErasePersistencePage(uint32_t page_address)
{
    FLASH_Status status;
    const volatile uint16_t *verify;
    uint16_t index;

    if ((page_address != BSP_BOARD_PERSISTENCE_A_ADDR) &&
        (page_address != BSP_BOARD_PERSISTENCE_B_ADDR))
    {
        return false;
    }
    /* 精确页首白名单先于 unlock；擦除硬件按整页工作，不能接受页内任意地址。 */
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR |
                    FLASH_FLAG_WRPRTERR);
    status = FLASH_ErasePage(page_address);
    /* 操作完成立即重新上锁，即使后续 readback 失败也不扩大可写时间窗。 */
    FLASH_Lock();
    if (status != FLASH_COMPLETE)
    {
        return false;
    }
    /* SPL 返回完成仍需逐 halfword 回读，擦除不完整不能交给上层继续写 record。 */
    verify = (const volatile uint16_t *)page_address;
    for (index = 0U; index < (BSP_BOARD_FLASH_PAGE_SIZE / 2UL); ++index)
    {
        if (verify[index] != 0xFFFFU)
        {
            return false;
        }
    }
    return true;
}

/* 仅向预留页写入一个 halfword 并回报硬件结果。 */
bool BSP_Flash_ProgramPersistenceHalfWord(uint32_t address, uint16_t value)
{
    FLASH_Status status;

    /* 只允许 erased halfword 的首次 1→0 编程；禁止在旧内容上直接覆盖。 */
    if (((address & 1UL) != 0UL) ||
        !BSP_Flash_RangeInsidePersistence(address, 2U) ||
        (*(const volatile uint16_t *)address != 0xFFFFU))
    {
        return false;
    }
    /* STM32F1 的最小编程粒度是 16-bit halfword，故地址必须偶数且目标已擦除。 */
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR |
                    FLASH_FLAG_WRPRTERR);
    status = FLASH_ProgramHalfWord(address, value);
    /* 与 erase 相同，控制器返回后先恢复硬件锁，再向上层报告校验结果。 */
    FLASH_Lock();
    /* lock 后再比较实际 Flash，API 的 true 同时表示控制器成功与数据一致。 */
    return (status == FLASH_COMPLETE) &&
        (*(const volatile uint16_t *)address == value);
}
