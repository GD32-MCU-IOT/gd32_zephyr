/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_GD_GD32_COMMON_GD32_CACHE_H_
#define ZEPHYR_SOC_GD_GD32_COMMON_GD32_CACHE_H_

#include <zephyr/kernel.h>
#include <zephyr/cache.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_CACHE_MANAGEMENT) && defined(CONFIG_DCACHE)
#define GD32_CACHE_MANAGEMENT_ACTIVE 1
#endif

/**
 * @brief D-Cache line size in bytes
 *
 * Uses Zephyr's CONFIG_DCACHE_LINE_SIZE when defined (set by the
 * architecture or board); falls back to 32 (Cortex-M7 hardware default).
 */
#if defined(CONFIG_DCACHE_LINE_SIZE) && (CONFIG_DCACHE_LINE_SIZE > 0)
#define GD32_DCACHE_LINE_SIZE CONFIG_DCACHE_LINE_SIZE
#else
#define GD32_DCACHE_LINE_SIZE 32
#endif

/**
 * @brief Check if an address or size is cache-line aligned
 *
 * Delegates to Zephyr's IS_ALIGNED() from <zephyr/sys/util.h>.
 */
#define GD32_IS_CACHE_ALIGNED(addr) IS_ALIGNED((uintptr_t)(addr), GD32_DCACHE_LINE_SIZE)

/**
 * @brief Round a size up to the next cache-line boundary
 *
 * Delegates to Zephyr's ROUND_UP() from <zephyr/sys/util.h>.
 */
#define GD32_CACHE_ALIGN_UP(size) ROUND_UP((size), GD32_DCACHE_LINE_SIZE)

/**
 * @brief Attribute for internal DMA buffers
 *
 * Piggybacks on Zephyr's built-in __nocache (defined in
 * <zephyr/linker/section_tags.h>
 *
 */
#if defined(GD32_CACHE_MANAGEMENT_ACTIVE)
#define GD32_DMA_BUFFER __nocache __aligned(GD32_DCACHE_LINE_SIZE)
#else
#define GD32_DMA_BUFFER
#endif

/**
 * @brief Check if a buffer resides in non-cacheable memory
 *
 * Returns true if buf..buf+len-1 is entirely inside one of:
 *   - CONFIG_NOCACHE_MEMORY linker section (.nocache)
 *   - CONFIG_MEM_ATTR DT region marked DT_MEM_ARM_MPU_RAM_NOCACHE
 *
 * @param buf  Start address
 * @param len  Length in bytes (0 always returns false)
 * @return true if the whole range is non-cacheable
 */
#if defined(GD32_CACHE_MANAGEMENT_ACTIVE)
bool gd32_buf_in_nocache(const void *buf, size_t len);

#else /* !GD32_CACHE_MANAGEMENT_ACTIVE */

static inline bool gd32_buf_in_nocache(const void *buf, size_t len)
{
	ARG_UNUSED(buf);
	ARG_UNUSED(len);

	return true;
}

#endif /* GD32_CACHE_MANAGEMENT_ACTIVE */

/* ------------------------------------------------------------------ */
/* Cache maintenance wrappers                                          */
/* ------------------------------------------------------------------ */

#if defined(GD32_CACHE_MANAGEMENT_ACTIVE)

/**
 * @brief Flush (clean) cache for a TX buffer before DMA
 *
 * Ensures CPU-written data is visible to DMA controller.
 * No-op when the buffer is already in non-cacheable memory.
 *
 * @param buf  TX buffer start
 * @param len  Length in bytes
 */
static inline void gd32_cache_flush_buf(void *buf, size_t len)
{
	if (len == 0 || buf == NULL) {
		return;
	}
	if (!gd32_buf_in_nocache(buf, len)) {
		sys_cache_data_flush_range(buf, len);
	}
}

/**
 * @brief Safe cache invalidation for an RX buffer after DMA
 *
 * Makes DMA-written data visible to CPU.
 *
 * If both start and (start + len) are 32-byte aligned a pure invalidate
 * is performed.  Otherwise, flush-and-invalidate is used so that data in
 * the same cache line but *outside* the DMA region is not lost.
 *
 * No-op when the buffer is already in non-cacheable memory.
 *
 * @param buf  RX buffer start
 * @param len  Length in bytes
 */
static inline void gd32_cache_invalidate_buf(void *buf, size_t len)
{
	if (len == 0 || buf == NULL) {
		return;
	}
	if (!gd32_buf_in_nocache(buf, len)) {
		if (GD32_IS_CACHE_ALIGNED(buf) && GD32_IS_CACHE_ALIGNED((uint8_t *)buf + len)) {
			sys_cache_data_invd_range(buf, len);
		} else {
			/* Non-aligned: flush first to protect adjacent data */
			sys_cache_data_flush_and_invd_range(buf, len);
		}
	}
}

/**
 * @brief Flush and invalidate cache range
 *
 * No-op when the buffer is already in non-cacheable memory.
 */
static inline void gd32_cache_flush_and_invd_buf(void *buf, size_t len)
{
	if (len == 0 || buf == NULL) {
		return;
	}
	if (!gd32_buf_in_nocache(buf, len)) {
		sys_cache_data_flush_and_invd_range(buf, len);
	}
}

#else /* !GD32_CACHE_MANAGEMENT_ACTIVE */

static inline void gd32_cache_flush_buf(void *buf, size_t len)
{
	ARG_UNUSED(buf);
	ARG_UNUSED(len);
}

static inline void gd32_cache_invalidate_buf(void *buf, size_t len)
{
	ARG_UNUSED(buf);
	ARG_UNUSED(len);
}

static inline void gd32_cache_flush_and_invd_buf(void *buf, size_t len)
{
	ARG_UNUSED(buf);
	ARG_UNUSED(len);
}

#endif /* GD32_CACHE_MANAGEMENT_ACTIVE */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SOC_GD_GD32_COMMON_GD32_CACHE_H_ */
