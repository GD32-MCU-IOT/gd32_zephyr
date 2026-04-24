/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief GD32 cache management – nocache region detection
 */

#include "gd32_cache.h"

#include <zephyr/linker/linker-defs.h>
#include <zephyr/sys/math_extras.h>

#if defined(CONFIG_MEM_ATTR)
#include <zephyr/mem_mgmt/mem_attr.h>
#include <zephyr/dt-bindings/memory-attr/memory-attr-arm.h>
#endif

#if defined(CONFIG_NOCACHE_MEMORY)
/* Linker-defined boundaries of the .nocache section */
extern char _nocache_ram_start[];
extern char _nocache_ram_end[];
#endif

#if defined(GD32_CACHE_MANAGEMENT_ACTIVE)
bool gd32_buf_in_nocache(const void *buf, size_t len)
{
	uintptr_t addr = (uintptr_t)buf;
	uintptr_t buf_end;

	/*
	 * Compute the address of the last byte (inclusive).
	 * When len == 0, (len - 1) wraps to SIZE_MAX, which causes
	 * u32_add_overflow to detect overflow and return false safely.
	 */
	if (u32_add_overflow((uint32_t)addr, (uint32_t)(len - 1), (uint32_t *)&buf_end)) {
		return false;
	}

#if defined(CONFIG_NOCACHE_MEMORY)
	/* .nocache linker section (placed in DMA-accessible SRAM) */
	if (addr >= (uintptr_t)_nocache_ram_start && buf_end < (uintptr_t)_nocache_ram_end) {
		return true;
	}
#endif

#if defined(CONFIG_MEM_ATTR)
	/* Check if buffer is in a region marked NOCACHE in DT. */
	if (mem_attr_check_buf((void *)buf, len, DT_MEM_ARM_MPU_RAM_NOCACHE) == 0) {
		return true;
	}
#endif

	return false;
}
#endif /* GD32_CACHE_MANAGEMENT_ACTIVE */
