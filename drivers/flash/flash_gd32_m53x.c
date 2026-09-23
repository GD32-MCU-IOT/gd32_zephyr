/*
 * Copyright (c) 2025 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT gd_gd32_flash_controller

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <gd32_fmc.h>

LOG_MODULE_REGISTER(flash_gd32_m53x, CONFIG_FLASH_LOG_LEVEL);

#define GD32_NV_FLASH_M53X_NODE		DT_INST(0, gd_gd32_nv_flash_m53x)
#define GD32_NV_FLASH_M53X_ADDR		DT_REG_ADDR(GD32_NV_FLASH_M53X_NODE)
#define GD32_NV_FLASH_M53X_SIZE		DT_REG_SIZE(GD32_NV_FLASH_M53X_NODE)
#define GD32_NV_FLASH_M53X_TIMEOUT	DT_PROP(GD32_NV_FLASH_M53X_NODE, max_erase_time_ms)
#define GD32_NV_FLASH_M53X_PAGE_SIZE	DT_PROP(GD32_NV_FLASH_M53X_NODE, page_size)

/*
 * The main flash keeps a 9-bit ECC next to every 128-bit quad-word, so a
 * quad-word that is not fully programmed is silently discarded by the FMC.
 * That rules out the scalar programming unit the v1..v4 backends build on,
 * hence this driver does not share flash_gd32.c.
 */
#define GD32_FMC_M53X_QUAD_SIZE		DT_PROP(GD32_NV_FLASH_M53X_NODE, write_block_size)
#define GD32_FMC_M53X_QUAD_WORDS	(GD32_FMC_M53X_QUAD_SIZE / sizeof(uint32_t))

BUILD_ASSERT(GD32_FMC_M53X_QUAD_SIZE == 16U,
	     "GD32 M53x FMC requires write-block-size = <16>");

#define GD32_FMC_M53X_WRITE_ERR	(FMC_STAT_PGSERR | FMC_STAT_PGERR | \
				 FMC_STAT_PGAERR | FMC_STAT_WPERR)
#define GD32_FMC_M53X_ERASE_ERR	(FMC_STAT_PGSERR | FMC_STAT_WPERR)

struct flash_gd32_m53x_data {
	struct k_sem mutex;
};

static struct flash_gd32_m53x_data flash_data;

static const struct flash_parameters flash_gd32_m53x_parameters = {
	.write_block_size = GD32_FMC_M53X_QUAD_SIZE,
	.erase_value = 0xff,
};

#ifdef CONFIG_FLASH_PAGE_LAYOUT
static const struct flash_pages_layout gd32_fmc_m53x_layout[] = {
	{
		.pages_size = GD32_NV_FLASH_M53X_PAGE_SIZE,
		.pages_count = GD32_NV_FLASH_M53X_SIZE / GD32_NV_FLASH_M53X_PAGE_SIZE
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

static bool gd32_fmc_m53x_valid_range(off_t offset, uint32_t len, bool write)
{
	if ((offset < 0) || (offset > GD32_NV_FLASH_M53X_SIZE) ||
	    ((offset + len) > GD32_NV_FLASH_M53X_SIZE)) {
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

static int gd32_fmc_m53x_write_range(off_t offset, const void *data, size_t len)
{
	uint32_t addr = GD32_NV_FLASH_M53X_ADDR + offset;
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

static int gd32_fmc_m53x_erase_block(off_t offset, size_t size)
{
	uint32_t page_addr = GD32_NV_FLASH_M53X_ADDR + offset;
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
static void flash_gd32_m53x_pages_layout(const struct device *dev,
					 const struct flash_pages_layout **layout,
					 size_t *layout_size)
{
	ARG_UNUSED(dev);

	*layout = gd32_fmc_m53x_layout;
	*layout_size = ARRAY_SIZE(gd32_fmc_m53x_layout);
}
#endif /* CONFIG_FLASH_PAGE_LAYOUT */

static int flash_gd32_m53x_read(const struct device *dev, off_t offset,
				void *data, size_t len)
{
	ARG_UNUSED(dev);

	if ((offset < 0) || ((uint32_t)offset > GD32_NV_FLASH_M53X_SIZE) ||
	    (len > (GD32_NV_FLASH_M53X_SIZE - (uint32_t)offset))) {
		return -EINVAL;
	}

	if (len == 0U) {
		return 0;
	}

	memcpy(data, (uint8_t *)GD32_NV_FLASH_M53X_ADDR + offset, len);

	return 0;
}

static int flash_gd32_m53x_write(const struct device *dev, off_t offset,
				 const void *data, size_t len)
{
	struct flash_gd32_m53x_data *dev_data = dev->data;
	int ret;

	if (!gd32_fmc_m53x_valid_range(offset, len, true)) {
		return -EINVAL;
	}

	if (len == 0U) {
		return 0;
	}

	k_sem_take(&dev_data->mutex, K_FOREVER);

	ret = gd32_fmc_m53x_write_range(offset, data, len);

	k_sem_give(&dev_data->mutex);

	return ret;
}

static int flash_gd32_m53x_erase(const struct device *dev, off_t offset, size_t size)
{
	struct flash_gd32_m53x_data *dev_data = dev->data;
	int ret;

	if (size == 0U) {
		return 0;
	}

	if (!gd32_fmc_m53x_valid_range(offset, size, false)) {
		return -EINVAL;
	}

	k_sem_take(&dev_data->mutex, K_FOREVER);

	ret = gd32_fmc_m53x_erase_block(offset, size);

	k_sem_give(&dev_data->mutex);

	return ret;
}

static const struct flash_parameters *
flash_gd32_m53x_get_parameters(const struct device *dev)
{
	ARG_UNUSED(dev);

	return &flash_gd32_m53x_parameters;
}

static int flash_gd32_m53x_get_size(const struct device *dev, uint64_t *size)
{
	ARG_UNUSED(dev);

	*size = GD32_NV_FLASH_M53X_SIZE;

	return 0;
}

static DEVICE_API(flash, flash_gd32_m53x_driver_api) = {
	.read = flash_gd32_m53x_read,
	.write = flash_gd32_m53x_write,
	.erase = flash_gd32_m53x_erase,
	.get_parameters = flash_gd32_m53x_get_parameters,
	.get_size = flash_gd32_m53x_get_size,
#ifdef CONFIG_FLASH_PAGE_LAYOUT
	.page_layout = flash_gd32_m53x_pages_layout,
#endif
};

static int flash_gd32_m53x_init(const struct device *dev)
{
	struct flash_gd32_m53x_data *dev_data = dev->data;

	k_sem_init(&dev_data->mutex, 1, 1);

	return 0;
}

DEVICE_DT_INST_DEFINE(0, flash_gd32_m53x_init, NULL,
		      &flash_data, NULL, POST_KERNEL,
		      CONFIG_FLASH_INIT_PRIORITY, &flash_gd32_m53x_driver_api);
