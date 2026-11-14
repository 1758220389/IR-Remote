#ifndef FLASH_BACKEND_H
#define FLASH_BACKEND_H
#include "config_store.h"
#define FLASH_BACKEND_BANK0_ADDRESS 0x0800C000UL
#define FLASH_BACKEND_BANK1_ADDRESS 0x0800E000UL
#define FLASH_BACKEND_PAGE_SIZE 1024u
/* STM32F103 64KB Flash, 1KB pages. Link application to first 48KB.
 * Initializes callbacks only (no erase/unlock). context is NULL.
 * Each operation unlocks, clears flags, executes, verifies and re-locks.
 * Run foreground only; no concurrent Flash operations or source mutation.
 * Supply ample power; erase stalls execution/interrupt service on STM32F1.
 * This backend cannot compensate for a debugger mass erase on download. */
void FlashBackend_Init(ConfigStoreBackend *backend);
#endif
