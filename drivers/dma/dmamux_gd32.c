/*
 * Copyright (c) 2025 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief GD32 DMAMUX driver
 *
 * The DMAMUX is a request multiplexer that routes peripheral DMA requests
 * to DMA channels. This driver acts as a proxy between peripherals and
 * the underlying DMA controller, setting the request ID in the DMAMUX
 * hardware before forwarding calls to the real DMA.
 *
 * Architecture:
 *   Peripheral -> DMAMUX -> DMA Controller
 *
 * The DMAMUX channels map to underlying DMA channels:
 *   - Channel 0-6:  DMA0 channel 0-6
 *   - Channel 7-11: DMA1 channel 0-4
 */

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/gd32.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <gd32_dma.h>

LOG_MODULE_REGISTER(dmamux_gd32, CONFIG_DMA_LOG_LEVEL);

#define DT_DRV_COMPAT gd_gd32_dmamux

/*
 * Use HAL register definitions from gd32f50x_dma.h:
 *   DMAMUX_RM_CHxCFG        - channel config registers (offset 0x00 + ch*4)
 *   DMAMUX_RM_INTF          - interrupt flag register (offset 0x80)
 *   DMAMUX_RM_INTC          - interrupt clear register (offset 0x84)
 *   DMAMUX_RM_CHXCFG_MUXID  - request ID field [7:0]
 *   DMAMUX_RM_CHXCFG_SOIE   - sync overrun interrupt enable
 *   DMAMUX_RM_CHXCFG_EVGEN  - event generation enable
 *   DMAMUX_RM_CHXCFG_SYNCEN - sync enable
 *
 * Access macro for channel config register by base address and channel number
 */
#define DMAMUX_CHxCFG(base, ch) REG32((base) + (ch) * 4U)
#define DMAMUX_INTF(base)       REG32((base) + 0x80U)
#define DMAMUX_INTC(base)       REG32((base) + 0x84U)

/* DMA0 has 7 channels (0-6), DMA1 has 5 channels (0-4) */
#define DMA0_CHANNEL_COUNT 7
#define DMA1_CHANNEL_COUNT 5
#define MAX_DMAMUX_CHANNELS (DMA0_CHANNEL_COUNT + DMA1_CHANNEL_COUNT)

/* Each DMAMUX channel maps to a specific DMA controller and channel */
struct dmamux_gd32_channel_map {
	const struct device *dma_dev;
	uint8_t dma_channel;
};

struct dmamux_gd32_config {
	uint32_t base;
	uint8_t channel_count;
	uint8_t generator_count;
	uint16_t request_count;
	uint16_t clkid;
	const struct dmamux_gd32_channel_map *channel_map;
};

struct dmamux_gd32_data {
	/* DMA context for channel allocation (required by dma_request_channel) */
	struct dma_context ctx;
	/* Per-channel callback info */
	dma_callback_t callbacks[MAX_DMAMUX_CHANNELS];
	void *user_data[MAX_DMAMUX_CHANNELS];
};

/**
 * @brief Set the request ID for a DMAMUX channel
 */
static void dmamux_gd32_set_request(const struct dmamux_gd32_config *cfg,
				    uint32_t channel, uint32_t request_id)
{
	uint32_t reg_val;

	reg_val = DMAMUX_CHxCFG(cfg->base, channel);
	reg_val &= ~BITS(0, 7);
	reg_val |= (request_id & BITS(0, 7));
	DMAMUX_CHxCFG(cfg->base, channel) = reg_val;

	LOG_DBG("DMAMUX ch%d: set request ID %d (reg=0x%08x)",
		channel, request_id, DMAMUX_CHxCFG(cfg->base, channel));
}

/**
 * @brief DMA callback wrapper to route to user callback
 */
static void dmamux_gd32_dma_callback(const struct device *dma_dev, void *arg,
				     uint32_t channel, int status)
{
	const struct device *dev = arg;
	struct dmamux_gd32_data *data = dev->data;
	const struct dmamux_gd32_config *cfg = dev->config;

	/* Find the DMAMUX channel corresponding to this DMA callback */
	for (uint32_t i = 0; i < cfg->channel_count; i++) {
		if (cfg->channel_map[i].dma_dev == dma_dev &&
		    cfg->channel_map[i].dma_channel == channel) {
			if (data->callbacks[i]) {
				data->callbacks[i](dev, data->user_data[i], i, status);
			}
			return;
		}
	}

	LOG_WRN("DMAMUX: Unexpected DMA callback from %s ch%d",
		dma_dev->name, channel);
}

static int dmamux_gd32_configure(const struct device *dev, uint32_t channel,
				 struct dma_config *config)
{
	const struct dmamux_gd32_config *cfg = dev->config;
	struct dmamux_gd32_data *data = dev->data;
	const struct dmamux_gd32_channel_map *map;
	uint32_t request_id;
	int ret;
	struct dma_config dma_cfg;

	if (channel >= cfg->channel_count) {
		LOG_ERR("DMAMUX channel %d out of range (max %d)",
			channel, cfg->channel_count - 1);
		return -EINVAL;
	}

	/* Request ID is passed via dma_slot */
	request_id = config->dma_slot;
	if (request_id > cfg->request_count) {
		LOG_ERR("DMAMUX request ID %d out of range (max %d)",
			request_id, cfg->request_count);
		return -EINVAL;
	}

	map = &cfg->channel_map[channel];
	if (!device_is_ready(map->dma_dev)) {
		LOG_ERR("DMAMUX: DMA device %s not ready", map->dma_dev->name);
		return -ENODEV;
	}

	/* Store user callback for later routing */
	data->callbacks[channel] = config->dma_callback;
	data->user_data[channel] = config->user_data;

	/* Create a copy of config to avoid modifying the caller's structure */
	memcpy(&dma_cfg, config, sizeof(dma_cfg));

	/* Replace callback with our wrapper in the copy */
	dma_cfg.dma_callback = dmamux_gd32_dma_callback;
	dma_cfg.user_data = (void *)dev;

	/* Configure the underlying DMA channel with the modified copy */
	ret = dma_config(map->dma_dev, map->dma_channel, &dma_cfg);
	if (ret < 0) {
		LOG_ERR("DMAMUX: Failed to configure DMA %s ch%d: %d",
			map->dma_dev->name, map->dma_channel, ret);
		return ret;
	}

	/* Set the request ID in DMAMUX hardware */
	dmamux_gd32_set_request(cfg, channel, request_id);

	LOG_DBG("DMAMUX ch%d configured: req=%d -> DMA %s ch%d",
		channel, request_id, map->dma_dev->name, map->dma_channel);

	return 0;
}

static int dmamux_gd32_reload(const struct device *dev, uint32_t channel,
			      uint32_t src, uint32_t dst, size_t size)
{
	const struct dmamux_gd32_config *cfg = dev->config;
	const struct dmamux_gd32_channel_map *map;

	if (channel >= cfg->channel_count) {
		return -EINVAL;
	}

	map = &cfg->channel_map[channel];
	return dma_reload(map->dma_dev, map->dma_channel, src, dst, size);
}

static int dmamux_gd32_start(const struct device *dev, uint32_t channel)
{
	const struct dmamux_gd32_config *cfg = dev->config;
	const struct dmamux_gd32_channel_map *map;

	if (channel >= cfg->channel_count) {
		return -EINVAL;
	}

	map = &cfg->channel_map[channel];

	LOG_DBG("DMAMUX ch%d start -> DMA %s ch%d",
		channel, map->dma_dev->name, map->dma_channel);

	return dma_start(map->dma_dev, map->dma_channel);
}

static int dmamux_gd32_stop(const struct device *dev, uint32_t channel)
{
	const struct dmamux_gd32_config *cfg = dev->config;
	const struct dmamux_gd32_channel_map *map;

	if (channel >= cfg->channel_count) {
		return -EINVAL;
	}

	map = &cfg->channel_map[channel];
	return dma_stop(map->dma_dev, map->dma_channel);
}

static int dmamux_gd32_get_status(const struct device *dev, uint32_t channel,
				  struct dma_status *stat)
{
	const struct dmamux_gd32_config *cfg = dev->config;
	const struct dmamux_gd32_channel_map *map;

	if (channel >= cfg->channel_count) {
		return -EINVAL;
	}

	map = &cfg->channel_map[channel];
	return dma_get_status(map->dma_dev, map->dma_channel, stat);
}

static bool dmamux_gd32_chan_filter(const struct device *dev, int channel,
				    void *filter_param)
{
	const struct dmamux_gd32_config *cfg = dev->config;
	uint32_t filter;

	if (!filter_param) {
		return false;
	}

	filter = *((uint32_t *)filter_param);

	if (channel >= cfg->channel_count) {
		return false;
	}

	return (filter & BIT(channel)) != 0;
}

static int dmamux_gd32_init(const struct device *dev)
{
	const struct dmamux_gd32_config *cfg = dev->config;
	int ret;

	/* Enable DMAMUX clock */
	ret = clock_control_on(GD32_CLOCK_CONTROLLER,
			       (clock_control_subsys_t)&cfg->clkid);
	if (ret < 0) {
		LOG_ERR("Failed to enable DMAMUX clock: %d", ret);
		return ret;
	}

	/* Clear all channel configurations */
	for (uint32_t i = 0; i < cfg->channel_count; i++) {
		DMAMUX_CHxCFG(cfg->base, i) = 0;
	}

	/* Clear interrupt flags */
	DMAMUX_INTC(cfg->base) = 0xFFFFFFFF;

	/* Verify DMA controllers are ready */
	for (uint32_t i = 0; i < cfg->channel_count; i++) {
		if (!device_is_ready(cfg->channel_map[i].dma_dev)) {
			LOG_ERR("DMAMUX: DMA device for channel %d not ready", i);
			return -ENODEV;
		}
	}

	LOG_INF("DMAMUX initialized: %d channels, %d requests",
		cfg->channel_count, cfg->request_count);

	return 0;
}

static DEVICE_API(dma, dmamux_gd32_api) = {
	.config = dmamux_gd32_configure,
	.reload = dmamux_gd32_reload,
	.start = dmamux_gd32_start,
	.stop = dmamux_gd32_stop,
	.get_status = dmamux_gd32_get_status,
	.chan_filter = dmamux_gd32_chan_filter,
};

/*
 * Channel map generation macros
 *
 * Maps DMAMUX channels to underlying DMA controllers:
 *   DMAMUX ch0-6  -> DMA0 ch0-6
 *   DMAMUX ch7-11 -> DMA1 ch0-4
 */

#define DMAMUX_DMA0_DEV DEVICE_DT_GET_OR_NULL(DT_NODELABEL(dma0))
#define DMAMUX_DMA1_DEV DEVICE_DT_GET_OR_NULL(DT_NODELABEL(dma1))

/* LISTIFY passes (index, ...) to the callback, so we need to accept extra args */
#define DMAMUX_CHANNEL_MAP_ENTRY(ch, ...)				\
	{								\
		.dma_dev = ((ch) < DMA0_CHANNEL_COUNT) ?		\
			   DMAMUX_DMA0_DEV : DMAMUX_DMA1_DEV,		\
		.dma_channel = ((ch) < DMA0_CHANNEL_COUNT) ?		\
			       (ch) : ((ch) - DMA0_CHANNEL_COUNT),	\
	}

#define DMAMUX_CHANNEL_MAP(n)						\
	static const struct dmamux_gd32_channel_map			\
		dmamux_gd32_channel_map_##n[] = {			\
		LISTIFY(DT_INST_PROP(n, dma_channels),			\
			DMAMUX_CHANNEL_MAP_ENTRY, (,))			\
	}

#define DMAMUX_GD32_INIT(n)						\
	DMAMUX_CHANNEL_MAP(n);						\
									\
	static const struct dmamux_gd32_config dmamux_gd32_cfg_##n = {	\
		.base = DT_INST_REG_ADDR(n),				\
		.channel_count = DT_INST_PROP(n, dma_channels),		\
		.generator_count = DT_INST_PROP_OR(n, dma_generators, 0),\
		.request_count = DT_INST_PROP(n, dma_requests),		\
		.clkid = DT_INST_CLOCKS_CELL(n, id),			\
		.channel_map = dmamux_gd32_channel_map_##n,		\
	};								\
									\
	ATOMIC_DEFINE(dmamux_gd32_atomic_##n,				\
		      DT_INST_PROP(n, dma_channels));			\
									\
	static struct dmamux_gd32_data dmamux_gd32_data_##n = {		\
		.ctx = {						\
			.magic = DMA_MAGIC,				\
			.atomic = dmamux_gd32_atomic_##n,		\
			.dma_channels = DT_INST_PROP(n, dma_channels),	\
		},							\
	};								\
									\
	DEVICE_DT_INST_DEFINE(n, dmamux_gd32_init, NULL,			\
			      &dmamux_gd32_data_##n,			\
			      &dmamux_gd32_cfg_##n,			\
			      PRE_KERNEL_1,				\
			      CONFIG_DMAMUX_GD32_INIT_PRIORITY,		\
			      &dmamux_gd32_api);

DT_INST_FOREACH_STATUS_OKAY(DMAMUX_GD32_INIT)
