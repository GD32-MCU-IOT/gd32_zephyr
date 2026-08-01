/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for GD32 OSPI flash extended operations.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_FLASH_GD32_OSPI_FLASH_API_EXTENSIONS_H_
#define ZEPHYR_INCLUDE_DRIVERS_FLASH_GD32_OSPI_FLASH_API_EXTENSIONS_H_

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Explicitly switch a GD32 OSPI NOR flash device into memory-mapped
 * (XIP) mode.
 *
 * By default flash_read() always performs an indirect-mode command
 * sequence. Memory-mapped mode is never entered automatically, so callers
 * that want flash_read() (or direct pointer access through the controller's
 * memory-mapped address window) to be serviced via the memory-mapped path
 * must call this function first.
 *
 * Memory-mapped mode is automatically disabled again by the next
 * flash_write() or flash_erase() call on the same device.
 *
 * @param dev Flash device.
 *
 * @return 0 on success, negative errno code on failure.
 */
int flash_gd32_ospi_memory_mapped_enable(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_FLASH_GD32_OSPI_FLASH_API_EXTENSIONS_H_ */
