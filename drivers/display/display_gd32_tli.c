/*
 * Copyright (c) 2025 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT gd_gd32_tli

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/gd32.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/logging/log.h>

#include <gd32_tli.h>
#include <gd32_rcu.h>

LOG_MODULE_REGISTER(display_gd32_tli, CONFIG_DISPLAY_LOG_LEVEL);

/*
 * GD32 TLI Pixel Format (PPF[2:0]):
 *   000 - ARGB8888  (4 bytes/pixel)
 *   001 - RGB888    (3 bytes/pixel)
 *   010 - RGB565    (2 bytes/pixel)
 *   011 - ARGB1555  (2 bytes/pixel)
 *   100 - ARGB4444  (2 bytes/pixel)
 *   101 - L8        (1 byte/pixel, grayscale)
 *   110 - AL44      (1 byte/pixel, alpha + grayscale)
 *   111 - AL88      (2 bytes/pixel, alpha + grayscale)
 */

/* Pixel format configuration based on Kconfig (like STM32 LTDC) */
#if defined(CONFIG_GD32_TLI_ARGB8888)
#define GD32_TLI_INIT_PIXEL_SIZE    4u
#define GD32_TLI_INIT_PIXEL_FORMAT  LAYER_PPF_ARGB8888
#define DISPLAY_INIT_PIXEL_FORMAT   PIXEL_FORMAT_ARGB_8888
#elif defined(CONFIG_GD32_TLI_RGB888)
#define GD32_TLI_INIT_PIXEL_SIZE    3u
#define GD32_TLI_INIT_PIXEL_FORMAT  LAYER_PPF_RGB888
#define DISPLAY_INIT_PIXEL_FORMAT   PIXEL_FORMAT_RGB_888
#elif defined(CONFIG_GD32_TLI_RGB565)
#define GD32_TLI_INIT_PIXEL_SIZE    2u
#define GD32_TLI_INIT_PIXEL_FORMAT  LAYER_PPF_RGB565
#define DISPLAY_INIT_PIXEL_FORMAT   PIXEL_FORMAT_RGB_565
#else
#error "Invalid GD32 TLI pixel format chosen"
#endif

struct display_gd32_tli_config {
	uint32_t reg;
	uint16_t clkid;
	struct gpio_dt_spec disp_en_gpio;
	struct gpio_dt_spec bl_ctrl_gpio;
	const struct pinctrl_dev_config *pctrl;
	uint16_t hsync;
	uint16_t vsync;
	uint16_t hbp;
	uint16_t vbp;
	uint16_t hfp;
	uint16_t vfp;
	uint16_t width;
	uint16_t height;
	uint16_t pllsai_n;
	uint16_t pllsai_r;
	uint32_t pllsair_div;
};

struct display_gd32_tli_data {
	uint8_t *frame_buffer;
	uint32_t frame_buffer_len;
	enum display_pixel_format current_pixel_format;
	uint8_t current_pixel_size;
	enum display_orientation orientation;
};

static int display_gd32_tli_write(const struct device *dev, const uint16_t x,
				  const uint16_t y,
				  const struct display_buffer_descriptor *desc,
				  const void *buf)
{
	const struct display_gd32_tli_config *config = dev->config;
	struct display_gd32_tli_data *data = dev->data;
	uint16_t width = desc->width;
	uint16_t height = desc->height;
	uint16_t pitch = desc->pitch;
	const uint8_t *src = buf;
	uint8_t *dst;

	if ((x + width > config->width) || (y + height > config->height)) {
		LOG_ERR("Write coordinates out of bounds");
		return -EINVAL;
	}

	dst = data->frame_buffer;

	for (uint16_t row = 0; row < height; row++) {
		uint32_t dst_offset = ((y + row) * config->width + x) * data->current_pixel_size;
		uint32_t src_offset = row * pitch * data->current_pixel_size;

		memcpy(dst + dst_offset, src + src_offset, width * data->current_pixel_size);
	}

	/* Trigger TLI reload in vertical blanking period to update display */
	tli_reload_config(TLI_FRAME_BLANK_RELOAD_EN);

	return 0;
}

static int display_gd32_tli_read(const struct device *dev, const uint16_t x,
				 const uint16_t y,
				 const struct display_buffer_descriptor *desc,
				 void *buf)
{
	LOG_ERR("Read not supported");
	return -ENOTSUP;
}

static void *display_gd32_tli_get_framebuffer(const struct device *dev)
{
	struct display_gd32_tli_data *data = dev->data;

	return data->frame_buffer;
}

static int display_gd32_tli_blanking_off(const struct device *dev)
{
	const struct display_gd32_tli_config *config = dev->config;
	int ret;

	/* Turn on backlight (if configured in device tree) */
	if (config->bl_ctrl_gpio.port) {
		ret = gpio_pin_set_dt(&config->bl_ctrl_gpio, 1);
		if (ret < 0) {
			LOG_ERR("Failed to turn on backlight");
			return ret;
		}
	}

	tli_enable();
	return 0;
}

static int display_gd32_tli_blanking_on(const struct device *dev)
{
	const struct display_gd32_tli_config *config = dev->config;
	int ret;

	tli_disable();

	/* Turn off backlight (if configured in device tree) */
	if (config->bl_ctrl_gpio.port) {
		ret = gpio_pin_set_dt(&config->bl_ctrl_gpio, 0);
		if (ret < 0) {
			LOG_ERR("Failed to turn off backlight");
			return ret;
		}
	}

	return 0;
}

static int display_gd32_tli_set_brightness(const struct device *dev,
					   const uint8_t brightness)
{
	LOG_WRN("Set brightness not supported");
	return -ENOTSUP;
}

static int display_gd32_tli_set_contrast(const struct device *dev,
					 const uint8_t contrast)
{
	LOG_WRN("Set contrast not supported");
	return -ENOTSUP;
}

static void display_gd32_tli_get_capabilities(
	const struct device *dev, struct display_capabilities *capabilities)
{
	const struct display_gd32_tli_config *config = dev->config;
	struct display_gd32_tli_data *data = dev->data;

	memset(capabilities, 0, sizeof(struct display_capabilities));
	capabilities->x_resolution = config->width;
	capabilities->y_resolution = config->height;
	/* TLI supports multiple pixel formats */
	capabilities->supported_pixel_formats = PIXEL_FORMAT_ARGB_8888 |
						PIXEL_FORMAT_RGB_888 |
						PIXEL_FORMAT_RGB_565;
	capabilities->current_pixel_format = data->current_pixel_format;
	capabilities->current_orientation = data->orientation;
}

/* Reconfigure TLI layer with new pixel format (like STM32 LTDC) */
static int display_gd32_tli_set_pixel_format(const struct device *dev,
					     const enum display_pixel_format pixel_format)
{
	const struct display_gd32_tli_config *config = dev->config;
	struct display_gd32_tli_data *data = dev->data;
	tli_layer_parameter_struct tli_layer_init_struct;
	uint32_t tli_ppf;
	uint8_t pixel_size;

	switch (pixel_format) {
	case PIXEL_FORMAT_RGB_565:
		tli_ppf = LAYER_PPF_RGB565;
		pixel_size = 2u;
		break;
	case PIXEL_FORMAT_RGB_888:
		tli_ppf = LAYER_PPF_RGB888;
		pixel_size = 3u;
		break;
	case PIXEL_FORMAT_ARGB_8888:
		tli_ppf = LAYER_PPF_ARGB8888;
		pixel_size = 4u;
		break;
	default:
		LOG_ERR("Unsupported pixel format: %d", pixel_format);
		return -ENOTSUP;
	}

	/* Check if new format fits in allocated framebuffer */
	uint32_t required_size = config->width * config->height * pixel_size;

	if (required_size > data->frame_buffer_len) {
		LOG_ERR("Cannot switch to format %d: requires %u bytes, only %u allocated",
			pixel_format, required_size, data->frame_buffer_len);
		LOG_ERR("Change CONFIG_GD32_TLI_PIXEL_FORMAT in Kconfig to use this format");
		return -ENOMEM;
	}

	/* Disable layer before reconfiguration */
	tli_layer_disable(LAYER0);
	tli_reload_config(TLI_REQUEST_RELOAD_EN);

	/* Configure Layer 0 with new pixel format */
	tli_layer_init_struct.layer_window_leftpos = config->hsync + config->hbp;
	tli_layer_init_struct.layer_window_rightpos =
		config->hsync + config->hbp + config->width - 1;
	tli_layer_init_struct.layer_window_toppos = config->vsync + config->vbp;
	tli_layer_init_struct.layer_window_bottompos =
		config->vsync + config->vbp + config->height - 1;
	tli_layer_init_struct.layer_ppf = tli_ppf;
	tli_layer_init_struct.layer_sa = 0xFF;
	tli_layer_init_struct.layer_default_blue = 0xFF;
	tli_layer_init_struct.layer_default_green = 0xFF;
	tli_layer_init_struct.layer_default_red = 0xFF;
	tli_layer_init_struct.layer_default_alpha = 0x0;
	tli_layer_init_struct.layer_acf1 = LAYER_ACF1_PASA;
	tli_layer_init_struct.layer_acf2 = LAYER_ACF2_PASA;
	tli_layer_init_struct.layer_frame_bufaddr = (uint32_t)data->frame_buffer;
	tli_layer_init_struct.layer_frame_line_length =
		(config->width * pixel_size) + 3;
	tli_layer_init_struct.layer_frame_buf_stride_offset =
		config->width * pixel_size;
	tli_layer_init_struct.layer_frame_total_line_number = config->height;

	tli_layer_init(LAYER0, &tli_layer_init_struct);

	/* Re-enable layer */
	tli_layer_enable(LAYER0);
	tli_reload_config(TLI_FRAME_BLANK_RELOAD_EN);

	/* Update current pixel format */
	data->current_pixel_format = pixel_format;
	data->current_pixel_size = pixel_size;

	LOG_INF("TLI pixel format changed to %d (bpp=%d)", pixel_format, pixel_size);

	return 0;
}

static int display_gd32_tli_set_orientation(const struct device *dev,
					    const enum display_orientation
						    orientation)
{
	if (orientation != DISPLAY_ORIENTATION_NORMAL) {
		LOG_ERR("Only normal orientation supported");
		return -ENOTSUP;
	}
	return 0;
}

static const struct display_driver_api display_gd32_tli_api = {
	.blanking_on = display_gd32_tli_blanking_on,
	.blanking_off = display_gd32_tli_blanking_off,
	.write = display_gd32_tli_write,
	.read = display_gd32_tli_read,
	.get_framebuffer = display_gd32_tli_get_framebuffer,
	.set_brightness = display_gd32_tli_set_brightness,
	.set_contrast = display_gd32_tli_set_contrast,
	.get_capabilities = display_gd32_tli_get_capabilities,
	.set_pixel_format = display_gd32_tli_set_pixel_format,
	.set_orientation = display_gd32_tli_set_orientation,
};

static int display_gd32_tli_init(const struct device *dev)
{
	const struct display_gd32_tli_config *config = dev->config;
	struct display_gd32_tli_data *data = dev->data;
	tli_parameter_struct tli_init_struct;
	tli_layer_parameter_struct tli_layer_init_struct;
	int ret;

	/* Configure display enable GPIO (if configured in device tree) */
	if (config->disp_en_gpio.port) {
		ret = gpio_pin_configure_dt(&config->disp_en_gpio, GPIO_OUTPUT_ACTIVE);
		if (ret < 0) {
			LOG_ERR("Failed to configure display enable GPIO");
			return ret;
		}
	}

	/* Configure backlight GPIO (if configured in device tree) */
	if (config->bl_ctrl_gpio.port) {
		ret = gpio_pin_configure_dt(&config->bl_ctrl_gpio, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_ERR("Failed to configure backlight GPIO");
			return ret;
		}
	}

	/* Configure DT provided pins */
	ret = pinctrl_apply_state(config->pctrl, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("TLI pinctrl setup failed");
		return ret;
	}

	/* Enable TLI peripheral clock */
	ret = clock_control_on(GD32_CLOCK_CONTROLLER,
			       (clock_control_subsys_t)&config->clkid);
	if (ret < 0) {
		LOG_ERR("Failed to enable TLI clock");
		return ret;
	}

	/* Configure PLLSAI for LCD pixel clock */
	if (rcu_pllsai_r_config(config->pllsai_n, config->pllsai_r) == ERROR) {
		LOG_ERR("PLLSAI configuration failed");
		return -EIO;
	}

	rcu_tli_clock_div_config(config->pllsair_div);
	rcu_osci_on(RCU_PLLSAI_CK);
	if (rcu_osci_stab_wait(RCU_PLLSAI_CK) == ERROR) {
		LOG_ERR("PLLSAI stabilization timeout");
		return -ETIMEDOUT;
	}

	tli_init_struct.signalpolarity_hs = TLI_HSYN_ACTLIVE_LOW;
	tli_init_struct.signalpolarity_vs = TLI_VSYN_ACTLIVE_LOW;
	tli_init_struct.signalpolarity_de = TLI_DE_ACTLIVE_LOW;
	tli_init_struct.signalpolarity_pixelck = TLI_PIXEL_CLOCK_TLI;

	tli_init_struct.synpsz_hpsz = config->hsync - 1;
	tli_init_struct.synpsz_vpsz = config->vsync - 1;
	tli_init_struct.backpsz_hbpsz = config->hsync + config->hbp - 1;
	tli_init_struct.backpsz_vbpsz = config->vsync + config->vbp - 1;
	tli_init_struct.activesz_hasz = config->hsync + config->hbp + config->width - 1;
	tli_init_struct.activesz_vasz = config->vsync + config->vbp + config->height - 1;
	tli_init_struct.totalsz_htsz = config->hsync + config->hbp + config->width +
					config->hfp - 1;
	tli_init_struct.totalsz_vtsz = config->vsync + config->vbp + config->height +
					config->vfp - 1;

	tli_init_struct.backcolor_red = 0xFF;
	tli_init_struct.backcolor_green = 0xFF;
	tli_init_struct.backcolor_blue = 0xFF;

	tli_init(&tli_init_struct);

	/* Configure Layer 0 with Kconfig-selected pixel format */
	tli_layer_init_struct.layer_window_leftpos = config->hsync + config->hbp;
	tli_layer_init_struct.layer_window_rightpos =
		config->hsync + config->hbp + config->width - 1;
	tli_layer_init_struct.layer_window_toppos = config->vsync + config->vbp;
	tli_layer_init_struct.layer_window_bottompos =
		config->vsync + config->vbp + config->height - 1;
	tli_layer_init_struct.layer_ppf = GD32_TLI_INIT_PIXEL_FORMAT;
	tli_layer_init_struct.layer_sa = 0xFF;
	tli_layer_init_struct.layer_default_blue = 0xFF;
	tli_layer_init_struct.layer_default_green = 0xFF;
	tli_layer_init_struct.layer_default_red = 0xFF;
	tli_layer_init_struct.layer_default_alpha = 0x0;
	tli_layer_init_struct.layer_acf1 = LAYER_ACF1_PASA;
	tli_layer_init_struct.layer_acf2 = LAYER_ACF2_PASA;
	tli_layer_init_struct.layer_frame_bufaddr = (uint32_t)data->frame_buffer;
	tli_layer_init_struct.layer_frame_line_length =
		(config->width * GD32_TLI_INIT_PIXEL_SIZE) + 3;
	tli_layer_init_struct.layer_frame_buf_stride_offset =
		config->width * GD32_TLI_INIT_PIXEL_SIZE;
	tli_layer_init_struct.layer_frame_total_line_number = config->height;

	tli_layer_init(LAYER0, &tli_layer_init_struct);

	/* Enable layer and TLI */
	tli_layer_enable(LAYER0);
	tli_reload_config(TLI_FRAME_BLANK_RELOAD_EN);
	tli_enable();

	LOG_INF("TLI initialized: %dx%d, pixel_format=%d (bpp=%d)",
		config->width, config->height,
		data->current_pixel_format, data->current_pixel_size);

	return 0;
}

/* Framebuffer size based on Kconfig pixel format (like STM32 LTDC) */
#define GD32_TLI_FB_SIZE(inst) \
	(GD32_TLI_INIT_PIXEL_SIZE * DT_INST_PROP(inst, height) * DT_INST_PROP(inst, width))

#ifdef CONFIG_PINCTRL
#define GD32_TLI_PINCTRL_DEFINE(inst) PINCTRL_DT_INST_DEFINE(inst)
#define GD32_TLI_PINCTRL_INIT(inst) PINCTRL_DT_INST_DEV_CONFIG_GET(inst)
#else
#define GD32_TLI_PINCTRL_DEFINE(inst)
#define GD32_TLI_PINCTRL_INIT(inst) NULL
#endif

#define DISPLAY_GD32_TLI_DEVICE(inst)						\
										\
	GD32_TLI_PINCTRL_DEFINE(inst);						\
										\
	/* Frame buffer aligned for optimal performance */			\
	static uint8_t __aligned(4) __attribute__((section(".noinit")))		\
		frame_buffer_##inst[CONFIG_GD32_TLI_FB_NUM * GD32_TLI_FB_SIZE(inst)]; \
										\
	static const struct display_gd32_tli_config				\
		display_gd32_tli_config_##inst = {				\
		.reg = DT_INST_REG_ADDR(inst),					\
		.clkid = DT_INST_CLOCKS_CELL(inst, id),				\
		.disp_en_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, disp_en_gpios, {0}),	\
		.bl_ctrl_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, bl_ctrl_gpios, {0}),	\
		.pctrl = GD32_TLI_PINCTRL_INIT(inst),				\
		.hsync = DT_INST_PROP(inst, hsync),				\
		.vsync = DT_INST_PROP(inst, vsync),				\
		.hbp = DT_INST_PROP(inst, hbp),					\
		.vbp = DT_INST_PROP(inst, vbp),					\
		.hfp = DT_INST_PROP(inst, hfp),					\
		.vfp = DT_INST_PROP(inst, vfp),					\
		.width = DT_INST_PROP(inst, width),				\
		.height = DT_INST_PROP(inst, height),				\
		.pllsai_n = DT_INST_PROP(inst, pllsai_n),			\
		.pllsai_r = DT_INST_PROP(inst, pllsai_r),			\
		.pllsair_div = DT_INST_PROP(inst, pllsair_div),			\
	};									\
										\
	static struct display_gd32_tli_data display_gd32_tli_data_##inst = {	\
		.frame_buffer = frame_buffer_##inst,				\
		.frame_buffer_len = GD32_TLI_FB_SIZE(inst),			\
		.current_pixel_format = DISPLAY_INIT_PIXEL_FORMAT,		\
		.current_pixel_size = GD32_TLI_INIT_PIXEL_SIZE,			\
		.orientation = DISPLAY_ORIENTATION_NORMAL,			\
	};									\
										\
	DEVICE_DT_INST_DEFINE(inst, display_gd32_tli_init, NULL,		\
			      &display_gd32_tli_data_##inst,			\
			      &display_gd32_tli_config_##inst,			\
			      POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY,	\
			      &display_gd32_tli_api);

DT_INST_FOREACH_STATUS_OKAY(DISPLAY_GD32_TLI_DEVICE)
