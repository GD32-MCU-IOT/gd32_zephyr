/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "flash_gd32.h"

#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/gd32.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/sys/byteorder.h>
#include <gd32_fmc.h>
#include <gd32_syscfg.h>

LOG_MODULE_DECLARE(flash_gd32);

#define GD32_NV_FLASH_G5X3_NODE         DT_INST(0, gd_gd32_nv_flash)
#define GD32_NV_FLASH_G5X3_TIMEOUT      DT_PROP(GD32_NV_FLASH_G5X3_NODE, max_erase_time_ms)
/* Devicetree page size describes the factory default dual-bank mode. */
#define GD32_NV_FLASH_G5X3_DUAL_PAGE_SIZE   DT_PROP(GD32_NV_FLASH_G5X3_NODE, page_size)
#define GD32_NV_FLASH_G5X3_SINGLE_PAGE_SIZE KB(2)

/*
 * GD32G5x3 FMC: single register set (FMC_KEY/FMC_CTL/FMC_STAT, there is no
 * FMC_ADDR). Page erase selects the target by page number (PNSEL) plus bank
 * number (BKSEL), and the main flash is programmed in 64-bit double words
 * protected by an 8-bit ECC. The register layout and operation model are
 * not compatible with the v1..v4 backends, which program plain words and
 * locate erase pages through FMC_ADDR or sector numbers.
 */
#define GD32_FMC_G5X3_WRITE_ERR \
	(FMC_STAT_PGERR | FMC_STAT_WPERR | FMC_STAT_PGAERR | \
	 FMC_STAT_PGMERR | FMC_STAT_PGSERR)
#define GD32_FMC_G5X3_ERASE_ERR FMC_STAT_WPERR

/* Dual-bank mode (OBCTL.DBS=1): bank0 is the first 256KB. */
#if (PRE_KB(256) >= SOC_NV_FLASH_SIZE)
#define GD32_NV_FLASH_G5X3_BANK0_SIZE   SOC_NV_FLASH_SIZE
#else
#define GD32_NV_FLASH_G5X3_BANK0_SIZE   KB(256)
#endif

BUILD_ASSERT(SOC_NV_FLASH_PRG_SIZE == 8U,
	     "GD32 G5x3 FMC requires write-block-size = <8>");
/* PNSEL is an 8-bit field, so each bank holds at most 256 pages. */
BUILD_ASSERT((GD32_NV_FLASH_G5X3_BANK0_SIZE / GD32_NV_FLASH_G5X3_DUAL_PAGE_SIZE) <= 256U,
	     "GD32 G5x3 page size is too small for the PNSEL field");
BUILD_ASSERT((SOC_NV_FLASH_SIZE / GD32_NV_FLASH_G5X3_SINGLE_PAGE_SIZE) <= 256U,
	     "GD32 G5x3 single-bank flash is too large for the PNSEL field");

#ifdef CONFIG_FLASH_PAGE_LAYOUT
static struct flash_pages_layout gd32_fmc_g5x3_layout[1];
#endif

/* Bank mode comes from the option bytes (OBCTL.DBS), loaded at reset. */
static inline bool gd32_fmc_g5x3_dual_bank(void)
{
	return (FMC_OBCTL & FMC_OBCTL_DBS) != 0U;
}

static inline uint32_t gd32_fmc_g5x3_page_size(void)
{
	return gd32_fmc_g5x3_dual_bank() ? GD32_NV_FLASH_G5X3_DUAL_PAGE_SIZE
					 : GD32_NV_FLASH_G5X3_SINGLE_PAGE_SIZE;
}

/* SYSCFG_CFG0.FMC_SWP = 0 maps bank1 at the flash base address. */
static bool gd32_fmc_g5x3_banks_swapped(void)
{
	uint16_t clkid = DT_CLOCKS_CELL(DT_NODELABEL(syscfg), id);

	/* SYSCFG must be clocked for FMC_SWP to read back its real value. */
	(void)clock_control_on(GD32_CLOCK_CONTROLLER, (clock_control_subsys_t)&clkid);

	return (SYSCFG_CFG0 & SYSCFG_CFG0_FMC_SWP) == 0U;
}

static inline void gd32_fmc_g5x3_unlock(void)
{
	FMC_KEY = FMC_UNLOCK_KEY0;
	FMC_KEY = FMC_UNLOCK_KEY1;
}

static inline void gd32_fmc_g5x3_lock(void)
{
	FMC_CTL |= FMC_CTL_LK;
}

static int gd32_fmc_g5x3_wait_idle(void)
{
	const int64_t expired_time = k_uptime_get() + GD32_NV_FLASH_G5X3_TIMEOUT;

	while (FMC_STAT & FMC_STAT_BUSY) {
		if (k_uptime_get() > expired_time) {
			return -ETIMEDOUT;
		}
	}

	return 0;
}

/* One PG cycle programs one 64-bit double word as two word writes, low word
 * first (write order required by the FMC, UM section 2.3.8).
 */
static inline void gd32_fmc_g5x3_dword_write(flash_prg_t *dst, const flash_prg_t *src)
{
	volatile uint32_t *dst32 = (volatile uint32_t *)dst;

	dst32[0] = sys_get_le32((const uint8_t *)src);
	barrier_isync_fence_full();
	dst32[1] = sys_get_le32((const uint8_t *)src + 4U);
}

static int gd32_fmc_g5x3_page_erase(uint32_t page_num, bool bksel)
{
	uint32_t ctl;
	int ret = 0;

	gd32_fmc_g5x3_unlock();

	ret = gd32_fmc_g5x3_wait_idle();
	if (ret < 0) {
		goto lock_out;
	}

	ctl = FMC_CTL & ~(FMC_CTL_PNSEL | FMC_CTL_BKSEL);
	ctl |= FMC_CTL_PER | ((page_num << 3) & FMC_CTL_PNSEL);
	if (bksel) {
		ctl |= FMC_CTL_BKSEL;
	}
	FMC_CTL = ctl;

	FMC_CTL |= FMC_CTL_START;

	ret = gd32_fmc_g5x3_wait_idle();
	if (ret < 0) {
		goto clear_out;
	}

	if (FMC_STAT & GD32_FMC_G5X3_ERASE_ERR) {
		ret = -EIO;
		FMC_STAT |= GD32_FMC_G5X3_ERASE_ERR;
		LOG_ERR("FMC bank %u page %u erase failed", bksel ? 1U : 0U, page_num);
	} else {
		FMC_STAT |= FMC_STAT_ENDF;
	}

clear_out:
	FMC_CTL &= ~(FMC_CTL_PER | FMC_CTL_PNSEL | FMC_CTL_BKSEL);

lock_out:
	gd32_fmc_g5x3_lock();

	return ret;
}

/* Programming is bank agnostic: one register set, plain address writes. */
static int gd32_fmc_g5x3_write(off_t offset, const void *data, size_t len)
{
	flash_prg_t *prg_flash = (flash_prg_t *)((uint8_t *)SOC_NV_FLASH_ADDR + offset);
	flash_prg_t *prg_data = (flash_prg_t *)data;
	int ret = 0;

	gd32_fmc_g5x3_unlock();

	ret = gd32_fmc_g5x3_wait_idle();
	if (ret < 0) {
		goto lock_out;
	}

	FMC_CTL |= FMC_CTL_PG;

	for (size_t i = 0U; i < (len / sizeof(flash_prg_t)); i++) {
		gd32_fmc_g5x3_dword_write(prg_flash++, prg_data++);

		/* Each double word carries ECC, check every PG cycle. */
		ret = gd32_fmc_g5x3_wait_idle();
		if (ret < 0) {
			goto expired_out;
		}

		if (FMC_STAT & GD32_FMC_G5X3_WRITE_ERR) {
			ret = -EIO;
			FMC_STAT |= GD32_FMC_G5X3_WRITE_ERR;
			LOG_ERR("FMC double-word program at 0x%08x failed",
				(uint32_t)(SOC_NV_FLASH_ADDR + offset) +
					i * sizeof(flash_prg_t));
			goto expired_out;
		}
	}

expired_out:
	FMC_CTL &= ~FMC_CTL_PG;

lock_out:
	gd32_fmc_g5x3_lock();

	return ret;
}

bool flash_gd32_valid_range(off_t offset, uint32_t len, bool write)
{
	if ((offset < 0) || ((uint32_t)offset > SOC_NV_FLASH_SIZE) ||
	    (len > (SOC_NV_FLASH_SIZE - (uint32_t)offset))) {
		return false;
	}

	if (write) {
		/* Check offset and len is flash_prg_t aligned. */
		if ((offset % sizeof(flash_prg_t)) ||
		    (len % sizeof(flash_prg_t))) {
			return false;
		}
	} else {
		uint32_t page_size = gd32_fmc_g5x3_page_size();

		if ((offset % page_size) || (len % page_size)) {
			return false;
		}
	}

	return true;
}

int flash_gd32_write_range(off_t offset, const void *data, size_t len)
{
	return gd32_fmc_g5x3_write(offset, data, len);
}

int flash_gd32_erase_block(off_t offset, size_t size)
{
	const bool dual_bank = gd32_fmc_g5x3_dual_bank();
	const bool swapped = dual_bank && gd32_fmc_g5x3_banks_swapped();
	const uint32_t page_size = gd32_fmc_g5x3_page_size();
	uint32_t addr = (uint32_t)offset;
	int ret = 0;

	while (size > 0U) {
		uint32_t page_num;
		bool bksel = false;

		if (dual_bank) {
			bool bank1 = addr >= GD32_NV_FLASH_G5X3_BANK0_SIZE;

			page_num = (addr - (bank1 ? GD32_NV_FLASH_G5X3_BANK0_SIZE : 0U)) /
				   page_size;
			/* BKSEL selects the physical bank behind the logical address. */
			bksel = bank1 != swapped;
		} else {
			/* Single-bank mode requires BKSEL to stay cleared. */
			page_num = addr / page_size;
		}

		ret = gd32_fmc_g5x3_page_erase(page_num, bksel);
		if (ret < 0) {
			return ret;
		}

		size -= page_size;
		addr += page_size;
	}

	return 0;
}

#ifdef CONFIG_FLASH_PAGE_LAYOUT
void flash_gd32_pages_layout(const struct device *dev,
			     const struct flash_pages_layout **layout,
			     size_t *layout_size)
{
	ARG_UNUSED(dev);

	gd32_fmc_g5x3_layout[0].pages_size = gd32_fmc_g5x3_page_size();
	gd32_fmc_g5x3_layout[0].pages_count =
		SOC_NV_FLASH_SIZE / gd32_fmc_g5x3_layout[0].pages_size;

	*layout = gd32_fmc_g5x3_layout;
	*layout_size = ARRAY_SIZE(gd32_fmc_g5x3_layout);
}
#endif
