/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "flash_gd32.h"

#include <string.h>
#include <zephyr/cache.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <gd32_nvmc.h>

LOG_MODULE_DECLARE(flash_gd32);

#define GD32_NVMC_CTRL_NODE DT_INST(0, gd_gd32h77x_78x_flash_controller)

#define GD32_NVMC_CNVM_NODE      SOC_NV_FLASH_NODE
#define GD32_NVMC_CNVM_TIMEOUT   DT_PROP(GD32_NVMC_CNVM_NODE, max_erase_time_ms)
#define GD32_NVMC_CNVM_PAGE_SIZE DT_PROP(GD32_NVMC_CNVM_NODE, page_size)

/* CNVM and ECNVM are both programmed in 256-bit words through four 64-bit stores. */
#define GD32_NVMC_PRG_SIZE   32U
#define GD32_NVMC_PRG_DWORDS (GD32_NVMC_PRG_SIZE / sizeof(uint64_t))

BUILD_ASSERT(SOC_NV_FLASH_PRG_SIZE == GD32_NVMC_PRG_SIZE,
	     "GD32H77x/78x CNVM requires write-block-size = <32>");

BUILD_ASSERT(SOC_NV_FLASH_ADDR == CNVM_BASE_ADDRESS,
	     "GD32H77x/78x soc-nv-flash node must be the CNVM");

#define GD32_NVMC_CNVM_ERR  (NVMC_CNVM_STAT_WPERR | NVMC_CNVM_STAT_PGSERR | \
			     NVMC_CNVM_STAT_RPERR | NVMC_CNVM_STAT_RSERR)
#define GD32_NVMC_ECNVM_ERR (NVMC_ECNVM_STAT_WPERR | NVMC_ECNVM_STAT_PGSERR | \
			     NVMC_ECNVM_STAT_RPERR | NVMC_ECNVM_STAT_RSERR)

/* CNVM and ECNVM share one NVMC and CNVM programming may also busy the ECNVM. */
static K_SEM_DEFINE(gd32_nvmc_sem, 1, 1);

#ifdef CONFIG_FLASH_PAGE_LAYOUT
static const struct flash_pages_layout gd32_nvmc_cnvm_layout[] = {
	{
		.pages_size = GD32_NVMC_CNVM_PAGE_SIZE,
		.pages_count = SOC_NV_FLASH_SIZE / GD32_NVMC_CNVM_PAGE_SIZE
	}
};
#endif

/* Register set of one NVM area, CNVM and ECNVM share the same bit layout. */
struct gd32_nvmc_regs {
	volatile uint32_t *key;
	volatile uint32_t *ctl;
	volatile uint32_t *stat;
	volatile uint32_t *addr;
	uint32_t ctl_lk;
	uint32_t ctl_pg;
	uint32_t ctl_erase;
	uint32_t ctl_start;
	uint32_t stat_busy;
	uint32_t stat_end;
	uint32_t stat_err;
};

static const struct gd32_nvmc_regs gd32_nvmc_cnvm_regs = {
	.key = &NVMC_CNVM_KEY,
	.ctl = &NVMC_CNVM_CTL,
	.stat = &NVMC_CNVM_STAT,
	.addr = &NVMC_CNVM_ADDR,
	.ctl_lk = NVMC_CNVM_CTL_LK,
	.ctl_pg = NVMC_CNVM_CTL_PG,
	.ctl_erase = NVMC_CNVM_CTL_SER,
	.ctl_start = NVMC_CNVM_CTL_START,
	.stat_busy = NVMC_CNVM_STAT_BUSY,
	.stat_end = NVMC_CNVM_STAT_ENDF,
	.stat_err = GD32_NVMC_CNVM_ERR,
};

static const struct gd32_nvmc_regs gd32_nvmc_ecnvm_regs = {
	.key = &NVMC_ECNVM_KEY,
	.ctl = &NVMC_ECNVM_CTL,
	.stat = &NVMC_ECNVM_STAT,
	.addr = &NVMC_ECNVM_ADDR,
	.ctl_lk = NVMC_ECNVM_CTL_LK,
	.ctl_pg = NVMC_ECNVM_CTL_PG,
	.ctl_erase = NVMC_ECNVM_CTL_PER,
	.ctl_start = NVMC_ECNVM_CTL_START,
	.stat_busy = NVMC_ECNVM_STAT_BUSY,
	.stat_end = NVMC_ECNVM_STAT_ENDF,
	.stat_err = GD32_NVMC_ECNVM_ERR,
};

static inline void gd32_nvmc_unlock(const struct gd32_nvmc_regs *regs)
{
	if (*regs->ctl & regs->ctl_lk) {
		*regs->key = UNLOCK_KEY0;
		*regs->key = UNLOCK_KEY1;
	}
}

static inline void gd32_nvmc_lock(const struct gd32_nvmc_regs *regs)
{
	*regs->ctl |= regs->ctl_lk;
}

static int gd32_nvmc_wait_idle(const struct gd32_nvmc_regs *regs, uint32_t timeout_ms)
{
	const int64_t expired_time = k_uptime_get() + timeout_ms;

	while (*regs->stat & regs->stat_busy) {
		if (k_uptime_get() > expired_time) {
			return -ETIMEDOUT;
		}
	}

	return 0;
}

/* CNVM operations may involve the ECNVM in data integrity enhance mode. */
static int gd32_nvmc_wait_all_idle(uint32_t timeout_ms)
{
	int ret;

	ret = gd32_nvmc_wait_idle(&gd32_nvmc_cnvm_regs, timeout_ms);
	if (ret < 0) {
		return ret;
	}

	return gd32_nvmc_wait_idle(&gd32_nvmc_ecnvm_regs, timeout_ms);
}

static int gd32_nvmc_check_status(const struct gd32_nvmc_regs *regs, uint32_t addr)
{
	uint32_t stat = *regs->stat;

	if (stat & regs->stat_err) {
		*regs->stat = stat & regs->stat_err;
		LOG_ERR("NVMC operation at 0x%08x failed, stat 0x%08x", addr, stat);
		return -EIO;
	}

	*regs->stat = regs->stat_end;

	return 0;
}

/* Drop CNVM read buffer lines that may still hold pre-operation data. */
static void gd32_nvmc_cnvm_buf_empty(void)
{
	const int64_t expired_time = k_uptime_get() + 10;

	NVMC_PERFCTL |= NVMC_PERFCTL_EMPTY_CNVM_BUF;

	while (NVMC_CS & NVMC_CS_ECBBUSY) {
		if (k_uptime_get() > expired_time) {
			LOG_WRN("NVMC CNVM buffer empty timeout");
			break;
		}
	}
}

static bool gd32_nvmc_range_ok(off_t offset, size_t len, uint32_t size, uint32_t align)
{
	if ((offset < 0) || ((uint32_t)offset > size) ||
	    (len > (size - (uint32_t)offset))) {
		return false;
	}

	return ((offset % align) == 0) && ((len % align) == 0U);
}

static void gd32_nvmc_word_store(uint32_t addr, const uint8_t *data)
{
	uint64_t dword[GD32_NVMC_PRG_DWORDS];

	/* The caller buffer carries no alignment guarantee. */
	memcpy(dword, data, sizeof(dword));

	__ISB();
	__DSB();

	for (size_t i = 0U; i < GD32_NVMC_PRG_DWORDS; i++) {
		uint64_t val = dword[i];

		/* Each 64-bit store must be a single strd, as required by the NVMC. */
		__asm__ volatile("strd %Q[val], %R[val], [%[addr]]"
				 :
				 : [addr] "r"(addr), [val] "r"(val)
				 : "memory");
		__DMB();
		addr += sizeof(uint64_t);
	}
}

static int gd32_nvmc_program(const struct gd32_nvmc_regs *regs, uint32_t addr,
			     const uint8_t *src, size_t len, uint32_t timeout_ms)
{
	const uint32_t start = addr;
	const size_t total = len;
	int ret;

	k_sem_take(&gd32_nvmc_sem, K_FOREVER);

	gd32_nvmc_unlock(regs);

	ret = gd32_nvmc_wait_all_idle(timeout_ms);
	if (ret < 0) {
		goto lock_out;
	}

	if (regs == &gd32_nvmc_ecnvm_regs) {
		NVMC_PERFCTL = (NVMC_PERFCTL & ~NVMC_PERFCTL_ECNVM_PG_SZ) |
			       ECNVM_PROG_SIZE_32BYTES;
	}

	*regs->ctl |= regs->ctl_pg;

	while (len > 0U) {
		gd32_nvmc_word_store(addr, src);

		ret = gd32_nvmc_wait_all_idle(timeout_ms);
		if (ret < 0) {
			break;
		}

		ret = gd32_nvmc_check_status(regs, addr);
		if (ret < 0) {
			break;
		}

		addr += GD32_NVMC_PRG_SIZE;
		src += GD32_NVMC_PRG_SIZE;
		len -= GD32_NVMC_PRG_SIZE;
	}

	__ISB();
	__DSB();

	*regs->ctl &= ~regs->ctl_pg;

lock_out:
	gd32_nvmc_lock(regs);

	gd32_nvmc_cnvm_buf_empty();
	sys_cache_data_invd_range((void *)start, total);

	k_sem_give(&gd32_nvmc_sem);

	return ret;
}

static int gd32_nvmc_page_erase(const struct gd32_nvmc_regs *regs, uint32_t page_addr,
				uint32_t timeout_ms)
{
	int ret;

	gd32_nvmc_unlock(regs);

	ret = gd32_nvmc_wait_all_idle(timeout_ms);
	if (ret < 0) {
		goto lock_out;
	}

	*regs->ctl |= regs->ctl_erase;
	*regs->addr = page_addr;
	*regs->ctl |= regs->ctl_start;

	ret = gd32_nvmc_wait_all_idle(timeout_ms);
	if (ret == 0) {
		ret = gd32_nvmc_check_status(regs, page_addr);
	}

	*regs->ctl &= ~regs->ctl_erase;

lock_out:
	gd32_nvmc_lock(regs);

	return ret;
}

static int gd32_nvmc_erase(const struct gd32_nvmc_regs *regs, uint32_t page_addr,
			   size_t size, uint32_t page_size, uint32_t timeout_ms)
{
	const uint32_t start = page_addr;
	const size_t total = size;
	int ret = 0;

	k_sem_take(&gd32_nvmc_sem, K_FOREVER);

	while (size > 0U) {
		ret = gd32_nvmc_page_erase(regs, page_addr, timeout_ms);
		if (ret < 0) {
			break;
		}

		size -= page_size;
		page_addr += page_size;
	}

	gd32_nvmc_cnvm_buf_empty();
	sys_cache_data_invd_range((void *)start, total);

	k_sem_give(&gd32_nvmc_sem);

	return ret;
}

bool flash_gd32_valid_range(off_t offset, uint32_t len, bool write)
{
	return gd32_nvmc_range_ok(offset, len, SOC_NV_FLASH_SIZE,
				  write ? GD32_NVMC_PRG_SIZE : GD32_NVMC_CNVM_PAGE_SIZE);
}

int flash_gd32_write_range(off_t offset, const void *data, size_t len)
{
	return gd32_nvmc_program(&gd32_nvmc_cnvm_regs, SOC_NV_FLASH_ADDR + offset, data, len,
				 GD32_NVMC_CNVM_TIMEOUT);
}

int flash_gd32_erase_block(off_t offset, size_t size)
{
	return gd32_nvmc_erase(&gd32_nvmc_cnvm_regs, SOC_NV_FLASH_ADDR + offset, size,
			       GD32_NVMC_CNVM_PAGE_SIZE, GD32_NVMC_CNVM_TIMEOUT);
}

#ifdef CONFIG_FLASH_PAGE_LAYOUT
void flash_gd32_pages_layout(const struct device *dev,
			     const struct flash_pages_layout **layout,
			     size_t *layout_size)
{
	ARG_UNUSED(dev);

	*layout = gd32_nvmc_cnvm_layout;
	*layout_size = ARRAY_SIZE(gd32_nvmc_cnvm_layout);
}
#endif /* CONFIG_FLASH_PAGE_LAYOUT */

/* ECNVM at 0x08200000, registered as its own flash device. */
struct gd32_nvmc_ecnvm_config {
	uint32_t addr;
	uint32_t size;
	uint32_t page_size;
	uint32_t timeout_ms;
	struct flash_parameters parameters;
#ifdef CONFIG_FLASH_PAGE_LAYOUT
	struct flash_pages_layout layout;
#endif
};

static int gd32_nvmc_ecnvm_read(const struct device *dev, off_t offset, void *data, size_t len)
{
	const struct gd32_nvmc_ecnvm_config *cfg = dev->config;

	if (!gd32_nvmc_range_ok(offset, len, cfg->size, 1U)) {
		return -EINVAL;
	}

	if (len == 0U) {
		return 0;
	}

	/* Reading the ECNVM while it is being programmed or erased must be avoided. */
	k_sem_take(&gd32_nvmc_sem, K_FOREVER);
	memcpy(data, (uint8_t *)cfg->addr + offset, len);
	k_sem_give(&gd32_nvmc_sem);

	return 0;
}

static int gd32_nvmc_ecnvm_write(const struct device *dev, off_t offset, const void *data,
				 size_t len)
{
	const struct gd32_nvmc_ecnvm_config *cfg = dev->config;

	if (!gd32_nvmc_range_ok(offset, len, cfg->size, GD32_NVMC_PRG_SIZE)) {
		return -EINVAL;
	}

	if (len == 0U) {
		return 0;
	}

	return gd32_nvmc_program(&gd32_nvmc_ecnvm_regs, cfg->addr + offset, data, len,
				 cfg->timeout_ms);
}

static int gd32_nvmc_ecnvm_erase(const struct device *dev, off_t offset, size_t size)
{
	const struct gd32_nvmc_ecnvm_config *cfg = dev->config;

	if (size == 0U) {
		return 0;
	}

	if (!gd32_nvmc_range_ok(offset, size, cfg->size, cfg->page_size)) {
		return -EINVAL;
	}

	return gd32_nvmc_erase(&gd32_nvmc_ecnvm_regs, cfg->addr + offset, size, cfg->page_size,
			       cfg->timeout_ms);
}

static const struct flash_parameters *gd32_nvmc_ecnvm_get_parameters(const struct device *dev)
{
	const struct gd32_nvmc_ecnvm_config *cfg = dev->config;

	return &cfg->parameters;
}

static int gd32_nvmc_ecnvm_get_size(const struct device *dev, uint64_t *size)
{
	const struct gd32_nvmc_ecnvm_config *cfg = dev->config;

	*size = cfg->size;

	return 0;
}

#ifdef CONFIG_FLASH_PAGE_LAYOUT
static void gd32_nvmc_ecnvm_pages_layout(const struct device *dev,
					 const struct flash_pages_layout **layout,
					 size_t *layout_size)
{
	const struct gd32_nvmc_ecnvm_config *cfg = dev->config;

	*layout = &cfg->layout;
	*layout_size = 1U;
}
#endif /* CONFIG_FLASH_PAGE_LAYOUT */

static DEVICE_API(flash, gd32_nvmc_ecnvm_api) = {
	.read = gd32_nvmc_ecnvm_read,
	.write = gd32_nvmc_ecnvm_write,
	.erase = gd32_nvmc_ecnvm_erase,
	.get_parameters = gd32_nvmc_ecnvm_get_parameters,
	.get_size = gd32_nvmc_ecnvm_get_size,
#ifdef CONFIG_FLASH_PAGE_LAYOUT
	.page_layout = gd32_nvmc_ecnvm_pages_layout,
#endif
};

#define GD32_NVMC_ECNVM_DEFINE(node_id)							\
	BUILD_ASSERT(DT_PROP(node_id, write_block_size) == GD32_NVMC_PRG_SIZE,		\
		     "GD32H77x/78x ECNVM requires write-block-size = <32>");		\
	BUILD_ASSERT(DT_REG_ADDR(node_id) == MAIN_ECNVM_BASE_ADDRESS,			\
		     "GD32H77x/78x ECNVM must start at 0x08200000");			\
	static const struct gd32_nvmc_ecnvm_config gd32_nvmc_ecnvm_config_##node_id = {	\
		.addr = DT_REG_ADDR(node_id),						\
		.size = DT_REG_SIZE(node_id),						\
		.page_size = DT_PROP(node_id, page_size),				\
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
	DEVICE_DT_DEFINE(node_id, NULL, NULL, NULL, &gd32_nvmc_ecnvm_config_##node_id,	\
			 POST_KERNEL, CONFIG_FLASH_INIT_PRIORITY, &gd32_nvmc_ecnvm_api);

/* The ECNVM is the flash child that does not carry "soc-nv-flash". */
#define GD32_NVMC_ECNVM_DEFINE_IF_EX(node_id)						\
	COND_CODE_1(DT_NODE_HAS_COMPAT(node_id, soc_nv_flash), (),			\
		    (GD32_NVMC_ECNVM_DEFINE(node_id)))

DT_FOREACH_CHILD_STATUS_OKAY(GD32_NVMC_CTRL_NODE, GD32_NVMC_ECNVM_DEFINE_IF_EX)
