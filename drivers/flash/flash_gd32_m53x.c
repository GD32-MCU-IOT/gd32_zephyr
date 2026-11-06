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

/* Main flash; the data flash node does not carry "soc-nv-flash". */
#define GD32_NV_FLASH_M53X_NODE		SOC_NV_FLASH_NODE
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

BUILD_ASSERT(GD32_NV_FLASH_M53X_PAGE_SIZE == 1024U,
	     "GD32 M53x FMC requires page-size = <1024>");

#define GD32_FMC_M53X_WRITE_ERR (FMC_STAT_PGSERR | FMC_STAT_PGERR | \
				 FMC_STAT_PGAERR | FMC_STAT_WPERR)
#define GD32_FMC_M53X_ERASE_ERR (FMC_STAT_PGSERR | FMC_STAT_WPERR)

/* Main and data flash share one FMC and cannot be programmed concurrently. */
static K_SEM_DEFINE(gd32_fmc_m53x_sem, 1, 1);

#ifdef CONFIG_FLASH_PAGE_LAYOUT
static const struct flash_pages_layout gd32_fmc_m53x_layout[] = {
	{
		.pages_size = GD32_NV_FLASH_M53X_PAGE_SIZE,
		.pages_count = SOC_NV_FLASH_SIZE / GD32_NV_FLASH_M53X_PAGE_SIZE
	}
};
#endif

static inline void gd32_fmc_m53x_bank_select(bool data_flash)
{
	if (data_flash) {
		FMC_CTL |= FMC_CTL_BKSEL;
	} else {
		FMC_CTL &= ~FMC_CTL_BKSEL;
	}
}

static int gd32_fmc_m53x_wait_idle(uint32_t timeout_ms)
{
	const int64_t expired_time = k_uptime_get() + timeout_ms;

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

static bool gd32_fmc_m53x_range_ok(off_t offset, size_t len, uint32_t size,
				   uint32_t align)
{
	if ((offset < 0) || ((uint32_t)offset > size) ||
	    (len > (size - (uint32_t)offset))) {
		return false;
	}

	return ((offset % align) == 0) && ((len % align) == 0U);
}

bool flash_gd32_valid_range(off_t offset, uint32_t len, bool write)
{
	return gd32_fmc_m53x_range_ok(offset, len, SOC_NV_FLASH_SIZE,
				      write ? GD32_FMC_M53X_QUAD_SIZE :
					      GD32_NV_FLASH_M53X_PAGE_SIZE);
}

static int gd32_fmc_m53x_quad_program(uint32_t addr, const uint8_t *data,
				      uint32_t timeout_ms)
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

	ret = gd32_fmc_m53x_wait_idle(timeout_ms);
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

static int gd32_fmc_m53x_program(uint32_t addr, const uint8_t *src, size_t len,
				 bool data_flash, uint32_t timeout_ms)
{
	int ret;

	k_sem_take(&gd32_fmc_m53x_sem, K_FOREVER);

	fmc_unlock();

	ret = gd32_fmc_m53x_wait_idle(timeout_ms);
	if (ret < 0) {
		goto lock_out;
	}

	gd32_fmc_m53x_bank_select(data_flash);
	FMC_CTL |= FMC_CTL_PG;

	while (len > 0U) {
		ret = gd32_fmc_m53x_quad_program(addr, src, timeout_ms);
		if (ret < 0) {
			break;
		}

		addr += GD32_FMC_M53X_QUAD_SIZE;
		src += GD32_FMC_M53X_QUAD_SIZE;
		len -= GD32_FMC_M53X_QUAD_SIZE;
	}

	FMC_CTL &= ~(FMC_CTL_PG | FMC_CTL_BKSEL);

lock_out:
	fmc_lock();

	/* The FMC may still be busy after a timeout, so leave the caches alone. */
	if (ret != -ETIMEDOUT) {
		gd32_fmc_m53x_cache_reset();
	}

	k_sem_give(&gd32_fmc_m53x_sem);

	return ret;
}

static int gd32_fmc_m53x_page_erase(uint32_t page_addr, bool data_flash,
				    uint32_t timeout_ms)
{
	int ret;

	fmc_unlock();

	ret = gd32_fmc_m53x_wait_idle(timeout_ms);
	if (ret < 0) {
		goto lock_out;
	}

	gd32_fmc_m53x_bank_select(data_flash);
	FMC_CTL |= FMC_CTL_PER;
	FMC_ADDR = page_addr;
	FMC_CTL |= FMC_CTL_START;

	ret = gd32_fmc_m53x_wait_idle(timeout_ms);
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
	FMC_CTL &= ~(FMC_CTL_PER | FMC_CTL_BKSEL);

lock_out:
	fmc_lock();

	return ret;
}

static int gd32_fmc_m53x_erase(uint32_t page_addr, size_t size, uint32_t page_size,
			       bool data_flash, uint32_t timeout_ms)
{
	int ret = 0;

	k_sem_take(&gd32_fmc_m53x_sem, K_FOREVER);

	while (size > 0U) {
		ret = gd32_fmc_m53x_page_erase(page_addr, data_flash, timeout_ms);
		if (ret < 0) {
			break;
		}

		size -= page_size;
		page_addr += page_size;
	}

	if (ret != -ETIMEDOUT) {
		gd32_fmc_m53x_cache_reset();
	}

	k_sem_give(&gd32_fmc_m53x_sem);

	return ret;
}

int flash_gd32_write_range(off_t offset, const void *data, size_t len)
{
	return gd32_fmc_m53x_program(SOC_NV_FLASH_ADDR + offset, data, len, false,
				     GD32_NV_FLASH_M53X_TIMEOUT);
}

int flash_gd32_erase_block(off_t offset, size_t size)
{
	return gd32_fmc_m53x_erase(SOC_NV_FLASH_ADDR + offset, size,
				   GD32_NV_FLASH_M53X_PAGE_SIZE, false,
				   GD32_NV_FLASH_M53X_TIMEOUT);
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

/* Data flash (FMC_CTL.BKSEL = 1), registered as its own flash device. */
struct gd32_fmc_m53x_df_config {
	uint32_t addr;
	uint32_t size;
	uint32_t timeout_ms;
	struct flash_parameters parameters;
#ifdef CONFIG_FLASH_PAGE_LAYOUT
	struct flash_pages_layout layout;
#endif
};

static int gd32_fmc_m53x_df_read(const struct device *dev, off_t offset,
				 void *data, size_t len)
{
	const struct gd32_fmc_m53x_df_config *cfg = dev->config;

	if (!gd32_fmc_m53x_range_ok(offset, len, cfg->size, 1U)) {
		return -EINVAL;
	}

	if (len == 0U) {
		return 0;
	}

	k_sem_take(&gd32_fmc_m53x_sem, K_FOREVER);
	memcpy(data, (uint8_t *)cfg->addr + offset, len);
	k_sem_give(&gd32_fmc_m53x_sem);

	return 0;
}

static int gd32_fmc_m53x_df_write(const struct device *dev, off_t offset,
				  const void *data, size_t len)
{
	const struct gd32_fmc_m53x_df_config *cfg = dev->config;

	if (!gd32_fmc_m53x_range_ok(offset, len, cfg->size, GD32_FMC_M53X_QUAD_SIZE)) {
		return -EINVAL;
	}

	if (len == 0U) {
		return 0;
	}

	return gd32_fmc_m53x_program(cfg->addr + offset, data, len, true, cfg->timeout_ms);
}

static int gd32_fmc_m53x_df_erase(const struct device *dev, off_t offset, size_t size)
{
	const struct gd32_fmc_m53x_df_config *cfg = dev->config;

	if (size == 0U) {
		return 0;
	}

	if (!gd32_fmc_m53x_range_ok(offset, size, cfg->size, GD32_NV_FLASH_M53X_PAGE_SIZE)) {
		return -EINVAL;
	}

	return gd32_fmc_m53x_erase(cfg->addr + offset, size, GD32_NV_FLASH_M53X_PAGE_SIZE,
				   true, cfg->timeout_ms);
}

static const struct flash_parameters *
gd32_fmc_m53x_df_get_parameters(const struct device *dev)
{
	const struct gd32_fmc_m53x_df_config *cfg = dev->config;

	return &cfg->parameters;
}

static int gd32_fmc_m53x_df_get_size(const struct device *dev, uint64_t *size)
{
	const struct gd32_fmc_m53x_df_config *cfg = dev->config;

	*size = cfg->size;

	return 0;
}

#ifdef CONFIG_FLASH_PAGE_LAYOUT
static void gd32_fmc_m53x_df_pages_layout(const struct device *dev,
					  const struct flash_pages_layout **layout,
					  size_t *layout_size)
{
	const struct gd32_fmc_m53x_df_config *cfg = dev->config;

	*layout = &cfg->layout;
	*layout_size = 1U;
}
#endif /* CONFIG_FLASH_PAGE_LAYOUT */

static DEVICE_API(flash, gd32_fmc_m53x_df_api) = {
	.read = gd32_fmc_m53x_df_read,
	.write = gd32_fmc_m53x_df_write,
	.erase = gd32_fmc_m53x_df_erase,
	.get_parameters = gd32_fmc_m53x_df_get_parameters,
	.get_size = gd32_fmc_m53x_df_get_size,
#ifdef CONFIG_FLASH_PAGE_LAYOUT
	.page_layout = gd32_fmc_m53x_df_pages_layout,
#endif
};

#define GD32_FMC_M53X_DF_DEFINE(node_id)						\
	BUILD_ASSERT(DT_PROP(node_id, write_block_size) == GD32_FMC_M53X_QUAD_SIZE,	\
		     "GD32 M53x data flash requires write-block-size = <16>");		\
	BUILD_ASSERT(DT_PROP(node_id, page_size) == GD32_NV_FLASH_M53X_PAGE_SIZE,	\
		     "GD32 M53x data flash requires page-size = <1024>");		\
	static const struct gd32_fmc_m53x_df_config gd32_fmc_m53x_df_config_##node_id = { \
		.addr = DT_REG_ADDR(node_id),						\
		.size = DT_REG_SIZE(node_id),						\
		.timeout_ms = DT_PROP(node_id, max_erase_time_ms),			\
		.parameters = {								\
			.write_block_size = DT_PROP(node_id, write_block_size),	\
			.erase_value = 0xff,						\
		},									\
		IF_ENABLED(CONFIG_FLASH_PAGE_LAYOUT, (					\
		.layout = {								\
			.pages_size = DT_PROP(node_id, page_size),			\
			.pages_count = DT_REG_SIZE(node_id) / DT_PROP(node_id, page_size), \
		},))									\
	};										\
	DEVICE_DT_DEFINE(node_id, NULL, NULL, NULL, &gd32_fmc_m53x_df_config_##node_id,	\
			 POST_KERNEL, CONFIG_FLASH_INIT_PRIORITY, &gd32_fmc_m53x_df_api);

/* The data flash is the flash child that does not carry "soc-nv-flash". */
#define GD32_FMC_M53X_DF_DEFINE_IF_DATA(node_id)					\
	COND_CODE_1(DT_NODE_HAS_COMPAT(node_id, soc_nv_flash), (),			\
		    (GD32_FMC_M53X_DF_DEFINE(node_id)))

DT_FOREACH_CHILD_STATUS_OKAY(DT_INST(0, gd_gd32m53x_flash_controller),
			     GD32_FMC_M53X_DF_DEFINE_IF_DATA)
