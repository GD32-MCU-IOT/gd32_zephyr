/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT gd_gd32_ospi_nor

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/gd32.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/dt-bindings/flash_controller/ospi.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(flash_gd32_ospi, CONFIG_FLASH_LOG_LEVEL);

#if defined(CONFIG_FLASH_GD32_OSPI_MEMORY_MAPPED)
#include <zephyr/drivers/flash/gd32_ospi_flash_api_extensions.h>
#if defined(CONFIG_DCACHE)
#include <zephyr/cache.h>
#endif
#endif

#include "spi_nor_gd25x512me.h"

#include <gd32h7xx_ospi.h>
#include <gd32h7xx_ospim.h>
#include <gd32h7xx_rcu.h>

#define GD32_OSPI_FLASH_NODE DT_DRV_INST(0)
#define GD32_OSPI_CTRL_NODE  DT_INST_PARENT(0)

#define GD32_OSPI_HAS_RESET      DT_NODE_HAS_PROP(GD32_OSPI_CTRL_NODE, resets)
#define GD32_OSPI_HAS_RESET_GPIO DT_INST_NODE_HAS_PROP(0, reset_gpios)

#define GD32_OSPI_PAGE_SIZE  SPI_NOR_PAGE_SIZE
#define GD32_OSPI_ERASE_SIZE SPI_NOR_SECTOR_SIZE
#define GD32_OSPI_BLOCK_SIZE SPI_NOR_BLOCK_SIZE

#define GD32_OSPI_STATUS_TIMEOUT_US (2U * USEC_PER_SEC)
#define GD32_OSPI_RESET_WAIT_US     (100U * USEC_PER_MSEC)

enum gd32_ospi_writeoc_mode {
	GD32_OSPI_WRITEOC_AUTO = 0,
	GD32_OSPI_WRITEOC_PP,
	GD32_OSPI_WRITEOC_PP_1_1_2,
	GD32_OSPI_WRITEOC_PP_1_1_4,
	GD32_OSPI_WRITEOC_PP_1_4_4,
};

#define GD32_OSPI_WRITEOC_FROM_DT(inst)                                                            \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, writeoc), \
		(GD32_OSPI_WRITEOC_##DT_INST_STRING_UPPER_TOKEN(inst, writeoc)), \
		(GD32_OSPI_WRITEOC_AUTO))

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) <= 1,
	     "flash_gd32_ospi.c currently supports one gd,gd32-ospi-nor instance");

struct flash_gd32_ospi_config {
	uint32_t periph;
	uint32_t flash_size;
	uint32_t max_frequency;
	uint8_t bus_width;
	uint8_t data_rate;
	uint8_t writeoc;
	bool use_4b_opcodes;
	uint16_t clkid_ospix;
	uint16_t clkid_ospi_ker;
	const struct device *clock;
	const struct pinctrl_dev_config *pcfg;
	uintptr_t mmap_base;
	uint32_t mmap_size;
#if GD32_OSPI_HAS_RESET
	struct reset_dt_spec reset;
#endif
#if GD32_OSPI_HAS_RESET_GPIO
	struct gpio_dt_spec reset_gpio;
	uint32_t reset_pulse_ms;
#endif
};

struct flash_gd32_ospi_data {
	struct k_sem lock;
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	struct flash_pages_layout layout;
#endif
	ospi_parameter_struct ospi;
	bool opi_enabled;
	uint8_t address_width;
	uint32_t read_opcode;
	uint32_t program_opcode;
	bool mmap_enabled;
};

static inline void gd32_ospi_lock(const struct device *dev)
{
	struct flash_gd32_ospi_data *data = dev->data;

	k_sem_take(&data->lock, K_FOREVER);
}

static inline void gd32_ospi_unlock(const struct device *dev)
{
	struct flash_gd32_ospi_data *data = dev->data;

	k_sem_give(&data->lock);
}

static bool gd32_ospi_address_is_valid(const struct device *dev, off_t addr, size_t len)
{
	const struct flash_gd32_ospi_config *cfg = dev->config;

	return (addr >= 0) && ((uint64_t)addr + (uint64_t)len <= cfg->flash_size);
}

static bool gd32_ospi_opi_active(const struct flash_gd32_ospi_config *cfg,
				 const struct flash_gd32_ospi_data *data)
{
	return (cfg->bus_width == OSPI_OPI_MODE) && data->opi_enabled;
}

static uint32_t gd32_ospi_instr_mode(const struct flash_gd32_ospi_config *cfg,
				     const struct flash_gd32_ospi_data *data)
{
	/*
	 * In Octal STR mode every opcode (including RDSR/WREN/erase) is driven
	 * on all 8 lines, matching vendor gd25x512me.c.
	 */
	if (gd32_ospi_opi_active(cfg, data)) {
		return OSPI_INSTRUCTION_8_LINES;
	}

	return OSPI_INSTRUCTION_1_LINE;
}

static uint32_t gd32_ospi_addr_mode(const struct flash_gd32_ospi_config *cfg,
				    const struct flash_gd32_ospi_data *data)
{
	if (gd32_ospi_opi_active(cfg, data)) {
		return OSPI_ADDRESS_8_LINES;
	}

	if (cfg->bus_width == OSPI_QUAD_MODE) {
		return OSPI_ADDRESS_1_LINE;
	}

	if (cfg->bus_width == OSPI_DUAL_MODE) {
		return OSPI_ADDRESS_1_LINE;
	}

	return OSPI_ADDRESS_1_LINE;
}

static uint32_t gd32_ospi_data_mode(const struct flash_gd32_ospi_config *cfg,
				    const struct flash_gd32_ospi_data *data)
{
	switch (cfg->bus_width) {
	case OSPI_OPI_MODE:
		return gd32_ospi_opi_active(cfg, data) ? OSPI_DATA_8_LINES : OSPI_DATA_1_LINE;
	case OSPI_QUAD_MODE:
		return OSPI_DATA_4_LINES;
	case OSPI_DUAL_MODE:
		return OSPI_DATA_2_LINES;
	default:
		return OSPI_DATA_1_LINE;
	}
}

static uint32_t gd32_ospi_ctrl_addr_mode(const struct flash_gd32_ospi_config *cfg,
					 const struct flash_gd32_ospi_data *data)
{
	/*
	 * Once in Octal STR mode, control-path commands (e.g. sector/block
	 * erase) must drive their address phase on all 8 lines too, matching
	 * vendor gd25x512me.c ospi_flash_block_erase().
	 */
	if (gd32_ospi_opi_active(cfg, data)) {
		return OSPI_ADDRESS_8_LINES;
	}

	return OSPI_ADDRESS_1_LINE;
}

static uint32_t gd32_ospi_addr_size(uint8_t width)
{
	return (width == 4U) ? OSPI_ADDRESS_32_BITS : OSPI_ADDRESS_24_BITS;
}

static uint32_t gd32_ospi_device_size(uint32_t flash_size_bytes)
{
	uint32_t bits = 0U;
	uint32_t size = flash_size_bytes;

	while (size > 1U) {
		size >>= 1U;
		bits++;
	}

	if (bits == 0U) {
		return OSPI_MESZ_2_BYTES;
	}

	return OSPI_MESZ(bits - 1U);
}

static uint32_t gd32_ospi_read_opcode(const struct flash_gd32_ospi_config *cfg,
				      uint8_t address_width)
{
	bool use_4b = cfg->use_4b_opcodes && (address_width == 4U);

	if (cfg->bus_width == OSPI_OPI_MODE) {
		/*
		 * In OPI STR mode, large devices should use dedicated 4-byte OPI
		 * opcodes directly instead of relying on global 4-byte address mode.
		 */
		use_4b = (address_width == 4U);
		return use_4b ? GD25X512ME_4_BYTE_ADDR_OCTAL_IO_FAST_READ_CMD
			      : GD25X512ME_OCTAL_IO_FAST_READ_CMD;
	}

	if (cfg->bus_width == OSPI_QUAD_MODE) {
		return use_4b ? SPI_NOR_CMD_QREAD_4B : SPI_NOR_CMD_QREAD;
	}

	if (cfg->bus_width == OSPI_DUAL_MODE) {
		return use_4b ? SPI_NOR_CMD_DREAD_4B : SPI_NOR_CMD_DREAD;
	}

	return use_4b ? SPI_NOR_CMD_READ_FAST_4B : SPI_NOR_CMD_READ_FAST;
}

static uint32_t gd32_ospi_program_opcode(const struct flash_gd32_ospi_config *cfg,
					 uint8_t address_width)
{
	bool use_4b = cfg->use_4b_opcodes && (address_width == 4U);

	if (cfg->bus_width == OSPI_OPI_MODE) {
		/* See read path: prefer native 4-byte OPI opcodes for >16MB parts. */
		use_4b = (address_width == 4U);
		return use_4b ? GD25X512ME_4_BYTE_EXT_OCTAL_PAGE_PROG_CMD
			      : GD25X512ME_EXT_OCTAL_PAGE_PROG_CMD;
	}

	if (cfg->writeoc != GD32_OSPI_WRITEOC_AUTO) {
		switch (cfg->writeoc) {
		case GD32_OSPI_WRITEOC_PP:
			return use_4b ? SPI_NOR_CMD_PP_4B : SPI_NOR_CMD_PP;
		case GD32_OSPI_WRITEOC_PP_1_1_2:
			/* No dedicated 1-1-2 4-byte PP command for GD25X512ME. */
			return SPI_NOR_CMD_PP_1_1_2;
		case GD32_OSPI_WRITEOC_PP_1_1_4:
			return use_4b ? SPI_NOR_CMD_PP_1_1_4_4B : SPI_NOR_CMD_PP_1_1_4;
		case GD32_OSPI_WRITEOC_PP_1_4_4:
			return use_4b ? SPI_NOR_CMD_PP_1_4_4_4B : SPI_NOR_CMD_PP_1_4_4;
		default:
			break;
		}
	}

	if (cfg->bus_width == OSPI_QUAD_MODE) {
		return use_4b ? SPI_NOR_CMD_PP_1_1_4_4B : SPI_NOR_CMD_PP_1_1_4;
	}

	if (cfg->bus_width == OSPI_DUAL_MODE) {
		/* GD25X512ME command set has no dedicated 1-1-2 4-byte PP opcode. */
		return SPI_NOR_CMD_PP_1_1_2;
	}

	return use_4b ? SPI_NOR_CMD_PP_4B : SPI_NOR_CMD_PP;
}

static uint8_t gd32_ospi_read_dummy_cycles(const struct flash_gd32_ospi_config *cfg)
{
	if (cfg->bus_width == OSPI_OPI_MODE) {
		return 16U;
	}

	return SPI_NOR_DUMMY_RD;
}

static void gd32_ospi_fill_cmd(const struct flash_gd32_ospi_config *cfg,
			       struct flash_gd32_ospi_data *data, ospi_regular_cmd_struct *cmd,
			       uint32_t instruction, uint32_t address, uint32_t addr_mode,
			       uint32_t data_mode, uint32_t nbdata, uint8_t dummy_cycles)
{
	(void)cfg;

	memset(cmd, 0, sizeof(*cmd));
	cmd->operation_type = OSPI_OPTYPE_COMMON_CFG;
	cmd->instruction = instruction;
	cmd->ins_mode = gd32_ospi_instr_mode(cfg, data);
	cmd->ins_size = OSPI_INSTRUCTION_8_BITS;
	cmd->address = address;
	cmd->addr_mode = addr_mode;
	cmd->addr_size = gd32_ospi_addr_size(data->address_width);
	cmd->addr_dtr_mode = OSPI_ADDRDTR_MODE_DISABLE;
	cmd->alter_bytes_mode = OSPI_ALTERNATE_BYTES_NONE;
	cmd->alter_bytes_size = OSPI_ALTERNATE_BYTES_8_BITS;
	cmd->alter_bytes_dtr_mode = OSPI_ABDTR_MODE_DISABLE;
	cmd->data_mode = data_mode;
	cmd->nbdata = nbdata;
	cmd->data_dtr_mode = OSPI_DADTR_MODE_DISABLE;
	cmd->dummy_cycles = OSPI_DUMYC(dummy_cycles);
}

static int gd32_ospi_read_status(const struct device *dev, uint8_t *status)
{
	const struct flash_gd32_ospi_config *cfg = dev->config;
	struct flash_gd32_ospi_data *data = dev->data;
	ospi_regular_cmd_struct cmd;
	/*
	 * In Octal STR mode RDSR uses an 8-line data phase with a fixed 8 dummy
	 * cycles regardless of CFG_REG1, matching vendor ospi_read_status_register().
	 */
	bool opi_active = gd32_ospi_opi_active(cfg, data);
	uint32_t data_mode = opi_active ? OSPI_DATA_8_LINES : OSPI_DATA_1_LINE;
	uint8_t dummy_cycles = opi_active ? 8U : 0U;

	gd32_ospi_fill_cmd(cfg, data, &cmd, SPI_NOR_CMD_RDSR, 0U, OSPI_ADDRESS_NONE, data_mode, 1U,
			   dummy_cycles);

	ospi_command_config(cfg->periph, &data->ospi, &cmd);
	ospi_receive(cfg->periph, status);

	return 0;
}

static int gd32_ospi_wait_ready(const struct device *dev)
{
	struct flash_gd32_ospi_data *data = dev->data;

	uint32_t timeout = k_cycle_get_32() + k_us_to_cyc_ceil32(GD32_OSPI_STATUS_TIMEOUT_US);

	while (true) {
		uint8_t sr = 0U;

		(void)gd32_ospi_read_status(dev, &sr);
		if ((sr & SPI_NOR_WIP_BIT) == 0U) {
			return 0;
		}

		if ((int32_t)(k_cycle_get_32() - timeout) > 0) {
			LOG_ERR("wait_ready timeout (opi_enabled=%d, sr=0x%02x)", data->opi_enabled,
				sr);
			return -ETIMEDOUT;
		}
		k_sleep(K_MSEC(2));
	}
}

static int gd32_ospi_write_enable(const struct device *dev)
{
	const struct flash_gd32_ospi_config *cfg = dev->config;
	struct flash_gd32_ospi_data *data = dev->data;
	ospi_regular_cmd_struct cmd;
	uint32_t timeout;

	gd32_ospi_fill_cmd(cfg, data, &cmd, SPI_NOR_CMD_WREN, 0U, OSPI_ADDRESS_NONE, OSPI_DATA_NONE,
			   0U, 0U);
	ospi_command_config(cfg->periph, &data->ospi, &cmd);

	timeout = k_cycle_get_32() + k_us_to_cyc_ceil32(GD32_OSPI_STATUS_TIMEOUT_US);
	while (true) {
		uint8_t sr = 0U;

		(void)gd32_ospi_read_status(dev, &sr);
		if ((sr & SPI_NOR_WEL_BIT) != 0U) {
			return 0;
		}

		if ((int32_t)(k_cycle_get_32() - timeout) > 0) {
			LOG_ERR("write_enable timeout (opi_enabled=%d, sr=0x%02x)",
				data->opi_enabled, sr);
			return -ETIMEDOUT;
		}
	}
}

static int gd32_ospi_send_simple_cmd(const struct device *dev, uint32_t opcode)
{
	const struct flash_gd32_ospi_config *cfg = dev->config;
	struct flash_gd32_ospi_data *data = dev->data;
	ospi_regular_cmd_struct cmd;

	gd32_ospi_fill_cmd(cfg, data, &cmd, opcode, 0U, OSPI_ADDRESS_NONE, OSPI_DATA_NONE, 0U, 0U);
	ospi_command_config(cfg->periph, &data->ospi, &cmd);

	return 0;
}

static int gd32_ospi_write_volatile_cfg(const struct device *dev, uint32_t reg_addr, uint8_t value)
{
	const struct flash_gd32_ospi_config *cfg = dev->config;
	struct flash_gd32_ospi_data *data = dev->data;
	ospi_regular_cmd_struct cmd;
	int ret;
	bool switch_to_opi = false;

	gd32_ospi_fill_cmd(cfg, data, &cmd, GD25X512ME_WRITE_ENABLE_VOLATILE_STATUS_CFG_CMD, 0U,
			   OSPI_ADDRESS_NONE, OSPI_DATA_NONE, 0U, 0U);
	ospi_command_config(cfg->periph, &data->ospi, &cmd);

	gd32_ospi_fill_cmd(cfg, data, &cmd, GD25X512ME_WRITE_VOLATILE_CFG_REG_CMD, reg_addr,
			   gd32_ospi_addr_mode(cfg, data), gd32_ospi_data_mode(cfg, data), 1U, 0U);
	ospi_command_config(cfg->periph, &data->ospi, &cmd);
	ospi_transmit(cfg->periph, &value);

	/*
	 * When CFG0 enters octal STR mode, the flash immediately changes bus
	 * protocol. Poll status in OPI mode from this point onward.
	 */
	if ((cfg->bus_width == OSPI_OPI_MODE) && (reg_addr == GD25X512ME_CFG_REG0_ADDR) &&
	    ((value == GD25X512ME_CFG_OCTAL_STR_WO) || (value == GD25X512ME_CFG_OCTAL_STR))) {
		switch_to_opi = true;
		data->opi_enabled = true;

		/*
		 * Sanity-check the mode switch: read JEDEC ID (0x9F) in 8-line
		 * framing; GD parts return 0xC8 first, per vendor ospi_flash_read_id().
		 */
		{
			ospi_regular_cmd_struct id_cmd;
			uint8_t id_bytes[4] = {0U, 0U, 0U, 0U};

			gd32_ospi_fill_cmd(cfg, data, &id_cmd, 0x9FU, 0U, OSPI_ADDRESS_NONE,
					   OSPI_DATA_8_LINES, 4U, 8U);
			ospi_command_config(cfg->periph, &data->ospi, &id_cmd);
			ospi_receive(cfg->periph, id_bytes);
			LOG_DBG("[mode-switch] octal READ ID (0x9F) after entering OPI mode: %02x "
				"%02x %02x %02x\n",
				id_bytes[0], id_bytes[1], id_bytes[2], id_bytes[3]);
		}
	}

	ret = gd32_ospi_wait_ready(dev);

	if ((ret != 0) && switch_to_opi) {
		/* Roll back driver-side mode if transition did not complete. */
		data->opi_enabled = false;
	}

	return ret;
}

static int gd32_ospi_configure_opi_str_mode(const struct device *dev)
{
	int ret;

	/*
	 * Vendor flow issues a standard 0x06 WREN immediately before every
	 * volatile-cfg register write; replicate the proven sequence.
	 */
	ret = gd32_ospi_send_simple_cmd(dev, SPI_NOR_CMD_WREN);
	if (ret != 0) {
		return ret;
	}

	/* Match validated vendor flow: set dummy first, then switch to octal STR W/O opcode
	 * extension.
	 */
	ret = gd32_ospi_write_volatile_cfg(dev, GD25X512ME_CFG_REG1_ADDR,
					   GD25X512ME_CFG_16_DUMMY_CYCLES);
	if (ret != 0) {
		return ret;
	}

	ret = gd32_ospi_send_simple_cmd(dev, SPI_NOR_CMD_WREN);
	if (ret != 0) {
		return ret;
	}

	ret = gd32_ospi_write_volatile_cfg(dev, GD25X512ME_CFG_REG0_ADDR,
					   GD25X512ME_CFG_OCTAL_STR_WO);

	return ret;
}

#if defined(CONFIG_FLASH_GD32_OSPI_MEMORY_MAPPED)
/*
 * Memory-mapped setup mirrors vendor gd25x512me.c: pre-load read/program
 * opcodes into the READ_CFG/WRITE_CFG slots, then switch the peripheral to
 * OSPI_MEMORY_MAPPED mode so the CPU reads flash via its address window.
 */
static int gd32_ospi_mmap_wait_idle(const struct device *dev)
{
	const struct flash_gd32_ospi_config *cfg = dev->config;
	uint32_t timeout = k_cycle_get_32() + k_us_to_cyc_ceil32(GD32_OSPI_STATUS_TIMEOUT_US);

	while (ospi_flag_get(cfg->periph, OSPI_FLAG_BUSY) != RESET) {
		if ((int32_t)(k_cycle_get_32() - timeout) > 0) {
			LOG_ERR("memory-mapped mode: busy flag timeout");
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static int gd32_ospi_mmap_enable(const struct device *dev)
{
	const struct flash_gd32_ospi_config *cfg = dev->config;
	struct flash_gd32_ospi_data *data = dev->data;
	ospi_regular_cmd_struct cmd;
	int ret;

	if (data->mmap_enabled) {
		return 0;
	}

	/* Pre-configure the read command used while in memory-mapped mode. */
	gd32_ospi_fill_cmd(cfg, data, &cmd, data->read_opcode, 0U, gd32_ospi_addr_mode(cfg, data),
			   gd32_ospi_data_mode(cfg, data), 0U, gd32_ospi_read_dummy_cycles(cfg));
	cmd.operation_type = OSPI_OPTYPE_READ_CFG;
	ospi_command_config(cfg->periph, &data->ospi, &cmd);

	/* Pre-configure the program command used while in memory-mapped mode. */
	gd32_ospi_fill_cmd(cfg, data, &cmd, data->program_opcode, 0U,
			   gd32_ospi_addr_mode(cfg, data), gd32_ospi_data_mode(cfg, data), 0U, 0U);
	cmd.operation_type = OSPI_OPTYPE_WRITE_CFG;
	ospi_command_config(cfg->periph, &data->ospi, &cmd);

	ret = gd32_ospi_mmap_wait_idle(dev);
	if (ret != 0) {
		return ret;
	}

	ospi_functional_mode_config(cfg->periph, OSPI_MEMORY_MAPPED);
	data->mmap_enabled = true;

	return 0;
}

static void gd32_ospi_mmap_disable(const struct device *dev)
{
	const struct flash_gd32_ospi_config *cfg = dev->config;
	struct flash_gd32_ospi_data *data = dev->data;

	if (!data->mmap_enabled) {
		return;
	}

	/* Restore the resting indirect-write mode used by all other commands. */
	ospi_functional_mode_config(cfg->periph, OSPI_INDIRECT_WRITE);
	data->mmap_enabled = false;
}

int flash_gd32_ospi_memory_mapped_enable(const struct device *dev)
{
	int ret;

	gd32_ospi_lock(dev);
	ret = gd32_ospi_mmap_enable(dev);
	gd32_ospi_unlock(dev);

	return ret;
}
#endif /* CONFIG_FLASH_GD32_OSPI_MEMORY_MAPPED */

static int flash_gd32_ospi_read(const struct device *dev, off_t addr, void *buf, size_t len)
{
	const struct flash_gd32_ospi_config *cfg = dev->config;
	struct flash_gd32_ospi_data *data = dev->data;
	ospi_regular_cmd_struct cmd;

	if ((len == 0U) || (buf == NULL)) {
		return 0;
	}

	if (!gd32_ospi_address_is_valid(dev, addr, len)) {
		return -EINVAL;
	}

	gd32_ospi_lock(dev);

#if defined(CONFIG_FLASH_GD32_OSPI_MEMORY_MAPPED)
	/*
	 * The mmap fast path is taken only after an explicit
	 * flash_gd32_ospi_memory_mapped_enable(); other reads (e.g. post-erase/
	 * post-write verify reads) use the indirect-mode sequence below.
	 */
	if (data->mmap_enabled) {
		uintptr_t mmap_addr = cfg->mmap_base + (uint32_t)addr;

		LOG_DBG("[read] memory-mapped mode: addr=0x%lx len=%zu, mmap_addr=0x%08lx\n",
			(unsigned long)addr, len, (unsigned long)mmap_addr);

#if defined(CONFIG_DCACHE)
		sys_cache_data_invd_range((void *)mmap_addr, len);
#endif
		memcpy(buf, (const void *)mmap_addr, len);

		gd32_ospi_unlock(dev);

		return 0;
	}
#endif /* CONFIG_FLASH_GD32_OSPI_MEMORY_MAPPED */

	LOG_DBG("[read] indirect mode: addr=0x%lx len=%zu\n", (unsigned long)addr, len);

	gd32_ospi_fill_cmd(cfg, data, &cmd, data->read_opcode, (uint32_t)addr,
			   gd32_ospi_addr_mode(cfg, data), gd32_ospi_data_mode(cfg, data), len,
			   gd32_ospi_read_dummy_cycles(cfg));

	ospi_command_config(cfg->periph, &data->ospi, &cmd);
	ospi_receive(cfg->periph, buf);

	gd32_ospi_unlock(dev);

	return 0;
}

static int flash_gd32_ospi_write(const struct device *dev, off_t addr, const void *buf, size_t len)
{
	const struct flash_gd32_ospi_config *cfg = dev->config;
	struct flash_gd32_ospi_data *data = dev->data;
	ospi_regular_cmd_struct cmd;
	const uint8_t *src = buf;
	int ret;

	if ((len == 0U) || (buf == NULL)) {
		return 0;
	}

	if (!gd32_ospi_address_is_valid(dev, addr, len)) {
		return -EINVAL;
	}

	gd32_ospi_lock(dev);

#if defined(CONFIG_FLASH_GD32_OSPI_MEMORY_MAPPED)
	gd32_ospi_mmap_disable(dev);
#endif

	LOG_DBG("[write] indirect mode: addr=0x%lx len=%zu\n", (unsigned long)addr, len);

	ret = gd32_ospi_wait_ready(dev);
	if (ret != 0) {
		gd32_ospi_unlock(dev);
		return ret;
	}

	while (len > 0U) {
		size_t to_write = MIN(
			(size_t)GD32_OSPI_PAGE_SIZE - ((size_t)addr % GD32_OSPI_PAGE_SIZE), len);

		ret = gd32_ospi_write_enable(dev);
		if (ret != 0) {
			break;
		}

		gd32_ospi_fill_cmd(cfg, data, &cmd, data->program_opcode, (uint32_t)addr,
				   gd32_ospi_addr_mode(cfg, data), gd32_ospi_data_mode(cfg, data),
				   to_write, 0U);

		ospi_command_config(cfg->periph, &data->ospi, &cmd);
		ospi_transmit(cfg->periph, (uint8_t *)src);

		ret = gd32_ospi_wait_ready(dev);
		if (ret != 0) {
			break;
		}

		addr += to_write;
		src += to_write;
		len -= to_write;
	}

	gd32_ospi_unlock(dev);

	return ret;
}

static int flash_gd32_ospi_erase(const struct device *dev, off_t addr, size_t len)
{
	const struct flash_gd32_ospi_config *cfg = dev->config;
	struct flash_gd32_ospi_data *data = dev->data;
	ospi_regular_cmd_struct cmd;
	int ret;

	if (len == 0U) {
		return 0;
	}

	if (!gd32_ospi_address_is_valid(dev, addr, len)) {
		return -EINVAL;
	}

	if ((addr == 0) && (len >= cfg->flash_size)) {
		len = cfg->flash_size;
	} else if (((addr % GD32_OSPI_ERASE_SIZE) != 0U) || ((len % GD32_OSPI_ERASE_SIZE) != 0U)) {
		return -EINVAL;
	}

	gd32_ospi_lock(dev);

#if defined(CONFIG_FLASH_GD32_OSPI_MEMORY_MAPPED)
	gd32_ospi_mmap_disable(dev);
#endif

	ret = gd32_ospi_wait_ready(dev);
	if (ret != 0) {
		gd32_ospi_unlock(dev);
		return ret;
	}

	if ((addr == 0) && (len == cfg->flash_size)) {
		ret = gd32_ospi_write_enable(dev);
		if (ret == 0) {
			ret = gd32_ospi_send_simple_cmd(dev, SPI_NOR_CMD_BULKE);
		}
		if (ret == 0) {
			ret = gd32_ospi_wait_ready(dev);
		}
		gd32_ospi_unlock(dev);
		return ret;
	}

	while ((len > 0U) && (ret == 0)) {
		uint32_t opcode;
		uint32_t erase_addr_mode;
		size_t erase_size;
		bool use_4b = cfg->use_4b_opcodes && (data->address_width == 4U);

		if (cfg->bus_width == OSPI_OPI_MODE) {
			/*
			 * OPI path does not enter global 4-byte mode via 0xB7. Use
			 * dedicated 4-byte erase opcodes for >16MB addressing.
			 */
			use_4b = (data->address_width == 4U);
		}

		if (((addr % GD32_OSPI_BLOCK_SIZE) == 0U) && (len >= GD32_OSPI_BLOCK_SIZE)) {
			opcode = use_4b ? SPI_NOR_CMD_BE_4B : SPI_NOR_CMD_BE;
			erase_size = GD32_OSPI_BLOCK_SIZE;
		} else {
			opcode = use_4b ? SPI_NOR_CMD_SE_4B : SPI_NOR_CMD_SE;
			erase_size = GD32_OSPI_ERASE_SIZE;
		}

		erase_addr_mode = gd32_ospi_ctrl_addr_mode(cfg, data);

		ret = gd32_ospi_write_enable(dev);
		if (ret != 0) {
			break;
		}

		gd32_ospi_fill_cmd(cfg, data, &cmd, opcode, (uint32_t)addr, erase_addr_mode,
				   OSPI_DATA_NONE, 0U, 0U);
		ospi_command_config(cfg->periph, &data->ospi, &cmd);

		ret = gd32_ospi_wait_ready(dev);
		if (ret != 0) {
			break;
		}

		addr += erase_size;
		len -= erase_size;
	}

	gd32_ospi_unlock(dev);

	return ret;
}

static const struct flash_parameters flash_gd32_ospi_params = {
	.write_block_size = 1,
	.erase_value = 0xFF,
};

static const struct flash_parameters *flash_gd32_ospi_get_parameters(const struct device *dev)
{
	ARG_UNUSED(dev);

	return &flash_gd32_ospi_params;
}

static int flash_gd32_ospi_get_size(const struct device *dev, uint64_t *size)
{
	const struct flash_gd32_ospi_config *cfg = dev->config;

	*size = cfg->flash_size;

	return 0;
}

#if defined(CONFIG_FLASH_PAGE_LAYOUT)
static void flash_gd32_ospi_pages_layout(const struct device *dev,
					 const struct flash_pages_layout **layout,
					 size_t *layout_size)
{
	struct flash_gd32_ospi_data *data = dev->data;

	*layout = &data->layout;
	*layout_size = 1;
}
#endif

static int flash_gd32_ospi_hw_init(const struct device *dev)
{
	const struct flash_gd32_ospi_config *cfg = dev->config;
	struct flash_gd32_ospi_data *data = dev->data;
	uint32_t bus_freq;
	uint32_t divider;

	if (!device_is_ready(cfg->clock)) {
		LOG_ERR("clock device not ready");
		return -ENODEV;
	}

	if (clock_control_on(cfg->clock, (clock_control_subsys_t)&cfg->clkid_ospix) != 0) {
		LOG_ERR("cannot enable ospix clock");
		return -EIO;
	}

	if (clock_control_on(cfg->clock, (clock_control_subsys_t)&cfg->clkid_ospi_ker) != 0) {
		LOG_ERR("cannot enable ospi kernel clock");
		return -EIO;
	}

#if GD32_OSPI_HAS_RESET
	if (!device_is_ready(cfg->reset.dev)) {
		LOG_ERR("reset device not ready");
		return -ENODEV;
	}

	if (reset_line_toggle_dt(&cfg->reset) != 0) {
		LOG_ERR("cannot toggle ospi reset line");
		return -EIO;
	}
#endif

	if (pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT) != 0) {
		LOG_ERR("cannot apply ospi pinctrl");
		return -EIO;
	}

	rcu_periph_clock_enable(RCU_OSPIM);
	ospi_deinit(cfg->periph);
	ospi_disable(cfg->periph);
	ospi_struct_init(&data->ospi);

	if (clock_control_get_rate(cfg->clock, (clock_control_subsys_t)&cfg->clkid_ospi_ker,
				   &bus_freq) != 0) {
		LOG_ERR("cannot get ospi kernel clock rate");
		return -EIO;
	}

	divider = DIV_ROUND_UP(bus_freq, cfg->max_frequency);
	if (divider == 0U) {
		divider = 1U;
	}
	data->ospi.prescaler = CLAMP(divider - 1U, 0U, 255U);
	data->ospi.fifo_threshold = OSPI_FIFO_THRESHOLD_5;
	data->ospi.sample_shift = OSPI_SAMPLE_SHIFTING_NONE;
	data->ospi.device_size = gd32_ospi_device_size(cfg->flash_size);
	data->ospi.cs_hightime = OSPI_CS_HIGH_TIME_3_CYCLE;
	data->ospi.memory_type = OSPI_MICRON_MODE;
	data->ospi.wrap_size = OSPI_DIRECT;
	data->ospi.delay_hold_cycle = OSPI_DELAY_HOLD_NONE;

	ospi_init(cfg->periph, &data->ospi);

	ospi_enable(cfg->periph);

	ospi_functional_mode_config(cfg->periph, OSPI_INDIRECT_READ);

	ospi_interrupt_disable(cfg->periph,
			       OSPI_INT_TERR | OSPI_INT_TC | OSPI_INT_FT | OSPI_INT_SM);

	ospi_flag_clear(cfg->periph, OSPI_FLAG_TERR | OSPI_FLAG_TC | OSPI_FLAG_SM);

	ospi_functional_mode_config(cfg->periph, OSPI_INDIRECT_WRITE);

	/* Configure OSPIM routing on port0 according to selected controller. */
	ospim_deinit();
	ospim_port_sck_config(OSPIM_PORT0, OSPIM_PORT_SCK_ENABLE);
	ospim_port_csn_config(OSPIM_PORT0, OSPIM_PORT_CSN_ENABLE);
	ospim_port_io3_0_config(OSPIM_PORT0, OSPIM_IO_LOW_ENABLE);
	ospim_port_io7_4_config(OSPIM_PORT0, (cfg->bus_width == OSPI_OPI_MODE)
						     ? OSPIM_IO_HIGH_ENABLE
						     : OSPIM_IO_HIGH_DISABLE);

	if (cfg->periph == OSPI0) {
		ospim_port_sck_source_select(OSPIM_PORT0, OSPIM_SCK_SOURCE_OSPI0_SCK);
		ospim_port_csn_source_select(OSPIM_PORT0, OSPIM_CSN_SOURCE_OSPI0_CSN);
		ospim_port_io3_0_source_select(OSPIM_PORT0, OSPIM_SRCPLIO_OSPI0_IO_LOW);
		ospim_port_io7_4_source_select(OSPIM_PORT0, OSPIM_SRCPHIO_OSPI0_IO_HIGH);
	} else {
		ospim_port_sck_source_select(OSPIM_PORT0, OSPIM_SCK_SOURCE_OSPI1_SCK);
		ospim_port_csn_source_select(OSPIM_PORT0, OSPIM_CSN_SOURCE_OSPI1_CSN);
		ospim_port_io3_0_source_select(OSPIM_PORT0, OSPIM_SRCPLIO_OSPI1_IO_LOW);
		ospim_port_io7_4_source_select(OSPIM_PORT0, OSPIM_SRCPHIO_OSPI1_IO_HIGH);
	}

	return 0;
}

static int flash_gd32_ospi_init(const struct device *dev)
{
	const struct flash_gd32_ospi_config *cfg = dev->config;
	struct flash_gd32_ospi_data *data = dev->data;
	int ret;

	data->opi_enabled = false;
	data->address_width = 3U;
	data->mmap_enabled = false;

	if (cfg->data_rate != OSPI_STR_TRANSFER) {
		LOG_ERR("only STR transfer is currently supported");
		return -ENOTSUP;
	}

	if ((cfg->bus_width == OSPI_DUAL_MODE) && cfg->use_4b_opcodes) {
		LOG_ERR("dual mode does not support four-byte-opcodes");
		return -ENOTSUP;
	}

	if ((cfg->bus_width == OSPI_OPI_MODE) && (cfg->writeoc != GD32_OSPI_WRITEOC_AUTO)) {
		LOG_ERR("writeoc is not supported in OPI mode");
		return -ENOTSUP;
	}

	if ((cfg->bus_width == OSPI_DUAL_MODE) && (cfg->writeoc == GD32_OSPI_WRITEOC_PP_1_1_4 ||
						   cfg->writeoc == GD32_OSPI_WRITEOC_PP_1_4_4)) {
		LOG_ERR("writeoc does not match dual mode");
		return -ENOTSUP;
	}

	if ((cfg->bus_width != OSPI_SPI_MODE) && (cfg->bus_width != OSPI_DUAL_MODE) &&
	    (cfg->bus_width != OSPI_QUAD_MODE) && (cfg->bus_width != OSPI_OPI_MODE)) {
		LOG_ERR("unsupported spi-bus-width: %u", cfg->bus_width);
		return -ENOTSUP;
	}

	k_sem_init(&data->lock, 1, 1);

	ret = flash_gd32_ospi_hw_init(dev);
	if (ret != 0) {
		return ret;
	}

#if GD32_OSPI_HAS_RESET_GPIO
	if (!device_is_ready(cfg->reset_gpio.port)) {
		LOG_ERR("reset GPIO port not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&cfg->reset_gpio, GPIO_OUTPUT_ACTIVE);
	if (ret != 0) {
		LOG_ERR("cannot configure reset GPIO: %d", ret);
		return ret;
	}

	k_msleep(cfg->reset_pulse_ms);

	ret = gpio_pin_set_dt(&cfg->reset_gpio, 0);
	if (ret != 0) {
		LOG_ERR("cannot release reset GPIO: %d", ret);
		return ret;
	}
#endif

	ret = gd32_ospi_send_simple_cmd(dev, SPI_NOR_CMD_RESET_EN);
	if (ret != 0) {
		return ret;
	}

	ret = gd32_ospi_send_simple_cmd(dev, SPI_NOR_CMD_RESET_MEM);
	if (ret != 0) {
		return ret;
	}

	k_busy_wait(GD32_OSPI_RESET_WAIT_US);

	/*
	 * Vendor resets twice (1-line, then 8-line framing): a flash left in
	 * Octal STR mode by a previous run would ignore a 1-line reset, so the
	 * second pass guarantees it returns to 1-1-1 SPI.
	 */
	if (cfg->bus_width == OSPI_OPI_MODE) {
		data->opi_enabled = true;

		ret = gd32_ospi_send_simple_cmd(dev, SPI_NOR_CMD_RESET_EN);
		if (ret != 0) {
			data->opi_enabled = false;
			return ret;
		}

		ret = gd32_ospi_send_simple_cmd(dev, SPI_NOR_CMD_RESET_MEM);
		if (ret != 0) {
			data->opi_enabled = false;
			return ret;
		}

		data->opi_enabled = false;
		k_busy_wait(GD32_OSPI_RESET_WAIT_US);
	}

	if (cfg->bus_width == OSPI_OPI_MODE) {
		ret = gd32_ospi_configure_opi_str_mode(dev);
		if (ret != 0) {
			return ret;
		}
	}

	data->address_width = (cfg->flash_size > (16U * 1024U * 1024U)) ? 4U : 3U;
	data->read_opcode = gd32_ospi_read_opcode(cfg, data->address_width);
	data->program_opcode = gd32_ospi_program_opcode(cfg, data->address_width);

	if ((data->address_width == 4U) && !cfg->use_4b_opcodes &&
	    (cfg->bus_width != OSPI_OPI_MODE)) {
		ret = gd32_ospi_write_enable(dev);
		if (ret != 0) {
			return ret;
		}
		ret = gd32_ospi_send_simple_cmd(dev, SPI_NOR_CMD_4BA);
		if (ret != 0) {
			return ret;
		}
		ret = gd32_ospi_wait_ready(dev);
		if (ret != 0) {
			return ret;
		}
	}

	ret = gd32_ospi_wait_ready(dev);
	if (ret != 0) {
		return ret;
	}

#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	data->layout.pages_size = GD32_OSPI_ERASE_SIZE;
	data->layout.pages_count = cfg->flash_size / GD32_OSPI_ERASE_SIZE;
#endif

	return 0;
}

static DEVICE_API(flash, flash_gd32_ospi_driver_api) = {
	.read = flash_gd32_ospi_read,
	.write = flash_gd32_ospi_write,
	.erase = flash_gd32_ospi_erase,
	.get_parameters = flash_gd32_ospi_get_parameters,
	.get_size = flash_gd32_ospi_get_size,
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	.page_layout = flash_gd32_ospi_pages_layout,
#endif
};

PINCTRL_DT_DEFINE(GD32_OSPI_CTRL_NODE);

static struct flash_gd32_ospi_data flash_gd32_ospi_data_0;

static const struct flash_gd32_ospi_config flash_gd32_ospi_cfg_0 = {
	.periph = DT_REG_ADDR_BY_IDX(GD32_OSPI_CTRL_NODE, 0),
	.flash_size = (DT_PROP(GD32_OSPI_FLASH_NODE, size) / 8U),
	.max_frequency = DT_PROP(GD32_OSPI_FLASH_NODE, ospi_max_frequency),
	.bus_width = DT_PROP(GD32_OSPI_FLASH_NODE, spi_bus_width),
	.data_rate = DT_PROP(GD32_OSPI_FLASH_NODE, data_rate),
	.writeoc = GD32_OSPI_WRITEOC_FROM_DT(0),
	.use_4b_opcodes = DT_INST_PROP_OR(0, four_byte_opcodes, false),
	.clkid_ospix = DT_CLOCKS_CELL_BY_IDX(GD32_OSPI_CTRL_NODE, 0, id),
	.clkid_ospi_ker = DT_CLOCKS_CELL_BY_IDX(GD32_OSPI_CTRL_NODE, 1, id),
	.clock = DEVICE_DT_GET(DT_CLOCKS_CTLR_BY_IDX(GD32_OSPI_CTRL_NODE, 0)),
	.pcfg = PINCTRL_DT_DEV_CONFIG_GET(GD32_OSPI_CTRL_NODE),
	.mmap_base = DT_REG_ADDR_BY_IDX(GD32_OSPI_CTRL_NODE, 1),
	.mmap_size = DT_REG_SIZE_BY_IDX(GD32_OSPI_CTRL_NODE, 1),
#if GD32_OSPI_HAS_RESET
	.reset = RESET_DT_SPEC_GET(GD32_OSPI_CTRL_NODE),
#endif
#if GD32_OSPI_HAS_RESET_GPIO
	.reset_gpio = GPIO_DT_SPEC_INST_GET(0, reset_gpios),
	.reset_pulse_ms = DT_INST_PROP_OR(0, reset_gpios_duration, 1U),
#endif
};

DEVICE_DT_INST_DEFINE(0, flash_gd32_ospi_init, NULL, &flash_gd32_ospi_data_0,
		      &flash_gd32_ospi_cfg_0, POST_KERNEL, CONFIG_FLASH_INIT_PRIORITY,
		      &flash_gd32_ospi_driver_api);
