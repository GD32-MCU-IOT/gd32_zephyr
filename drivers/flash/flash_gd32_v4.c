/*
 * Copyright (c) 2022 BrainCo Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "flash_gd32.h"

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <gd32_fmc.h>

LOG_MODULE_DECLARE(flash_gd32);

#define GD32_NV_FLASH_V4_NODE      DT_INST(0, gd_gd32_nv_flash_v4)
#define GD32_NV_FLASH_V4_TIMEOUT   DT_PROP(GD32_NV_FLASH_V4_NODE, max_erase_time_ms)
#define GD32_NV_FLASH_V4_PAGE_SIZE DT_PROP(GD32_NV_FLASH_V4_NODE, page_size)

/**
 * @brief GD32 FMC flash memory layout.
 */
#if defined(CONFIG_FLASH_PAGE_LAYOUT)

#if !((PRE_KB(1024) == SOC_NV_FLASH_SIZE) || (PRE_KB(2048) == SOC_NV_FLASH_SIZE) ||                \
	  (PRE_KB(3840) == SOC_NV_FLASH_SIZE))
#error "Unknown FMC layout for GD32 flash V4 series."
#endif

static const struct flash_pages_layout gd32_fmc_v4_layout[] = {
	{
	.pages_size = GD32_NV_FLASH_V4_PAGE_SIZE,
	.pages_count = SOC_NV_FLASH_SIZE / GD32_NV_FLASH_V4_PAGE_SIZE
	}
};

#endif /* CONFIG_FLASH_PAGE_LAYOUT */

/* GD32H7xx error flags */
#define GD32_FMC_V4_WRITE_ERR (FMC_STAT_WPERR | FMC_STAT_PGSERR)
#define GD32_FMC_V4_ERASE_ERR (FMC_STAT_WPERR | FMC_STAT_PGSERR)

static inline void gd32_fmc_v4_unlock(void)
{
	fmc_unlock();
}

static inline void gd32_fmc_v4_lock(void)
{
	fmc_lock();
}

static int gd32_fmc_v4_wait_idle(void)
{
	const int64_t expired_time = k_uptime_get() + GD32_NV_FLASH_V4_TIMEOUT;

	while (FMC_STAT & FMC_STAT_BUSY) {
		if (k_uptime_get() > expired_time) {
			return -ETIMEDOUT;
		}
	}

	return 0;
}

bool flash_gd32_valid_range(off_t offset, uint32_t len, bool write)
{
	if ((offset > SOC_NV_FLASH_SIZE) || ((offset + len) > SOC_NV_FLASH_SIZE)) {
		return false;
	}

	if (write) {
		/* Check offset and len aligned to write-block-size. */
		if ((offset % sizeof(flash_prg_t)) || (len % sizeof(flash_prg_t))) {
			return false;
		}
	} else {
		if ((offset % GD32_NV_FLASH_V4_PAGE_SIZE) || (len % GD32_NV_FLASH_V4_PAGE_SIZE)) {
			return false;
		}
	}

	return true;
}

int flash_gd32_write_range(off_t offset, const void *data, size_t len)
{
	int ret = 0;
	flash_prg_t *prg_flash = (flash_prg_t *)((uint8_t *)SOC_NV_FLASH_ADDR + offset);
	uint32_t *prg_data = (uint32_t *)data;

	gd32_fmc_v4_unlock();

	ret = gd32_fmc_v4_wait_idle();
	if (ret < 0) {
		goto expired_out;
	}

	FMC_CTL |= FMC_CTL_PG;
	__ISB();
	__DSB();

	for (size_t i = 0U; i < (len / sizeof(flash_prg_t)); i++) {
		*prg_flash++ = *prg_data++;
		__ISB();
		__DSB();
	}

	ret = gd32_fmc_v4_wait_idle();
	if (ret < 0) {
		goto expired_out;
	}

	if (FMC_STAT & GD32_FMC_V4_WRITE_ERR) {
		ret = -EIO;
		FMC_STAT |= GD32_FMC_V4_WRITE_ERR;
		LOG_ERR("FMC programming failed");
	}

expired_out:
	FMC_CTL &= ~FMC_CTL_PG;

	gd32_fmc_v4_lock();

	return ret;
}

static int gd32_fmc_v4_sector_erase(uint32_t address)
{
	int ret = 0;

	gd32_fmc_v4_unlock();

	ret = gd32_fmc_v4_wait_idle();
	if (ret < 0) {
		goto expired_out;
	}

	FMC_CTL |= FMC_CTL_SER;

	FMC_ADDR = address;

	FMC_CTL |= FMC_CTL_START;

	ret = gd32_fmc_v4_wait_idle();
	if (ret < 0) {
		goto expired_out;
	}

	if (FMC_STAT & GD32_FMC_V4_ERASE_ERR) {
		ret = -EIO;
		FMC_STAT |= GD32_FMC_V4_ERASE_ERR;
		LOG_ERR("FMC sector erase failed");
	}

expired_out:
	FMC_CTL &= ~FMC_CTL_SER;

	gd32_fmc_v4_lock();

	return ret;
}

int flash_gd32_erase_block(off_t offset, size_t size)
{
	uint32_t page_addr = SOC_NV_FLASH_ADDR + offset;
	int ret = 0;

	while (size > 0U) {
		ret = gd32_fmc_v4_sector_erase(page_addr);
		if (ret < 0) {
			gd32_fmc_v4_lock();
			return ret;
		}
		page_addr += GD32_NV_FLASH_V4_PAGE_SIZE;
		size -= GD32_NV_FLASH_V4_PAGE_SIZE;
	}

	return 0;
}

#ifdef CONFIG_FLASH_PAGE_LAYOUT
void flash_gd32_pages_layout(const struct device *dev, const struct flash_pages_layout **layout,
			     size_t *layout_size)
{
	ARG_UNUSED(dev);

	*layout = gd32_fmc_v4_layout;
	*layout_size = ARRAY_SIZE(gd32_fmc_v4_layout);
}
#endif /* CONFIG_FLASH_PAGE_LAYOUT */
