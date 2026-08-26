#ifndef BSP_FLASH_H
#define BSP_FLASH_H

#include <stdbool.h>
#include <stdint.h>

/*
 * erase/program 地址严格限制在两个 persistence reserved page；application image
 * 与 SOC-log page 永远不是合法写目标。API 内部负责 unlock→operation→lock，
 * 上层 persistence 负责 A/B transaction 与 commit-last 顺序。
 */
bool BSP_Flash_Read(uint32_t address, uint8_t *destination, uint16_t length);
bool BSP_Flash_ErasePersistencePage(uint32_t page_address);
bool BSP_Flash_ProgramPersistenceHalfWord(uint32_t address, uint16_t value);

#endif /* BSP_FLASH_H：include guard */
