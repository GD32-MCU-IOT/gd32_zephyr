/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT gd_gd32_ili9320

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/gd32.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/sys_io.h>

#include <gd32_exmc.h>

struct ili9320_config {
	mem_addr_t command;
	mem_addr_t data;
	const struct pinctrl_dev_config *pinctrl;
	uint16_t clock;
	uint16_t width;
	uint16_t height;
	uint32_t address_setup;
	uint32_t address_hold;
	uint32_t data_setup;
	uint32_t bus_latency;
};

struct ili9320_reg {
	uint16_t address;
	uint16_t value;
};

static const struct ili9320_reg initialization[] = {
	{0x01, 0x0100}, {0x02, 0x0700}, {0x03, 0x1020}, {0x04, 0x0000}, {0x08, 0x0202},
	{0x09, 0x0000}, {0x0a, 0x0000}, {0x0c, 0x0001}, {0x0d, 0x0000}, {0x0f, 0x0000},
	{0x07, 0x0101}, {0x10, 0x10c0}, {0x11, 0x0007}, {0x12, 0x0110}, {0x13, 0x0b00},
	{0x29, 0x0000}, {0x2b, 0x4010}, {0x50, 0x0000}, {0x51, 0x00ef}, {0x52, 0x0000},
	{0x53, 0x013f}, {0x60, 0x2700}, {0x61, 0x0001}, {0x6a, 0x0000}, {0x80, 0x0000},
	{0x81, 0x0000}, {0x82, 0x0000}, {0x83, 0x0000}, {0x84, 0x0000}, {0x85, 0x0000},
	{0x90, 0x0010}, {0x92, 0x0000}, {0x93, 0x0001}, {0x95, 0x0110}, {0x97, 0x0000},
	{0x98, 0x0000}, {0x07, 0x0173},
};

static void ili9320_command(const struct ili9320_config *config, uint16_t command)
{
	sys_write16(command, config->command);
	barrier_dsync_fence_full();
}

static void ili9320_register_write(const struct ili9320_config *config, uint16_t address,
				   uint16_t value)
{
	ili9320_command(config, address);
	sys_write16(value, config->data);
	barrier_dsync_fence_full();
}

static int ili9320_write(const struct device *dev, const uint16_t x, const uint16_t y,
			 const struct display_buffer_descriptor *desc, const void *buf)
{
	const struct ili9320_config *config = dev->config;
	const uint8_t *pixels = buf;

	if (buf == NULL || desc->width == 0U || desc->height == 0U || desc->pitch < desc->width ||
	    x + desc->width > config->width || y + desc->height > config->height ||
	    desc->buf_size < ((size_t)(desc->height - 1U) * desc->pitch + desc->width) * 2U) {
		return -EINVAL;
	}

	ili9320_register_write(config, 0x50, x);
	ili9320_register_write(config, 0x51, x + desc->width - 1U);
	ili9320_register_write(config, 0x52, y);
	ili9320_register_write(config, 0x53, y + desc->height - 1U);
	ili9320_register_write(config, 0x20, x + desc->width - 1U);
	ili9320_register_write(config, 0x21, y);
	ili9320_command(config, 0x22);

	for (uint16_t row = 0; row < desc->height; row++) {
		for (uint16_t column = 0; column < desc->width; column++) {
			size_t offset =
				((size_t)row * desc->pitch + desc->width - 1U - column) * 2U;

			sys_write16(sys_get_le16(&pixels[offset]), config->data);
		}
	}
	barrier_dsync_fence_full();
	return 0;
}

static int ili9320_blanking_on(const struct device *dev)
{
	ili9320_register_write(dev->config, 0x07, 0x0000);
	return 0;
}

static int ili9320_blanking_off(const struct device *dev)
{
	ili9320_register_write(dev->config, 0x07, 0x0173);
	return 0;
}

static void ili9320_get_capabilities(const struct device *dev,
				     struct display_capabilities *capabilities)
{
	const struct ili9320_config *config = dev->config;

	memset(capabilities, 0, sizeof(*capabilities));
	capabilities->x_resolution = config->width;
	capabilities->y_resolution = config->height;
	capabilities->supported_pixel_formats = PIXEL_FORMAT_RGB_565;
	capabilities->current_pixel_format = PIXEL_FORMAT_RGB_565;
	capabilities->current_orientation = DISPLAY_ORIENTATION_NORMAL;
}

static int ili9320_set_pixel_format(const struct device *dev, enum display_pixel_format format)
{
	ARG_UNUSED(dev);
	return format == PIXEL_FORMAT_RGB_565 ? 0 : -ENOTSUP;
}

static int ili9320_set_orientation(const struct device *dev, enum display_orientation orientation)
{
	ARG_UNUSED(dev);
	return orientation == DISPLAY_ORIENTATION_NORMAL ? 0 : -ENOTSUP;
}

static int ili9320_init(const struct device *dev)
{
	const struct ili9320_config *config = dev->config;
	exmc_norsram_timing_parameter_struct timing = {
		.syn_data_latency = EXMC_DATALAT_2_CLK,
		.syn_clk_division = EXMC_SYN_CLOCK_RATIO_DISABLE,
		.bus_latency = config->bus_latency,
		.asyn_data_setuptime = config->data_setup,
		.asyn_address_holdtime = config->address_hold,
		.asyn_address_setuptime = config->address_setup,
	};
	exmc_norsram_parameter_struct exmc = {
		.write_mode = EXMC_ASYN_WRITE,
		.asyn_wait = DISABLE,
		.nwait_signal = DISABLE,
		.memory_write = ENABLE,
		.nwait_config = EXMC_NWAIT_CONFIG_BEFORE,
		.wrap_burst_mode = DISABLE,
		.nwait_polarity = EXMC_NWAIT_POLARITY_LOW,
		.burst_mode = DISABLE,
		.databus_width = EXMC_NOR_DATABUS_WIDTH_16B,
		.memory_type = EXMC_MEMORY_TYPE_NOR,
		.address_data_mux = DISABLE,
		.read_write_timing = &timing,
	};
	int ret = pinctrl_apply_state(config->pinctrl, PINCTRL_STATE_DEFAULT);

	if (ret < 0) {
		return ret;
	}
	ret = clock_control_on(GD32_CLOCK_CONTROLLER, (clock_control_subsys_t)&config->clock);
	if (ret < 0) {
		return ret;
	}
	exmc_norsram_init(&exmc);
	exmc_norsram_enable();

	for (size_t index = 0; index < ARRAY_SIZE(initialization); index++) {
		ili9320_register_write(config, initialization[index].address,
				       initialization[index].value);
		if (initialization[index].address == 0x12) {
			/* Step-up circuit stabilization after PON is set */
			k_msleep(40);
		} else if (initialization[index].address == 0x0f ||
			   initialization[index].address == 0x07 ||
			   initialization[index].address == 0x98) {
			k_msleep(10);
		}
	}
	return 0;
}

static DEVICE_API(display, ili9320_api) = {
	.blanking_on = ili9320_blanking_on,
	.blanking_off = ili9320_blanking_off,
	.write = ili9320_write,
	.get_capabilities = ili9320_get_capabilities,
	.set_pixel_format = ili9320_set_pixel_format,
	.set_orientation = ili9320_set_orientation,
};

#define ILI9320_DEFINE(inst)                                                                       \
	PINCTRL_DT_INST_DEFINE(inst);                                                              \
	static const struct ili9320_config ili9320_config_##inst = {                               \
		.command = DT_INST_REG_ADDR_BY_IDX(inst, 0),                                       \
		.data = DT_INST_REG_ADDR_BY_IDX(inst, 1),                                          \
		.pinctrl = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                                   \
		.clock = DT_INST_CLOCKS_CELL(inst, id),                                            \
		.width = DT_INST_PROP(inst, width),                                                \
		.height = DT_INST_PROP(inst, height),                                              \
		.address_setup = DT_INST_PROP(inst, address_setup_cycles),                         \
		.address_hold = DT_INST_PROP(inst, address_hold_cycles),                           \
		.data_setup = DT_INST_PROP(inst, data_setup_cycles),                               \
		.bus_latency = DT_INST_PROP(inst, bus_latency_cycles),                             \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, ili9320_init, NULL, NULL, &ili9320_config_##inst, POST_KERNEL, \
			      CONFIG_DISPLAY_INIT_PRIORITY, &ili9320_api);

DT_INST_FOREACH_STATUS_OKAY(ILI9320_DEFINE)
