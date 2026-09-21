/*
 * Copyright (c) 2025 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "flash_gd32.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <gd32_fmc.h>

LOG_MODULE_DECLARE(flash_gd32);

#define GD32_NV_FLASH_M53X_NODE		DT_INST(0, gd_gd32_nv_flash_m53x)
#define GD32_NV_FLASH_M53X_TIMEOUT	DT_PROP(GD32_NV_FLASH_M53X_NODE, max_erase_time_ms)
#define GD32_NV_FLASH_M53X_PAGE_SIZE	DT_PROP(GD32_NV_FLASH_M53X_NODE, page_size)

/*
 * The main flash keeps a 9-bit ECC next to every 128-bit quad-word, so a
 * quad-word that is not fully programmed is silently discarded by the FMC.
 */
#define GD32_FMC_M53X_QUAD_SIZE		SOC_NV_FLASH_PRG_SIZE
#define GD32_FMC_M53X_QUAD_WORDS	(GD32_FMC_M53X_QUAD_SIZE / sizeof(uint32_t))

BUILD_ASSERT(GD32_FMC_M53X_QUAD_SIZE == 16U,
	     "GD32 M53x FMC requires write-block-size = <16>");

#define GD32_FMC_M53X_WRITE_ERR	(FMC_STAT_PGSERR | FMC_STAT_PGERR | \
				 FMC_STAT_PGAERR | FMC_STAT_WPERR)
#define GD32_FMC_M53X_ERASE_ERR	(FMC_STAT_PGSERR | FMC_STAT_WPERR)

#ifdef CONFIG_FLASH_PAGE_LAYOUT
static const struct flash_pages_layout gd32_fmc_m53x_layout[] = {
	{
		.pages_size = GD32_NV_FLASH_M53X_PAGE_SIZE,
		.pages_count = SOC_NV_FLASH_SIZE / GD32_NV_FLASH_M53X_PAGE_SIZE
	}
};
#endif

static inline void gd32_fmc_m53x_unlock(void)
{
	FMC_KEY = UNLOCK_KEY0;
	FMC_KEY = UNLOCK_KEY1;
}

static inline void gd32_fmc_m53x_lock(void)
{
	FMC_CTL |= FMC_CTL_LK;
}

static int gd32_fmc_m53x_wait_idle(void)
{
	const int64_t expired_time = k_uptime_get() + GD32_NV_FLASH_M53X_TIMEOUT;

	while (FMC_STAT & FMC_STAT_BUSY) {
		if (k_uptime_get() > expired_time) {
			return -ETIMEDOUT;
		}
	}

	return 0;
}

/* Flash changed behind the IBUS/DBUS caches, drop the now stale lines. */
static void gd32_fmc_m53x_cache_reset(void)
{
	const uint32_t ws = FMC_WS;

	FMC_WS = ws & ~(FMC_WS_ICEN | FMC_WS_DCEN);
	FMC_WS |= (FMC_WS_ICRST | FMC_WS_DCRST);
	FMC_WS &= ~(FMC_WS_ICRST | FMC_WS_DCRST);
	FMC_WS = ws;
}

bool flash_gd32_valid_range(off_t offset, uint32_t len, bool write)
{
	if ((offset < 0) || (offset > SOC_NV_FLASH_SIZE) ||
	    ((offset + len) > SOC_NV_FLASH_SIZE)) {
		return false;
	}

	if (write) {
		/* Check offset and len are quad-word aligned. */
		if ((offset % GD32_FMC_M53X_QUAD_SIZE) ||
		    (len % GD32_FMC_M53X_QUAD_SIZE)) {
			return false;
		}
	} else {
		if ((offset % GD32_NV_FLASH_M53X_PAGE_SIZE) ||
		    (len % GD32_NV_FLASH_M53X_PAGE_SIZE)) {
			return false;
		}
	}

	return true;
}

static int gd32_fmc_m53x_quad_program(uint32_t addr, const uint8_t *data)
{
	volatile uint32_t *dst = (volatile uint32_t *)addr;
	int ret;

	for (size_t i = 0U; i < GD32_FMC_M53X_QUAD_WORDS; i++) {
		uint32_t word;

		/* The caller buffer carries no alignment guarantee. */
		memcpy(&word, &data[i * sizeof(uint32_t)], sizeof(word));
		dst[i] = word;
	}

	__DSB();

	ret = gd32_fmc_m53x_wait_idle();
	if (ret < 0) {
		return ret;
	}

	if (FMC_STAT & GD32_FMC_M53X_WRITE_ERR) {
		FMC_STAT |= GD32_FMC_M53X_WRITE_ERR;
		LOG_ERR("FMC quad-word program at 0x%08x failed", addr);
		return -EIO;
	}

	FMC_STAT |= FMC_STAT_ENDF;

	return 0;
}

int flash_gd32_write_range(off_t offset, const void *data, size_t len)
{
	uint32_t addr = SOC_NV_FLASH_ADDR + offset;
	const uint8_t *src = data;
	int ret;

	gd32_fmc_m53x_unlock();

	ret = gd32_fmc_m53x_wait_idle();
	if (ret < 0) {
		goto lock_out;
	}

	FMC_CTL |= FMC_CTL_PG;

	while (len > 0U) {
		ret = gd32_fmc_m53x_quad_program(addr, src);
		if (ret < 0) {
			break;
		}

		addr += GD32_FMC_M53X_QUAD_SIZE;
		src += GD32_FMC_M53X_QUAD_SIZE;
		len -= GD32_FMC_M53X_QUAD_SIZE;
	}

	FMC_CTL &= ~FMC_CTL_PG;

lock_out:
	gd32_fmc_m53x_lock();
	gd32_fmc_m53x_cache_reset();

	return ret;
}

static int gd32_fmc_m53x_page_erase(uint32_t page_addr)
{
	int ret;

	gd32_fmc_m53x_unlock();

	ret = gd32_fmc_m53x_wait_idle();
	if (ret < 0) {
		goto lock_out;
	}

	/* Route the erase to the main flash bank instead of the data flash. */
	FMC_CTL &= ~FMC_CTL_BKSEL;

	FMC_CTL |= FMC_CTL_PER;
	FMC_ADDR = page_addr;
	FMC_CTL |= FMC_CTL_START;

	ret = gd32_fmc_m53x_wait_idle();
	if (ret < 0) {
		goto clear_out;
	}

	if (FMC_STAT & GD32_FMC_M53X_ERASE_ERR) {
		ret = -EIO;
		FMC_STAT |= GD32_FMC_M53X_ERASE_ERR;
		LOG_ERR("FMC page 0x%08x erase failed", page_addr);
	} else {
		FMC_STAT |= FMC_STAT_ENDF;
	}

clear_out:
	FMC_CTL &= ~FMC_CTL_PER;

lock_out:
	gd32_fmc_m53x_lock();

	return ret;
}

int flash_gd32_erase_block(off_t offset, size_t size)
{
	uint32_t page_addr = SOC_NV_FLASH_ADDR + offset;
	int ret = 0;

	while (size > 0U) {
		ret = gd32_fmc_m53x_page_erase(page_addr);
		if (ret < 0) {
			break;
		}

		size -= GD32_NV_FLASH_M53X_PAGE_SIZE;
		page_addr += GD32_NV_FLASH_M53X_PAGE_SIZE;
	}

	gd32_fmc_m53x_cache_reset();

	return ret;
}

#ifdef CONFIG_FLASH_PAGE_LAYOUT
void flash_gd32_pages_layout(const struct device *dev,
			     const struct flash_pages_layout **layout,
			     size_t *layout_size)
{
	ARG_UNUSED(dev);

	*layout = gd32_fmc_m53x_layout;
	*layout_size = ARRAY_SIZE(gd32_fmc_m53x_layout);
}
#endif /* CONFIG_FLASH_PAGE_LAYOUT */
