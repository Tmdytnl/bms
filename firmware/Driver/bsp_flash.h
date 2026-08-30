#ifndef BSP_FLASH_H
#define BSP_FLASH_H

#include <stdbool.h>
#include <stdint.h>

/*
 * erase/program 地址严格限制在两个 persistence reserved page；application image
 * 与 SOC-log page 永远不是合法写目标。API 内部负责 unlock→operation→lock，
 * 上层 persistence 负责 A/B transaction 与 commit-last 顺序。
 */
/* 读也执行同一 reserved-range 检查，防止 persistence 被滥用为任意内存接口。 */
bool BSP_Flash_Read(uint32_t address, uint8_t *destination, uint16_t length);
/* 只接受 A/B 页首地址，成功表示整页回读均为 erased 0xFFFF。 */
bool BSP_Flash_ErasePersistencePage(uint32_t page_address);
/* 只接受偶数地址且目标仍为 0xFFFF；成功包含编程后 halfword readback。 */
bool BSP_Flash_ProgramPersistenceHalfWord(uint32_t address, uint16_t value);

#endif /* BSP_FLASH_H：头文件防重复包含 */
