#ifndef BSP_FLASH_H
#define BSP_FLASH_H

#include <stdbool.h>
#include <stdint.h>

/* Access is deliberately restricted to the two reserved persistence pages.
 * The application image and SOC-log page are never valid write targets. */
bool BSP_Flash_Read(uint32_t address, uint8_t *destination, uint16_t length);
bool BSP_Flash_ErasePersistencePage(uint32_t page_address);
bool BSP_Flash_ProgramPersistenceHalfWord(uint32_t address, uint16_t value);

#endif /* BSP_FLASH_H */
