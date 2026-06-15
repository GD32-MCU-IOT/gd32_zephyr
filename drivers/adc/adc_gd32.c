/*
 * Copyright (c) 2022 BrainCo Inc.
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT gd_gd32_adc

#include <errno.h>

#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/gd32.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/devicetree.h>
#include <zephyr/irq.h>

#include <gd32_adc.h>
#include <gd32_rcu.h>
#if defined(CONFIG_SOC_SERIES_GD32H7XX) || defined(CONFIG_SOC_SERIES_GD32H75E)
#include <gd32_trigsel.h>
#endif

/*
 * GD32H75E HAL uses "ROUTRG"/"ROUTINE" naming where GD32H7XX uses "REGTRG"/"REGULAR".
 * Provide aliases so the shared init code below needs no per-SoC ifdefs.
 */
#if defined(CONFIG_SOC_SERIES_GD32H75E)
#define TRIGSEL_OUTPUT_ADC0_REGTRG TRIGSEL_OUTPUT_ADC0_ROUTRG
#define TRIGSEL_OUTPUT_ADC1_REGTRG TRIGSEL_OUTPUT_ADC1_ROUTRG
#define TRIGSEL_OUTPUT_ADC2_REGTRG TRIGSEL_OUTPUT_ADC2_ROUTRG
#define ADC_REGULAR_CHANNEL        ADC_ROUTINE_CHANNEL
#endif

#ifdef CONFIG_ADC_GD32_DMA
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/dma/dma_gd32.h>
#if defined(CONFIG_CACHE_MANAGEMENT) && defined(CONFIG_DCACHE)
#define ADC_GD32_DMA_CACHE_REQUIRED 1
#include <gd32_cache.h>
#else
#define ADC_GD32_DMA_CACHE_REQUIRED 0
#endif
#endif

#define ADC_CONTEXT_USES_KERNEL_TIMER
#include "adc_context.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(adc_gd32, CONFIG_ADC_LOG_LEVEL);

/**
 * @brief gd32 adc irq have some special cases as below:
 *   1. adc number no larger than 3.
 *   2. adc0 and adc1 share the same irq number.
 *   3. For gd32f4xx, adc2 share the same irq number with adc0 and adc1.
 *
 * To cover this cases, gd32_adc driver use node-label 'adc0', 'adc1' and
 * 'adc2' to handle gd32 adc irq config directly.'
 *
 * @note Sorry for the restriction, But new added gd32 adc node-label must be 'adc0',
 * 'adc1' and 'adc2'.
 */
#define ADC0_NODE		DT_NODELABEL(adc0)
#define ADC1_NODE		DT_NODELABEL(adc1)
#define ADC2_NODE		DT_NODELABEL(adc2)

#define ADC0_ENABLE		DT_NODE_HAS_STATUS_OKAY(ADC0_NODE)
#define ADC1_ENABLE		DT_NODE_HAS_STATUS_OKAY(ADC1_NODE)
#define ADC2_ENABLE		DT_NODE_HAS_STATUS_OKAY(ADC2_NODE)

#ifndef	ADC0
/**
 * @brief The name of gd32 ADC HAL are different between single and multi ADC SoCs.
 * This adjust the single ADC SoC HAL, so we can call gd32 ADC HAL in a common way.
 */
#undef ADC_STAT
#undef ADC_CTL0
#undef ADC_CTL1
#undef ADC_SAMPT0
#undef ADC_SAMPT1
#undef ADC_RSQ2
#undef ADC_RDATA

#define ADC_STAT(adc0)   REG32((adc0) + 0x00000000U)
#define ADC_CTL0(adc0)   REG32((adc0) + 0x00000004U)
#define ADC_CTL1(adc0)   REG32((adc0) + 0x00000008U)
#define ADC_SAMPT0(adc0) REG32((adc0) + 0x0000000CU)
#define ADC_SAMPT1(adc0) REG32((adc0) + 0x00000010U)
#define ADC_RSQ2(adc0)   REG32((adc0) + 0x00000034U)
#define ADC_RDATA(adc0)  REG32((adc0) + 0x0000004CU)
#endif

#if defined(CONFIG_SOC_SERIES_GD32F50X)
/*
 * GD32F50x multi-ADC HAL uses routine-sequence naming for the end-of-conversion
 * flag and its interrupt enable. Map the names used by this driver to them.
 */
#define ADC_STAT_EOC   ADC_STAT_EORC
#define ADC_CTL0_EOCIE ADC_CTL0_EORCIE
#endif

#define SPT_WIDTH   3U
#define SAMPT1_SIZE 10U

#if defined(CONFIG_SOC_SERIES_GD32H7XX) || defined(CONFIG_SOC_SERIES_GD32H75E)
/*
 * GD32H7XX does not use the SAMPT0/SAMPT1 sample-time lookup tables. The
 * sample time is encoded per sequence entry in the regular sequence
 * register (RSQx) RSMPn field, so no acq_time table is required here.
 */
#elif defined(CONFIG_SOC_SERIES_GD32F4XX)
#define SMP_TIME(x) ADC_SAMPLETIME_##x

static const uint16_t acq_time_tbl[8] = {3, 15, 28, 56, 84, 112, 144, 480};
static const uint32_t table_samp_time[] = {
	SMP_TIME(3),
	SMP_TIME(15),
	SMP_TIME(28),
	SMP_TIME(56),
	SMP_TIME(84),
	SMP_TIME(112),
	SMP_TIME(144),
	SMP_TIME(480)
};
#elif defined(CONFIG_SOC_SERIES_GD32L23X)
#define SMP_TIME(x) ADC_SAMPLETIME_##x##POINT5

static const uint16_t acq_time_tbl[8] = {3, 8, 14, 29, 42, 56, 72, 240};
static const uint32_t table_samp_time[] = {
	SMP_TIME(2),
	SMP_TIME(7),
	SMP_TIME(13),
	SMP_TIME(28),
	SMP_TIME(41),
	SMP_TIME(55),
	SMP_TIME(71),
	SMP_TIME(239),
};
#elif defined(CONFIG_SOC_SERIES_GD32A50X)
#define SMP_TIME(x) ADC_SAMPLETIME_##x##POINT5

static const uint16_t acq_time_tbl[8] = {3, 15, 28, 56, 84, 112, 144, 480};
static const uint32_t table_samp_time[] = {
	SMP_TIME(2),
	SMP_TIME(14),
	SMP_TIME(27),
	SMP_TIME(55),
	SMP_TIME(83),
	SMP_TIME(111),
	SMP_TIME(143),
	SMP_TIME(479)
};
#else
#define SMP_TIME(x) ADC_SAMPLETIME_##x##POINT5

static const uint16_t acq_time_tbl[8] = {2, 8, 14, 29, 42, 56, 72, 240};
static const uint32_t table_samp_time[] = {
	SMP_TIME(1),
	SMP_TIME(7),
	SMP_TIME(13),
	SMP_TIME(28),
	SMP_TIME(41),
	SMP_TIME(55),
	SMP_TIME(71),
	SMP_TIME(239)
};
#endif

#ifdef CONFIG_ADC_GD32_DMA
enum adc_gd32_dma_direction {
	RX = 0,
	TX,
	NUM_OF_DIRECTION
};

struct adc_gd32_dma_config {
	const struct device *dev;
	uint32_t channel;
	uint32_t config;
	uint32_t slot;
	uint32_t fifo_threshold;
};

struct adc_gd32_dma_data {
	struct dma_config config;
	struct dma_block_config block;
	uint32_t count;
};
#endif /* CONFIG_ADC_GD32_DMA */

struct adc_gd32_config {
	uint32_t reg;
#if defined(CONFIG_SOC_SERIES_GD32F3X0) || defined(CONFIG_SOC_SERIES_GD32F50X)
	uint32_t rcu_clock_source;
#endif
	uint16_t clkid;
	struct reset_dt_spec reset;
	uint8_t channels;
	const struct pinctrl_dev_config *pcfg;
	uint8_t irq_num;
	void (*irq_config_func)(void);
#if defined(CONFIG_SOC_SERIES_GD32H7XX) || defined(CONFIG_SOC_SERIES_GD32H75E)
	uint32_t trigger_select;
#endif
#ifdef CONFIG_ADC_GD32_DMA
	const struct adc_gd32_dma_config dma[NUM_OF_DIRECTION];
#endif
};

struct adc_gd32_data {
	struct adc_context ctx;
	const struct device *dev;
	uint16_t *buffer;
	uint16_t *repeat_buffer;
#ifdef CONFIG_ADC_GD32_DMA
	struct adc_gd32_dma_data dma[NUM_OF_DIRECTION];
#endif
};

#ifdef CONFIG_ADC_GD32_DMA
static void adc_gd32_dma_callback(const struct device *dma_dev, void *arg,
				  uint32_t channel, int status);

/**
 * @brief Configure and start the regular-group RX DMA channel.
 *
 * The ADC end-of-conversion (EOC) flag is cleared by the DMA read of the
 * ADC_RDATA register, so the result is moved to the user buffer without
 * CPU intervention and the DMA completion callback finalizes the sampling.
 */
static int adc_gd32_dma_setup(const struct device *dev, uint32_t dir)
{
	const struct adc_gd32_config *cfg = dev->config;
	struct adc_gd32_data *data = dev->data;
	struct dma_config *dma_cfg = &data->dma[dir].config;
	struct dma_block_config *block_cfg = &data->dma[dir].block;
	const struct adc_gd32_dma_config *dma = &cfg->dma[dir];
	int ret;

	memset(dma_cfg, 0, sizeof(struct dma_config));
	memset(block_cfg, 0, sizeof(struct dma_block_config));

	dma_cfg->source_burst_length = 1;
	dma_cfg->dest_burst_length = 1;
	dma_cfg->user_data = (void *)dev;
	dma_cfg->dma_callback = adc_gd32_dma_callback;
	dma_cfg->block_count = 1U;
	dma_cfg->head_block = block_cfg;
	dma_cfg->dma_slot = dma->slot;
	dma_cfg->channel_priority = GD32_DMA_CONFIG_PRIORITY(dma->config);
	dma_cfg->channel_direction = PERIPHERAL_TO_MEMORY;
	dma_cfg->source_data_size = 2;
	dma_cfg->dest_data_size = 2;
	dma_cfg->cyclic = 1;

	block_cfg->block_size = 1;
	block_cfg->source_address = (uint32_t)&ADC_RDATA(cfg->reg);
	block_cfg->source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
	block_cfg->dest_address = (uint32_t)data->buffer;
	block_cfg->dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;

	ret = dma_config(dma->dev, dma->channel, dma_cfg);
	if (ret < 0) {
		LOG_ERR("dma_config %p failed %d", dma->dev, ret);
		return ret;
	}

	ret = dma_start(dma->dev, dma->channel);
	if (ret < 0) {
		LOG_ERR("dma_start %p failed %d", dma->dev, ret);
		return ret;
	}

	return 0;
}
#endif /* CONFIG_ADC_GD32_DMA */

static void adc_gd32_isr(const struct device *dev)
{
	struct adc_gd32_data *data = dev->data;
	const struct adc_gd32_config *cfg = dev->config;

	if (ADC_STAT(cfg->reg) & ADC_STAT_EOC) {
		*data->buffer++ = ADC_RDATA(cfg->reg);

		/* Disable EOC interrupt. */
		ADC_CTL0(cfg->reg) &= ~ADC_CTL0_EOCIE;
		/* Clear EOC bit. */
		ADC_STAT(cfg->reg) &= ~ADC_STAT_EOC;

		adc_context_on_sampling_done(&data->ctx, dev);
	}
}

static void adc_context_start_sampling(struct adc_context *ctx)
{
	struct adc_gd32_data *data = CONTAINER_OF(ctx, struct adc_gd32_data, ctx);
	const struct device *dev = data->dev;
	const struct adc_gd32_config *cfg = dev->config;

	data->repeat_buffer = data->buffer;

	/* Make sure no stale end-of-conversion flag is set. */
	ADC_STAT(cfg->reg) &= ~ADC_STAT_EOC;

#if defined(CONFIG_SOC_SERIES_GD32F50X)
	/* Ensure ADC is powered and stabilized before triggering. */
	ADC_CTL1(cfg->reg) |= ADC_CTL1_ADCON;
	k_busy_wait(20);
#endif

#ifdef CONFIG_ADC_GD32_DMA
	{
		int ret = adc_gd32_dma_setup(dev, RX);

		if (ret < 0) {
			LOG_ERR("DMA setup failed: %d", ret);
			adc_context_complete(&data->ctx, ret);
			return;
		}
	}
#endif

	/* Enable EOC interrupt */
	ADC_CTL0(cfg->reg) |= ADC_CTL0_EOCIE;

	/* Set ADC software conversion trigger. */
	ADC_CTL1(cfg->reg) |= ADC_CTL1_SWRCST;
}

static void adc_context_update_buffer_pointer(struct adc_context *ctx,
					      bool repeat_sampling)
{
	struct adc_gd32_data *data = CONTAINER_OF(ctx, struct adc_gd32_data, ctx);

	if (repeat_sampling) {
		data->buffer = data->repeat_buffer;
	}
}

static inline void adc_gd32_calibration(const struct adc_gd32_config *cfg)
{
#if defined(CONFIG_SOC_SERIES_GD32F50X)
	/* GD32F50x ADC has no calibration sequence. */
	ARG_UNUSED(cfg);
#else
	ADC_CTL1(cfg->reg) |= ADC_CTL1_RSTCLB;
	/* Wait for calibration registers initialized. */
	while (ADC_CTL1(cfg->reg) & ADC_CTL1_RSTCLB) {
	}

	ADC_CTL1(cfg->reg) |= ADC_CTL1_CLB;
	/* Wait for calibration complete. */
	while (ADC_CTL1(cfg->reg) & ADC_CTL1_CLB) {
	}
#endif
}

static int adc_gd32_configure_sampt(const struct adc_gd32_config *cfg,
				    uint8_t channel, uint16_t acq_time)
{
#if defined(CONFIG_SOC_SERIES_GD32H7XX) || defined(CONFIG_SOC_SERIES_GD32H75E)
	ARG_UNUSED(cfg);
	ARG_UNUSED(channel);

	/*
	 * On GD32H7XX the sample time is part of the regular sequence
	 * register and is programmed together with the channel number in
	 * adc_gd32_start_read(). Only the default acquisition time is
	 * supported here.
	 */
	if (acq_time != ADC_ACQ_TIME_DEFAULT) {
		return -ENOTSUP;
	}

	return 0;
#else
	uint8_t index = 0, offset;

	if (acq_time != ADC_ACQ_TIME_DEFAULT) {
		/* Acquisition time unit is adc clock cycle. */
		if (ADC_ACQ_TIME_UNIT(acq_time) != ADC_ACQ_TIME_TICKS) {
			return -EINVAL;
		}

		for ( ; index < ARRAY_SIZE(acq_time_tbl); index++) {
			if (ADC_ACQ_TIME_VALUE(acq_time) <= acq_time_tbl[index]) {
				break;
			}
		}

		if (ADC_ACQ_TIME_VALUE(acq_time) != acq_time_tbl[index]) {
			return -ENOTSUP;
		}
	}

	if (channel < SAMPT1_SIZE) {
		offset = SPT_WIDTH * channel;
		ADC_SAMPT1(cfg->reg) &= ~(ADC_SAMPTX_SPTN << offset);
		ADC_SAMPT1(cfg->reg) |= table_samp_time[index] << offset;
	} else {
		offset = SPT_WIDTH * (channel - SAMPT1_SIZE);
		ADC_SAMPT0(cfg->reg) &= ~(ADC_SAMPTX_SPTN << offset);
		ADC_SAMPT0(cfg->reg) |= table_samp_time[index] << offset;
	}

	return 0;
#endif
}

static int adc_gd32_channel_setup(const struct device *dev,
				  const struct adc_channel_cfg *chan_cfg)
{
	const struct adc_gd32_config *cfg = dev->config;

	if (chan_cfg->gain != ADC_GAIN_1) {
		LOG_ERR("Gain is not valid");
		return -ENOTSUP;
	}

	if (chan_cfg->reference != ADC_REF_INTERNAL) {
		LOG_ERR("Reference is not valid");
		return -ENOTSUP;
	}

	if (chan_cfg->differential) {
		LOG_ERR("Differential sampling not supported");
		return -ENOTSUP;
	}

	if (chan_cfg->channel_id >= cfg->channels) {
		LOG_ERR("Invalid channel (%u)", chan_cfg->channel_id);
		return -EINVAL;
	}

	return adc_gd32_configure_sampt(cfg, chan_cfg->channel_id,
					chan_cfg->acquisition_time);
}

static int adc_gd32_start_read(const struct device *dev,
			       const struct adc_sequence *sequence)
{
	struct adc_gd32_data *data = dev->data;
	const struct adc_gd32_config *cfg = dev->config;
	uint8_t resolution_id;
	uint32_t index;

	index = find_lsb_set(sequence->channels) - 1;
	if (sequence->channels > BIT(index)) {
		LOG_ERR("Only single channel supported");
		return -ENOTSUP;
	}

#if defined(CONFIG_SOC_SERIES_GD32H7XX) || defined(CONFIG_SOC_SERIES_GD32H75E)
	switch (sequence->resolution) {
	case 14U:
		resolution_id = 0U;
		break;
	case 12U:
		resolution_id = 1U;
		break;
	case 10U:
		resolution_id = 2U;
		break;
	case 8U:
		resolution_id = 3U;
		break;
	default:
		return -EINVAL;
	}
#else
	switch (sequence->resolution) {
	case 12U:
		resolution_id = 0U;
		break;
	case 10U:
		resolution_id = 1U;
		break;
	case 8U:
		resolution_id = 2U;
		break;
	case 6U:
		resolution_id = 3U;
		break;
	default:
		return -EINVAL;
	}
#endif

#if defined(CONFIG_SOC_SERIES_GD32F4XX) || \
	defined(CONFIG_SOC_SERIES_GD32H7XX) || \
	defined(CONFIG_SOC_SERIES_GD32H75E) || \
	defined(CONFIG_SOC_SERIES_GD32F3X0) || \
	defined(CONFIG_SOC_SERIES_GD32L23X)
	ADC_CTL0(cfg->reg) &= ~ADC_CTL0_DRES;
	ADC_CTL0(cfg->reg) |= CTL0_DRES(resolution_id);
#elif defined(CONFIG_SOC_SERIES_GD32F403) || defined(CONFIG_SOC_SERIES_GD32A50X) || \
	defined(CONFIG_SOC_SERIES_GD32F50X)
	ADC_OVSAMPCTL(cfg->reg) &= ~ADC_OVSAMPCTL_DRES;
	ADC_OVSAMPCTL(cfg->reg) |= OVSAMPCTL_DRES(resolution_id);
#elif defined(CONFIG_SOC_SERIES_GD32VF103)
	ADC_OVSCR(cfg->reg) &= ~ADC_OVSCR_DRES;
	ADC_OVSCR(cfg->reg) |= OVSCR_DRES(resolution_id);
#endif

	if (sequence->calibrate) {
		adc_gd32_calibration(cfg);
	}

	/* Single conversion mode with regular group. */
#if defined(CONFIG_SOC_SERIES_GD32H7XX) || defined(CONFIG_SOC_SERIES_GD32H75E)
	ADC_RSQ8(cfg->reg) &= ~(ADC_RSQX_RSQN | ADC_RSQX_RSMPN);
	ADC_RSQ8(cfg->reg) = index;
#else
	ADC_RSQ2(cfg->reg) &= ~ADC_RSQX_RSQN;
	ADC_RSQ2(cfg->reg) = index;
#endif

	data->buffer = sequence->buffer;

	adc_context_start_read(&data->ctx, sequence);

	return adc_context_wait_for_completion(&data->ctx);
}

static int adc_gd32_read(const struct device *dev,
			 const struct adc_sequence *sequence)
{
	struct adc_gd32_data *data = dev->data;
	int error;

#ifdef CONFIG_ADC_GD32_DMA
#if ADC_GD32_DMA_CACHE_REQUIRED
	/*
	 * Clean and invalidate the destination buffer so no dirty cache line
	 * is written back over the data the DMA will deposit there.
	 */
	gd32_cache_flush_and_invd_buf(sequence->buffer, sequence->buffer_size);
#endif
#endif

	adc_context_lock(&data->ctx, false, NULL);
	error = adc_gd32_start_read(dev, sequence);
	adc_context_release(&data->ctx, error);

	return error;
}

#ifdef CONFIG_ADC_ASYNC
static int adc_gd32_read_async(const struct device *dev,
			       const struct adc_sequence *sequence,
			       struct k_poll_signal *async)
{
	struct adc_gd32_data *data = dev->data;
	int error;

	adc_context_lock(&data->ctx, true, async);
	error = adc_gd32_start_read(dev, sequence);
	adc_context_release(&data->ctx, error);

	return error;
}
#endif /* CONFIG_ADC_ASYNC */

/* adc_gd32_driver_api is defined per-instance in ADC_GD32_INIT to allow
 * ref_internal to be read from the devicetree vref-mv property.
 */

#ifdef CONFIG_ADC_GD32_DMA
static void adc_gd32_dma_callback(const struct device *dma_dev, void *arg,
				  uint32_t channel, int status)
{
	const struct device *dev = (const struct device *)arg;
	struct adc_gd32_data *data = dev->data;

	ARG_UNUSED(dma_dev);
	ARG_UNUSED(channel);

	if (status < 0) {
		LOG_ERR("DMA transfer error: %d", status);
		adc_context_complete(&data->ctx, status);
		return;
	}

#if ADC_GD32_DMA_CACHE_REQUIRED
	/* Make the DMA-written conversion result visible to the CPU. */
	gd32_cache_invalidate_buf(data->buffer, sizeof(*data->buffer));
#endif

	LOG_DBG("DMA done");
	adc_context_on_sampling_done(&data->ctx, dev);
}
#endif /* CONFIG_ADC_GD32_DMA */

static int adc_gd32_init(const struct device *dev)
{
	struct adc_gd32_data *data = dev->data;
	const struct adc_gd32_config *cfg = dev->config;
	int ret;

	data->dev = dev;

	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		return ret;
	}

#if defined(CONFIG_SOC_SERIES_GD32F3X0) || defined(CONFIG_SOC_SERIES_GD32F50X)
	/* Select adc clock source and its prescaler. */
	rcu_adc_clock_config(cfg->rcu_clock_source);
#endif

	(void)clock_control_on(GD32_CLOCK_CONTROLLER,
			       (clock_control_subsys_t)&cfg->clkid);

	(void)reset_line_toggle_dt(&cfg->reset);

#if defined(CONFIG_SOC_SERIES_GD32H7XX) || defined(CONFIG_SOC_SERIES_GD32H75E)
	/* Select ADC clock: synchronous from AHB clock (HCLK) divided by 8. */
	adc_clock_config(cfg->reg, ADC_CLK_SYNC_HCLK_DIV8);

	if (cfg->trigger_select == 0U) {
		/* Software trigger: disable hardware external trigger. */
		ADC_CTL1(cfg->reg) &= ~ADC_CTL1_ETMRC;
		ADC_CTL1(cfg->reg) |= EXTERNAL_TRIGGER_DISABLE << 28U;
	} else {
		/* Route a TRIGSEL input source to the ADC regular trigger. */
		rcu_periph_clock_enable(RCU_TRIGSEL);

		if (cfg->reg == ADC0) {
			trigsel_init(TRIGSEL_OUTPUT_ADC0_REGTRG,
				     (trigsel_source_enum)cfg->trigger_select);
		} else if (cfg->reg == ADC1) {
			trigsel_init(TRIGSEL_OUTPUT_ADC1_REGTRG,
				     (trigsel_source_enum)cfg->trigger_select);
		} else if (cfg->reg == ADC2) {
			trigsel_init(TRIGSEL_OUTPUT_ADC2_REGTRG,
				     (trigsel_source_enum)cfg->trigger_select);
		}

		adc_external_trigger_config(cfg->reg, ADC_REGULAR_CHANNEL,
					    EXTERNAL_TRIGGER_RISING);
	}
#endif

#if defined(CONFIG_SOC_SERIES_GD32F4XX) || \
	defined(CONFIG_SOC_SERIES_GD32F3X0) || \
	defined(CONFIG_SOC_SERIES_GD32L23X)
	/* Set SWRCST as the regular channel external trigger. */
	ADC_CTL1(cfg->reg) &= ~ADC_CTL1_ETSRC;
	ADC_CTL1(cfg->reg) |= CTL1_ETSRC(7);

	/* Enable external trigger for regular channel. */
	ADC_CTL1(cfg->reg) |= ADC_CTL1_ETERC;
#endif

#ifdef CONFIG_SOC_SERIES_GD32A50X
	ADC_CTL1(cfg->reg) |= ADC_CTL1_ETSRC;
	ADC_CTL1(cfg->reg) |= ADC_CTL1_ETERC;
#endif

#ifdef CONFIG_ADC_GD32_DMA
	/* Enable ADC DMA mode and keep issuing DMA requests after the last
	 * conversion of the regular sequence.
	 */
	if (cfg->reg == ADC0) {
		adc_dma_request_after_last_enable(ADC0);
		adc_dma_mode_enable(ADC0);
	}
#if defined(ADC1)
	else if (cfg->reg == ADC1) {
		adc_dma_request_after_last_enable(ADC1);
		adc_dma_mode_enable(ADC1);
	}
#endif
#if defined(ADC2)
	else if (cfg->reg == ADC2) {
		adc_dma_request_after_last_enable(ADC2);
		adc_dma_mode_enable(ADC2);
	}
#endif
#endif /* CONFIG_ADC_GD32_DMA */

	/* Enable ADC */
	ADC_CTL1(cfg->reg) |= ADC_CTL1_ADCON;

	adc_gd32_calibration(cfg);

	cfg->irq_config_func();

	adc_context_unlock_unconditionally(&data->ctx);

	return 0;
}

#define HANDLE_SHARED_IRQ(n, active_irq)							\
	static const struct device *const dev_##n = DEVICE_DT_INST_GET(n);			\
	const struct adc_gd32_config *cfg_##n = dev_##n->config;				\
												\
	if ((cfg_##n->irq_num == active_irq) &&							\
		(ADC_CTL0(cfg_##n->reg) & ADC_CTL0_EOCIE)) {					\
		adc_gd32_isr(dev_##n);								\
	}

static void adc_gd32_global_irq_handler(const struct device *dev)
{
	const struct adc_gd32_config *cfg = dev->config;

	LOG_DBG("global irq handler: %u", cfg->irq_num);

	DT_INST_FOREACH_STATUS_OKAY_VARGS(HANDLE_SHARED_IRQ, (cfg->irq_num));
}

static void adc_gd32_global_irq_cfg(void)
{
	static bool global_irq_init = true;

	if (!global_irq_init) {
		return;
	}

	global_irq_init = false;

#if ADC0_ENABLE
	/* Shared irq config default to adc0. */
	IRQ_CONNECT(DT_IRQN(ADC0_NODE),
		DT_IRQ(ADC0_NODE, priority),
		adc_gd32_global_irq_handler,
		DEVICE_DT_GET(ADC0_NODE),
		0);
	irq_enable(DT_IRQN(ADC0_NODE));
#elif ADC1_ENABLE
	IRQ_CONNECT(DT_IRQN(ADC1_NODE),
		DT_IRQ(ADC1_NODE, priority),
		adc_gd32_global_irq_handler,
		DEVICE_DT_GET(ADC1_NODE),
		0);
	irq_enable(DT_IRQN(ADC1_NODE));
#endif

#if (ADC0_ENABLE || ADC1_ENABLE) && \
	(defined(CONFIG_SOC_SERIES_GD32F4XX) || \
	 defined(CONFIG_SOC_SERIES_GD32H7XX) || \
	 defined(CONFIG_SOC_SERIES_GD32H75E))
	/* gd32f4xx/gd32h7xx adc2 share the same irq number with adc0 and adc1. */
#elif ADC2_ENABLE
	IRQ_CONNECT(DT_IRQN(ADC2_NODE),
		DT_IRQ(ADC2_NODE, priority),
		adc_gd32_global_irq_handler,
		DEVICE_DT_GET(ADC2_NODE),
		0);
	irq_enable(DT_IRQN(ADC2_NODE));
#endif
}

#if defined(CONFIG_SOC_SERIES_GD32F3X0) || defined(CONFIG_SOC_SERIES_GD32F50X)
#define ADC_CLOCK_SOURCE(n)									\
	.rcu_clock_source = DT_INST_PROP(n, rcu_clock_source)
#else
#define ADC_CLOCK_SOURCE(n)
#endif

#if defined(CONFIG_SOC_SERIES_GD32H7XX) || defined(CONFIG_SOC_SERIES_GD32H75E)
#define ADC_TRIGGER_SELECT(n)									\
	.trigger_select = DT_INST_PROP(n, trigger_select),
#else
#define ADC_TRIGGER_SELECT(n)
#endif

#ifdef CONFIG_ADC_GD32_DMA
/*
 * DMA cell mapping based on binding type:
 * - gd,gd32-dma (2 cells): channel, config
 * - gd,gd32-dma-v1 (4 cells): channel, slot, config, fifo_threshold
 * - gd,gd32-dmamux (3 cells): channel, slot, config
 *
 * Both gd,gd32-dma-v1 and gd,gd32-dmamux have 'slot' cell.
 * Only gd,gd32-dma-v1 has 'fifo_threshold' cell.
 * Standard gd,gd32-dma does NOT have 'slot' cell.
 */
#define DMA_IS_V1(idx, dir) \
	DT_NODE_HAS_COMPAT(DT_INST_DMAS_CTLR_BY_NAME(idx, dir), gd_gd32_dma_v1)

#define DMA_IS_DMAMUX(idx, dir) \
	DT_NODE_HAS_COMPAT(DT_INST_DMAS_CTLR_BY_NAME(idx, dir), gd_gd32_dmamux)

#define DMA_GET_SLOT(idx, dir) \
	COND_CODE_1(DMA_IS_V1(idx, dir), \
		(DT_INST_DMAS_CELL_BY_NAME(idx, dir, slot)), \
		(COND_CODE_1(DMA_IS_DMAMUX(idx, dir), \
			(DT_INST_DMAS_CELL_BY_NAME(idx, dir, slot)), (0))))

#define DMA_INITIALIZER(idx, dir)							\
	{										\
		.dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(idx, dir)),		\
		.channel = DT_INST_DMAS_CELL_BY_NAME(idx, dir, channel),		\
		.slot = DMA_GET_SLOT(idx, dir),						\
		.config = DT_INST_DMAS_CELL_BY_NAME(idx, dir, config),			\
		.fifo_threshold = COND_CODE_1(DMA_IS_V1(idx, dir),			\
			(DT_INST_DMAS_CELL_BY_NAME(idx, dir, fifo_threshold)), (0)),	\
	}

#define DMAS_DECL(idx)									\
	{										\
		COND_CODE_1(DT_INST_DMAS_HAS_NAME(idx, rx),				\
			    (DMA_INITIALIZER(idx, rx)), ({0})),				\
	}

#define ADC_DMA_DECL(n)	.dma = DMAS_DECL(n),
#else
#define ADC_DMA_DECL(n)
#endif /* CONFIG_ADC_GD32_DMA */

#define ADC_GD32_INIT(n)									\
	PINCTRL_DT_INST_DEFINE(n);								\
	static struct adc_gd32_data adc_gd32_data_##n = {					\
		ADC_CONTEXT_INIT_TIMER(adc_gd32_data_##n, ctx),					\
		ADC_CONTEXT_INIT_LOCK(adc_gd32_data_##n, ctx),					\
		ADC_CONTEXT_INIT_SYNC(adc_gd32_data_##n, ctx),					\
	};											\
	const static struct adc_gd32_config adc_gd32_config_##n = {				\
		.reg = DT_INST_REG_ADDR(n),							\
		.clkid = DT_INST_CLOCKS_CELL(n, id),						\
		.reset = RESET_DT_SPEC_INST_GET(n),						\
		.channels = DT_INST_PROP(n, channels),						\
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),					\
		.irq_num = DT_INST_IRQN(n),							\
		.irq_config_func = adc_gd32_global_irq_cfg,					\
		ADC_TRIGGER_SELECT(n)								\
		ADC_DMA_DECL(n)									\
		ADC_CLOCK_SOURCE(n)								\
	};											\
	static DEVICE_API(adc, adc_gd32_driver_api_##n) = {			\
		.channel_setup = adc_gd32_channel_setup,					\
		.read = adc_gd32_read,							\
	IF_ENABLED(CONFIG_ADC_ASYNC, (.read_async = adc_gd32_read_async,))\
		.ref_internal = DT_INST_PROP(n, vref_mv),					\
	};											\
	DEVICE_DT_INST_DEFINE(n,								\
			      adc_gd32_init, NULL,						\
			      &adc_gd32_data_##n, &adc_gd32_config_##n,				\
			      POST_KERNEL, CONFIG_ADC_INIT_PRIORITY,				\
			      &adc_gd32_driver_api_##n);				\

DT_INST_FOREACH_STATUS_OKAY(ADC_GD32_INIT)
