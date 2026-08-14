#ifndef BMS_MEMORY_MAP_H
#define BMS_MEMORY_MAP_H

#include <stdint.h>

#include "bms_build_assert.h"

/* STM32F103C8 official 64 KiB Flash boundary. */
#define BMS_MEMORY_MAP_VERSION                   (1U)
#define BMS_FLASH_BASE                           (0x08000000UL)
#define BMS_FLASH_SIZE                           (0x00010000UL)
#define BMS_FLASH_END_EXCLUSIVE                  (0x08010000UL)
#define BMS_FLASH_PAGE_SIZE                      (0x00000400UL)

/* The Keil IROM1 application region must match this half-open interval. */
#define BMS_APP_FLASH_BASE                       BMS_FLASH_BASE
#define BMS_APP_FLASH_SIZE                       (0x0000F400UL)
#define BMS_APP_FLASH_END_EXCLUSIVE              (0x0800F400UL)
#define BMS_APP_FLASH_LAST_ADDRESS               (0x0800F3FFUL)

/* Three reserved 1 KiB pages. No Phase 1 code writes these addresses. */
#define BMS_SOC_LOG_ADDR                         (0x0800F400UL)
#define BMS_SOC_LOG_END_EXCLUSIVE                (0x0800F800UL)
#define BMS_PARAM_A_ADDR                         (0x0800F800UL)
#define BMS_PARAM_A_END_EXCLUSIVE                (0x0800FC00UL)
#define BMS_PARAM_B_ADDR                         (0x0800FC00UL)
#define BMS_PARAM_B_END_EXCLUSIVE                BMS_FLASH_END_EXCLUSIVE

BMS_BUILD_ASSERT(BMS_MEMORY_MAP_VERSION == 1U,
                 memory_map_version_is_one);
BMS_BUILD_ASSERT(BMS_FLASH_SIZE == (64UL * 1024UL),
                 flash_size_is_sixty_four_kib);
BMS_BUILD_ASSERT(BMS_FLASH_PAGE_SIZE == 1024UL,
                 flash_page_is_one_kib);
BMS_BUILD_ASSERT(BMS_APP_FLASH_SIZE == (61UL * 1024UL),
                 application_region_is_sixty_one_kib);
BMS_BUILD_ASSERT((BMS_FLASH_BASE + BMS_FLASH_SIZE) ==
                     BMS_FLASH_END_EXCLUSIVE,
                 physical_flash_end_matches_size);
BMS_BUILD_ASSERT((BMS_APP_FLASH_BASE + BMS_APP_FLASH_SIZE) ==
                     BMS_APP_FLASH_END_EXCLUSIVE,
                 application_end_matches_size);
BMS_BUILD_ASSERT((BMS_APP_FLASH_LAST_ADDRESS + 1UL) ==
                     BMS_APP_FLASH_END_EXCLUSIVE,
                 application_last_address_matches_end);
BMS_BUILD_ASSERT(BMS_APP_FLASH_END_EXCLUSIVE == BMS_SOC_LOG_ADDR,
                 application_stops_at_soc_page);

BMS_BUILD_ASSERT(((BMS_SOC_LOG_ADDR - BMS_FLASH_BASE) %
                      BMS_FLASH_PAGE_SIZE) == 0UL,
                 soc_page_is_aligned);
BMS_BUILD_ASSERT(((BMS_PARAM_A_ADDR - BMS_FLASH_BASE) %
                      BMS_FLASH_PAGE_SIZE) == 0UL,
                 parameter_a_page_is_aligned);
BMS_BUILD_ASSERT(((BMS_PARAM_B_ADDR - BMS_FLASH_BASE) %
                      BMS_FLASH_PAGE_SIZE) == 0UL,
                 parameter_b_page_is_aligned);

BMS_BUILD_ASSERT((BMS_SOC_LOG_ADDR + BMS_FLASH_PAGE_SIZE) ==
                     BMS_PARAM_A_ADDR,
                 soc_and_parameter_a_do_not_overlap);
BMS_BUILD_ASSERT((BMS_PARAM_A_ADDR + BMS_FLASH_PAGE_SIZE) ==
                     BMS_PARAM_B_ADDR,
                 parameter_a_and_b_do_not_overlap);
BMS_BUILD_ASSERT((BMS_PARAM_B_ADDR + BMS_FLASH_PAGE_SIZE) ==
                     BMS_FLASH_END_EXCLUSIVE,
                 parameter_b_ends_at_physical_flash_end);
BMS_BUILD_ASSERT(BMS_SOC_LOG_ADDR >= BMS_FLASH_BASE,
                 soc_page_is_inside_flash);
BMS_BUILD_ASSERT(BMS_PARAM_B_END_EXCLUSIVE <= BMS_FLASH_END_EXCLUSIVE,
                 reserved_pages_are_inside_flash);

#endif /* BMS_MEMORY_MAP_H */
