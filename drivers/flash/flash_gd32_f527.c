/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "flash_gd32.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <gd32_fmc.h>

LOG_MODULE_DECLARE(flash_gd32);

#define GD32_NV_FLASH_F527_NODE		DT_INST(0, gd_gd32_nv_flash_f527)
#define GD32_NV_FLASH_F527_TIMEOUT	DT_PROP(GD32_NV_FLASH_F527_NODE, max_erase_time_ms)
#define GD32_NV_FLASH_F527_PAGE_SIZE	DT_PROP(GD32_NV_FLASH_F527_NODE, page_size)

#define GD32_FMC_F527_DWORD_SIZE	SOC_NV_FLASH_PRG_SIZE
#define GD32_FMC_F527_DWORD_WORDS	(GD32_FMC_F527_DWORD_SIZE / sizeof(uint32_t))

BUILD_ASSERT(GD32_FMC_F527_DWORD_SIZE == 8U,
	     "GD32 F527 FMC requires write-block-size = <8>");
BUILD_ASSERT(GD32_NV_FLASH_F527_PAGE_SIZE == 4096U,
	     "GD32 F527 FMC page erase requires page-size = <4096>");

#define GD32_FMC_F527_WRITE_ERR	(FMC_STAT_OPERR | FMC_STAT_PGSERR | FMC_STAT_PGMERR | \
				 FMC_STAT_PGAERR | FMC_STAT_WPERR)
#define GD32_FMC_F527_ERASE_ERR	(FMC_STAT_OPERR | FMC_STAT_WPERR)

#ifdef CONFIG_FLASH_PAGE_LAYOUT
static const struct flash_pages_layout gd32_fmc_f527_layout[] = {
	{
		.pages_size = GD32_NV_FLASH_F527_PAGE_SIZE,
		.pages_count = SOC_NV_FLASH_SIZE / GD32_NV_FLASH_F527_PAGE_SIZE
	}
};
#endif

static inline void gd32_fmc_f527_unlock(void)
{
	if (FMC_CTL & FMC_CTL_LK) {
		FMC_KEY = UNLOCK_KEY0;
		FMC_KEY = UNLOCK_KEY1;
	}
}

static inline void gd32_fmc_f527_lock(void)
{
	FMC_CTL |= FMC_CTL_LK;
}

static int gd32_fmc_f527_wait_idle(void)
{
	const int64_t expired_time = k_uptime_get() + GD32_NV_FLASH_F527_TIMEOUT;

	while (FMC_STAT & FMC_STAT_BUSY) {
		if (k_uptime_get() > expired_time) {
			return -ETIMEDOUT;
		}
	}

	return 0;
}

bool flash_gd32_valid_range(off_t offset, uint32_t len, bool write)
{
	if ((offset < 0) || ((uint32_t)offset > SOC_NV_FLASH_SIZE) ||
	    (len > (SOC_NV_FLASH_SIZE - (uint32_t)offset))) {
		return false;
	}

	if (write) {
		/* Check offset and len are double-word aligned. */
		if ((offset % GD32_FMC_F527_DWORD_SIZE) ||
		    (len % GD32_FMC_F527_DWORD_SIZE)) {
			return false;
		}
	} else {
		if ((offset % GD32_NV_FLASH_F527_PAGE_SIZE) ||
		    (len % GD32_NV_FLASH_F527_PAGE_SIZE)) {
			return false;
		}
	}

	return true;
}

static int gd32_fmc_f527_dword_program(uint32_t addr, const uint8_t *data)
{
	volatile uint32_t *dst = (volatile uint32_t *)addr;
	uint32_t word[GD32_FMC_F527_DWORD_WORDS];
	int ret;

	/* The caller buffer carries no alignment guarantee. */
	memcpy(word, data, sizeof(word));

	/* With DWPGE set the FMC joins the low and high words into one double-word. */
	dst[0] = word[0];
	dst[1] = word[1];

	__DSB();

	ret = gd32_fmc_f527_wait_idle();
	if (ret < 0) {
		return ret;
	}

	if (FMC_STAT & GD32_FMC_F527_WRITE_ERR) {
		FMC_STAT = GD32_FMC_F527_WRITE_ERR;
		LOG_ERR("FMC double-word program at 0x%08x failed", addr);
		return -EIO;
	}

	FMC_STAT = FMC_STAT_END;

	return 0;
}

int flash_gd32_write_range(off_t offset, const void *data, size_t len)
{
	uint32_t addr = SOC_NV_FLASH_ADDR + offset;
	const uint8_t *src = data;
	int ret;

	gd32_fmc_f527_unlock();

	ret = gd32_fmc_f527_wait_idle();
	if (ret < 0) {
		goto lock_out;
	}

	FMC_CTL |= FMC_CTL_DWPGE;
	FMC_CTL |= FMC_CTL_PG;

	while (len > 0U) {
		ret = gd32_fmc_f527_dword_program(addr, src);
		if (ret < 0) {
			break;
		}

		addr += GD32_FMC_F527_DWORD_SIZE;
		src += GD32_FMC_F527_DWORD_SIZE;
		len -= GD32_FMC_F527_DWORD_SIZE;
	}

	FMC_CTL &= ~(FMC_CTL_PG | FMC_CTL_DWPGE);

lock_out:
	gd32_fmc_f527_lock();

	return ret;
}

static int gd32_fmc_f527_page_erase(uint32_t page_addr)
{
	int ret;

	gd32_fmc_f527_unlock();

	ret = gd32_fmc_f527_wait_idle();
	if (ret < 0) {
		goto lock_out;
	}

	/* The page erase function is unlocked by PEKEY and needs SN to be zero. */
	FMC_PEKEY = UNLOCK_PE_KEY;
	FMC_PECFG = FMC_PECFG_PE_EN | (page_addr & FMC_PECFG_PE_ADDR);
	FMC_CTL &= ~(FMC_CTL_SN | FMC_CTL_SN_5);
	FMC_CTL |= FMC_CTL_SER;
	FMC_CTL |= FMC_CTL_START;

	ret = gd32_fmc_f527_wait_idle();
	if (ret < 0) {
		goto clear_out;
	}

	if (FMC_STAT & GD32_FMC_F527_ERASE_ERR) {
		ret = -EIO;
		FMC_STAT = GD32_FMC_F527_ERASE_ERR;
		LOG_ERR("FMC page 0x%08x erase failed", page_addr);
	} else {
		FMC_STAT = FMC_STAT_END;
	}

clear_out:
	FMC_PECFG &= ~FMC_PECFG_PE_EN;
	FMC_CTL &= ~FMC_CTL_SER;

lock_out:
	gd32_fmc_f527_lock();

	return ret;
}

int flash_gd32_erase_block(off_t offset, size_t size)
{
	uint32_t page_addr = SOC_NV_FLASH_ADDR + offset;
	int ret = 0;

	while (size > 0U) {
		ret = gd32_fmc_f527_page_erase(page_addr);
		if (ret < 0) {
			break;
		}

		size -= GD32_NV_FLASH_F527_PAGE_SIZE;
		page_addr += GD32_NV_FLASH_F527_PAGE_SIZE;
	}

	return ret;
}

#ifdef CONFIG_FLASH_PAGE_LAYOUT
void flash_gd32_pages_layout(const struct device *dev,
			     const struct flash_pages_layout **layout,
			     size_t *layout_size)
{
	ARG_UNUSED(dev);

	*layout = gd32_fmc_f527_layout;
	*layout_size = ARRAY_SIZE(gd32_fmc_f527_layout);
}
#endif /* CONFIG_FLASH_PAGE_LAYOUT */
