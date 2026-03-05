/*
 * Copyright (c) 2021 BrainCo Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT gd_gd32_spi

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/gd32.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/spi/rtio.h>
#ifdef CONFIG_SPI_GD32_DMA
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/dma/dma_gd32.h>
#endif

#include <gd32_spi.h>

#include <zephyr/logging/log.h>
#include <zephyr/irq.h>
LOG_MODULE_REGISTER(spi_gd32);

#include "spi_context.h"

/* SPI error status mask. */
#define SPI_GD32_ERR_MASK	(SPI_STAT_RXORERR | SPI_STAT_CONFERR | SPI_STAT_CRCERR)

#define GD32_SPI_PSC_MAX	0x7U

#if defined(CONFIG_DCACHE) && !defined(CONFIG_NOCACHE_MEMORY)
/* currently, manual cache coherency management is only done on dummy_rx_tx */
#define GD32_SPI_MANUAL_CACHE_COHERENCY_REQUIRED 1
#else
#define GD32_SPI_MANUAL_CACHE_COHERENCY_REQUIRED 0
#endif /* defined(CONFIG_DCACHE) && !defined(CONFIG_NOCACHE_MEMORY) */

/*
 * Some GD32 series uses different register layout like as GD32H7 series.
 * Define compatibility macros to minimize code changes.
 */
#if defined(CONFIG_SOC_SERIES_GD32H7XX)

#define SPI_DATA_TX(spix) SPI_TDATA(spix)
#define SPI_DATA_RX(spix) SPI_RDATA(spix)

#define SPI_STAT_TX_FLAG SPI_STAT_TP
#define SPI_STAT_RX_FLAG SPI_STAT_RP

#define SPI_TRANSFER_ONGOING(spix) (!(SPI_STAT(spix) & SPI_STAT_TC))

#define SPI_DMA_ENABLE(spix)  (SPI_CFG0(spix) |= (SPI_CFG0_DMATEN | SPI_CFG0_DMAREN))
#define SPI_DMA_DISABLE(spix) (SPI_CFG0(spix) &= ~(SPI_CFG0_DMATEN | SPI_CFG0_DMAREN))

#define SPI_INT_DISABLE_ALL(spix)                                                                  \
	(SPI_INT(spix) &= ~(SPI_INT_RPIE | SPI_INT_TPIE | SPI_INT_RXOREIE | SPI_INT_TXUREIE |      \
			    SPI_INT_CRCERIE | SPI_INT_CONFEIE))

#define SPI_INT_ENABLE_TRANSCEIVE(spix) (SPI_INT(spix) |= (SPI_INT_RPIE | SPI_INT_TPIE))

#define SPI_INT_ENABLE_ERR(spix)                                                                   \
	(SPI_INT(spix) |= (SPI_INT_RXOREIE | SPI_INT_TXUREIE | SPI_INT_CRCERIE | SPI_INT_CONFEIE))

#else /* CONFIG_SOC_SERIES_GD32H7XX */

#define SPI_DATA_TX(spix) SPI_DATA(spix)
#define SPI_DATA_RX(spix) SPI_DATA(spix)

#define SPI_STAT_TX_FLAG SPI_STAT_TBE
#define SPI_STAT_RX_FLAG SPI_STAT_RBNE

#define SPI_TRANSFER_ONGOING(spix)                                                                 \
	(!(SPI_STAT(spix) & SPI_STAT_TBE) || (SPI_STAT(spix) & SPI_STAT_TRANS))

#define SPI_DMA_ENABLE(spix)  (SPI_CTL1(spix) |= (SPI_CTL1_DMATEN | SPI_CTL1_DMAREN))
#define SPI_DMA_DISABLE(spix) (SPI_CTL1(spix) &= ~(SPI_CTL1_DMATEN | SPI_CTL1_DMAREN))

#define SPI_INT_DISABLE_ALL(spix)                                                                  \
	(SPI_CTL1(spix) &= ~(SPI_CTL1_RBNEIE | SPI_CTL1_TBEIE | SPI_CTL1_ERRIE))

#define SPI_INT_ENABLE_TRANSCEIVE(spix) (SPI_CTL1(spix) |= (SPI_CTL1_RBNEIE | SPI_CTL1_TBEIE))

#define SPI_INT_ENABLE_ERR(spix) (SPI_CTL1(spix) |= SPI_CTL1_ERRIE)

#endif /* CONFIG_SOC_SERIES_GD32H7XX */

#ifdef CONFIG_SPI_GD32_DMA

enum spi_gd32_dma_direction {
	RX = 0,
	TX,
	NUM_OF_DIRECTION
};

struct spi_gd32_dma_config {
	const struct device *dev;
	uint32_t channel;
	uint32_t config;
	uint32_t slot;
	uint32_t fifo_threshold;
};

struct spi_gd32_dma_data {
	struct dma_config config;
	struct dma_block_config block;
	uint32_t count;
};

#endif

struct spi_gd32_config {
	uint32_t reg;
	uint16_t clkid;
	struct reset_dt_spec reset;
	const struct pinctrl_dev_config *pcfg;
#ifdef CONFIG_SPI_GD32_DMA
	const struct spi_gd32_dma_config dma[NUM_OF_DIRECTION];
#endif
#ifdef CONFIG_SPI_GD32_INTERRUPT
	void (*irq_configure)();
#endif
};

struct spi_gd32_data {
	struct spi_context ctx;
#ifdef CONFIG_SPI_GD32_DMA
	struct spi_gd32_dma_data dma[NUM_OF_DIRECTION];
#endif
};

#ifdef CONFIG_SPI_GD32_DMA

static uint32_t dummy_tx;
static uint32_t dummy_rx;

static bool spi_gd32_dma_enabled(const struct device *dev)
{
	const struct spi_gd32_config *cfg = dev->config;

	if (cfg->dma[TX].dev && cfg->dma[RX].dev) {
		return true;
	}

	return false;
}

static size_t spi_gd32_dma_enabled_num(const struct device *dev)
{
	return spi_gd32_dma_enabled(dev) ? 2 : 0;
}

#endif

static int spi_gd32_get_err(const struct spi_gd32_config *cfg)
{
	uint32_t stat = SPI_STAT(cfg->reg);

	if (stat & SPI_GD32_ERR_MASK) {
		LOG_ERR("spi%u error status detected, err = %u",
			cfg->reg, stat & (uint32_t)SPI_GD32_ERR_MASK);

		return -EIO;
	}

	return 0;
}

static bool spi_gd32_transfer_ongoing(struct spi_gd32_data *data)
{
	return spi_context_tx_on(&data->ctx) ||
	       spi_context_rx_on(&data->ctx);
}

static int spi_gd32_configure(const struct device *dev,
			      const struct spi_config *config)
{
	struct spi_gd32_data *data = dev->data;
	const struct spi_gd32_config *cfg = dev->config;
	uint32_t bus_freq;

	if (spi_context_configured(&data->ctx, config)) {
		return 0;
	}

	if (SPI_OP_MODE_GET(config->operation) == SPI_OP_MODE_SLAVE) {
		LOG_ERR("Slave mode not supported");
		return -ENOTSUP;
	}

	SPI_CTL0(cfg->reg) &= ~SPI_CTL0_SPIEN;

#if defined(CONFIG_SOC_SERIES_GD32H7XX)
	uint32_t cfg0 = SPI_CFG0(cfg->reg);
	uint32_t cfg1 = SPI_CFG1(cfg->reg);

	/* Master mode and full duplex */
	cfg1 |= SPI_MASTER;

	/* Data size */
	cfg0 &= ~SPI_CFG0_DZ;
	if (SPI_WORD_SIZE_GET(config->operation) == 8) {
		cfg0 |= SPI_DATASIZE_8BIT;
		cfg0 |= SPI_CFG0_BYTEN; /* Enable byte access */
	} else if (SPI_WORD_SIZE_GET(config->operation) == 16) {
		cfg0 |= SPI_DATASIZE_16BIT;
	} else if (SPI_WORD_SIZE_GET(config->operation) == 32) {
		cfg0 |= SPI_DATASIZE_32BIT;
		cfg0 |= SPI_CFG0_WORDEN; /* Enable word access */
	} else {
		return -ENOTSUP;
	}

	/* NSS mode */
	if (spi_cs_is_gpio(config)) {
		cfg1 |= SPI_NSS_SOFT;
	} else {
		cfg1 |= (SPI_NSS_HARD | SPI_CFG1_NSSDRV);
	}
	cfg1 |= SPI_CFG1_NSSDRV;

	/* LSB/MSB */
	if (config->operation & SPI_TRANSFER_LSB) {
		cfg1 |= SPI_ENDIAN_LSB;
	}

	/* Clock polarity and phase */
	if (config->operation & SPI_MODE_CPOL) {
		cfg1 |= SPI_CFG1_CKPL;
	}
	if (config->operation & SPI_MODE_CPHA) {
		cfg1 |= SPI_CFG1_CKPH;
	}

	/* Prescaler */
	(void)clock_control_get_rate(GD32_CLOCK_CONTROLLER, (clock_control_subsys_t)&cfg->clkid,
				     &bus_freq);
	cfg0 &= ~SPI_CFG0_PSC;
	for (uint8_t i = 0U; i <= GD32_SPI_PSC_MAX; i++) {
		bus_freq >>= 1;
		if (bus_freq <= config->frequency) {
			cfg0 |= CFG0_PSC(i);
			break;
		}
	}

	SPI_CFG0(cfg->reg) = cfg0;
	SPI_CFG1(cfg->reg) = cfg1;
	SPI_I2SCTL(cfg->reg) &= ~SPI_I2SCTL_I2SSEL;
#else  /* CONFIG_SOC_SERIES_GD32H7XX */
	SPI_CTL0(cfg->reg) |= SPI_MASTER;
	SPI_CTL0(cfg->reg) &= ~SPI_TRANSMODE_BDTRANSMIT;
#if defined(CONFIG_SOC_SERIES_GD32G5X3)
	SPI_CTL1(cfg->reg) &= ~SPI_CTL1_DZ;
	if (SPI_WORD_SIZE_GET(config->operation) == 8) {
		SPI_CTL1(cfg->reg) |= SPI_FRAMESIZE_8BIT;
		SPI_CTL1(cfg->reg) |= SPI_CTL1_BYTEN; /* Enable byte access to FIFO */
	} else if (SPI_WORD_SIZE_GET(config->operation) == 16) {
		SPI_CTL1(cfg->reg) |= SPI_FRAMESIZE_16BIT;
		SPI_CTL1(cfg->reg) &= ~SPI_CTL1_BYTEN; /* Half-word access */
	} else {
		return -ENOTSUP;
	}
#else
	if (SPI_WORD_SIZE_GET(config->operation) == 8) {
		SPI_CTL0(cfg->reg) |= SPI_FRAMESIZE_8BIT;
	} else {
		SPI_CTL0(cfg->reg) |= SPI_FRAMESIZE_16BIT;
	}
#endif
	/* Reset to hardware NSS mode. */
	SPI_CTL0(cfg->reg) &= ~SPI_CTL0_SWNSSEN;
	if (spi_cs_is_gpio(config)) {
		SPI_CTL0(cfg->reg) |= SPI_CTL0_SWNSSEN;
	} else {
		/*
		 * For single master env,
		 * hardware NSS mode also need to set the NSSDRV bit.
		 */
		SPI_CTL1(cfg->reg) |= SPI_CTL1_NSSDRV;
	}

	SPI_CTL0(cfg->reg) &= ~SPI_CTL0_LF;
	if (config->operation & SPI_TRANSFER_LSB) {
		SPI_CTL0(cfg->reg) |= SPI_CTL0_LF;
	}

	SPI_CTL0(cfg->reg) &= ~SPI_CTL0_CKPL;
	if (config->operation & SPI_MODE_CPOL) {
		SPI_CTL0(cfg->reg) |= SPI_CTL0_CKPL;
	}

	SPI_CTL0(cfg->reg) &= ~SPI_CTL0_CKPH;
	if (config->operation & SPI_MODE_CPHA) {
		SPI_CTL0(cfg->reg) |= SPI_CTL0_CKPH;
	}

	(void)clock_control_get_rate(GD32_CLOCK_CONTROLLER,
				     (clock_control_subsys_t)&cfg->clkid,
				     &bus_freq);

	for (uint8_t i = 0U; i <= GD32_SPI_PSC_MAX; i++) {
		bus_freq = bus_freq >> 1U;
		if (bus_freq <= config->frequency) {
			SPI_CTL0(cfg->reg) &= ~SPI_CTL0_PSC;
			SPI_CTL0(cfg->reg) |= CTL0_PSC(i);
			break;
		}
	}
#endif /* CONFIG_SOC_SERIES_GD32H7XX */

	data->ctx.config = config;

	return 0;
}

static int spi_gd32_frame_exchange(const struct device *dev)
{
	struct spi_gd32_data *data = dev->data;
	const struct spi_gd32_config *cfg = dev->config;
	struct spi_context *ctx = &data->ctx;

#if defined(CONFIG_SOC_SERIES_GD32H7XX)
	uint32_t tx_frame = 0U, rx_frame = 0U;

	/* Set transfer count and start transfer */
	const size_t chunk_len = spi_context_max_continuous_chunk(ctx);

	spi_current_data_num_config(cfg->reg, chunk_len);
	spi_master_transfer_start(cfg->reg, SPI_TRANS_START);
#else
	uint16_t tx_frame = 0U, rx_frame = 0U;
#endif

	while ((SPI_STAT(cfg->reg) & SPI_STAT_TX_FLAG) == 0) {
		/* NOP */
	}

	if (SPI_WORD_SIZE_GET(ctx->config->operation) == 8) {
		if (spi_context_tx_buf_on(ctx)) {
			tx_frame = UNALIGNED_GET((uint8_t *)(data->ctx.tx_buf));
		}
		/* For 8 bits mode, spi will forced SPI_DATA[15:8] to 0. */
		SPI_DATA_TX(cfg->reg) = tx_frame;

		spi_context_update_tx(ctx, 1, 1);
	} else if (SPI_WORD_SIZE_GET(ctx->config->operation) == 16) {
		if (spi_context_tx_buf_on(ctx)) {
			tx_frame = UNALIGNED_GET((uint16_t *)(data->ctx.tx_buf));
		}
		SPI_DATA_TX(cfg->reg) = tx_frame;

		spi_context_update_tx(ctx, 2, 1);
	}
#if defined(CONFIG_SOC_SERIES_GD32H7XX)
	else if (SPI_WORD_SIZE_GET(ctx->config->operation) == 32) {
		if (spi_context_tx_buf_on(ctx)) {
			tx_frame = UNALIGNED_GET((uint32_t *)(data->ctx.tx_buf));
		}
		SPI_DATA_TX(cfg->reg) = tx_frame;
		spi_context_update_tx(ctx, 4, 1);
	}
#endif
	else {
		uint32_t frame_size = SPI_WORD_SIZE_GET(ctx->config->operation);

		LOG_ERR("Unsupported frame size %d bits", frame_size);
		return -ENOTSUP;
	}

	while ((SPI_STAT(cfg->reg) & SPI_STAT_RX_FLAG) == 0) {
		/* NOP */
	}

	if (SPI_WORD_SIZE_GET(data->ctx.config->operation) == 8) {
		/* For 8 bits mode, spi will forced SPI_DATA[15:8] to 0. */
		rx_frame = SPI_DATA_RX(cfg->reg);
		if (spi_context_rx_buf_on(ctx)) {
			UNALIGNED_PUT(rx_frame, (uint8_t *)data->ctx.rx_buf);
		}

		spi_context_update_rx(ctx, 1, 1);
	} else if (SPI_WORD_SIZE_GET(data->ctx.config->operation) == 16) {
		rx_frame = SPI_DATA_RX(cfg->reg);
		if (spi_context_rx_buf_on(ctx)) {
			UNALIGNED_PUT(rx_frame, (uint16_t *)data->ctx.rx_buf);
		}

		spi_context_update_rx(ctx, 2, 1);
	}
#if defined(CONFIG_SOC_SERIES_GD32H7XX)
	else if (SPI_WORD_SIZE_GET(data->ctx.config->operation) == 32) {
		rx_frame = SPI_DATA_RX(cfg->reg);
		if (spi_context_rx_buf_on(ctx)) {
			UNALIGNED_PUT(rx_frame, (uint32_t *)data->ctx.rx_buf);
		}

		spi_context_update_rx(ctx, 4, 1);
	}
#endif
	else {
		uint32_t frame_size = SPI_WORD_SIZE_GET(data->ctx.config->operation);

		LOG_ERR("Unsupported frame size %d bits", frame_size);
		return -ENOTSUP;
	}

	return spi_gd32_get_err(cfg);
}

#ifdef CONFIG_SPI_GD32_DMA
static void spi_gd32_dma_callback(const struct device *dma_dev, void *arg,
				  uint32_t channel, int status);

static uint32_t spi_gd32_dma_setup(const struct device *dev, const uint32_t dir)
{
	const struct spi_gd32_config *cfg = dev->config;
	struct spi_gd32_data *data = dev->data;
	struct dma_config *dma_cfg = &data->dma[dir].config;
	struct dma_block_config *block_cfg = &data->dma[dir].block;
	const struct spi_gd32_dma_config *dma = &cfg->dma[dir];
	int ret;

	memset(dma_cfg, 0, sizeof(struct dma_config));
	memset(block_cfg, 0, sizeof(struct dma_block_config));

	dma_cfg->source_burst_length = 1;
	dma_cfg->dest_burst_length = 1;
	dma_cfg->user_data = (void *)dev;
	dma_cfg->dma_callback = spi_gd32_dma_callback;
	dma_cfg->block_count = 1U;
	dma_cfg->head_block = block_cfg;
	dma_cfg->dma_slot = cfg->dma[dir].slot;
	dma_cfg->channel_priority =
		GD32_DMA_CONFIG_PRIORITY(cfg->dma[dir].config);
	dma_cfg->channel_direction =
		GD32_DMA_CONFIG_DIRECTION(cfg->dma[dir].config);

	if (SPI_WORD_SIZE_GET(data->ctx.config->operation) == 8) {
		dma_cfg->source_data_size = 1;
		dma_cfg->dest_data_size = 1;
	} else if (SPI_WORD_SIZE_GET(data->ctx.config->operation) == 16) {
		dma_cfg->source_data_size = 2;
		dma_cfg->dest_data_size = 2;
	}
#if defined(CONFIG_SOC_SERIES_GD32H7XX)
	else if (SPI_WORD_SIZE_GET(data->ctx.config->operation) == 32) {
		dma_cfg->source_data_size = 4;
		dma_cfg->dest_data_size = 4;
	}
#endif
	else {
		uint32_t frame_size = SPI_WORD_SIZE_GET(data->ctx.config->operation);

		LOG_ERR("Unsupported frame size %d bits", frame_size);
		return -ENOTSUP;
	}

	block_cfg->block_size = spi_context_max_continuous_chunk(&data->ctx);

	if (dir == TX) {
		block_cfg->dest_address = (uint32_t)&SPI_DATA_TX(cfg->reg);
		block_cfg->dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
		if (spi_context_tx_buf_on(&data->ctx)) {
			block_cfg->source_address = (uint32_t)data->ctx.tx_buf;
			block_cfg->source_addr_adj = DMA_ADDR_ADJ_INCREMENT;
		} else {
			block_cfg->source_address = (uint32_t)&dummy_tx;
			block_cfg->source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
		}
	}

	if (dir == RX) {
		block_cfg->source_address = (uint32_t)&SPI_DATA_RX(cfg->reg);
		block_cfg->source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;

		if (spi_context_rx_buf_on(&data->ctx)) {
			block_cfg->dest_address = (uint32_t)data->ctx.rx_buf;
			block_cfg->dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;
		} else {
			block_cfg->dest_address = (uint32_t)&dummy_rx;
			block_cfg->dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
		}
	}

	ret = dma_config(dma->dev, dma->channel, dma_cfg);
	if (ret < 0) {
		LOG_ERR("dma_config %p failed %d\n", dma->dev, ret);
		return ret;
	}

#ifdef CONFIG_DCACHE
	/* SCB_CleanInvalidateDCache(); */
#endif

	ret = dma_start(dma->dev, dma->channel);
	if (ret < 0) {
		LOG_ERR("dma_start %p failed %d\n", dma->dev, ret);
		return ret;
	}

	return 0;
}

static int spi_gd32_start_dma_transceive(const struct device *dev)
{
	const struct spi_gd32_config *cfg = dev->config;
	struct spi_gd32_data *data = dev->data;
	const size_t chunk_len = spi_context_max_continuous_chunk(&data->ctx);
	struct dma_status stat;
	int ret = 0;

#if defined(CONFIG_SOC_SERIES_GD32H7XX)
	/* Disable SPI before configuring transfer count */
	spi_disable(cfg->reg);
	/* Configure SPI transfer count for GD32H7 */
	spi_current_data_num_config(cfg->reg, chunk_len);
	spi_i2s_data_transmit(cfg->reg, 0x5A);

	/* Re-enable SPI after DMA configuration */
	spi_enable(cfg->reg);
#endif

	for (size_t i = 0; i < spi_gd32_dma_enabled_num(dev); i++) {
		dma_get_status(cfg->dma[i].dev, cfg->dma[i].channel, &stat);
		if ((chunk_len != data->dma[i].count) && !stat.busy) {
			ret = spi_gd32_dma_setup(dev, i);
			if (ret < 0) {
				goto on_error;
			}
		}
	}

	/* Enable SPI DMA requests AFTER DMA channels are started */
	SPI_DMA_ENABLE(cfg->reg);

#if defined(CONFIG_SOC_SERIES_GD32H7XX)
	/* Start SPI master transfer for GD32H7 */
	spi_master_transfer_start(cfg->reg, SPI_TRANS_START);
#endif

on_error:
	if (ret < 0) {
		for (size_t i = 0; i < spi_gd32_dma_enabled_num(dev); i++) {
			dma_stop(cfg->dma[i].dev, cfg->dma[i].channel);
		}
	}
	return ret;
}
#endif

static int spi_gd32_transceive_impl(const struct device *dev,
				    const struct spi_config *config,
				    const struct spi_buf_set *tx_bufs,
				    const struct spi_buf_set *rx_bufs,
				    spi_callback_t cb,
				    void *userdata)
{
	struct spi_gd32_data *data = dev->data;
	const struct spi_gd32_config *cfg = dev->config;
	int ret;

	spi_context_lock(&data->ctx, (cb != NULL), cb, userdata, config);

	ret = spi_gd32_configure(dev, config);
	if (ret < 0) {
		goto error;
	}

	SPI_CTL0(cfg->reg) |= SPI_CTL0_SPIEN;

	spi_context_buffers_setup(&data->ctx, tx_bufs, rx_bufs, 1);

	spi_context_cs_control(&data->ctx, true);

#ifdef CONFIG_SPI_GD32_INTERRUPT
#ifdef CONFIG_SPI_GD32_DMA
	if (spi_gd32_dma_enabled(dev)) {
		for (size_t i = 0; i < ARRAY_SIZE(data->dma); i++) {
			data->dma[i].count = 0;
		}

		ret = spi_gd32_start_dma_transceive(dev);
		if (ret < 0) {
			goto dma_error;
		}
	} else
#endif
	{
#if defined(CONFIG_SOC_SERIES_GD32H7XX)
		SPI_STATC(cfg->reg) &= ~(SPI_STAT_RP | SPI_STAT_TP | SPI_GD32_ERR_MASK);
		SPI_INT(cfg->reg) |= (SPI_INT_RPIE | SPI_INT_TPIE | SPI_INT_FEIE);
#else
		SPI_STAT(cfg->reg) &= ~(SPI_STAT_RBNE | SPI_STAT_TBE | SPI_GD32_ERR_MASK);
		SPI_CTL1(cfg->reg) |= (SPI_CTL1_RBNEIE | SPI_CTL1_TBEIE | SPI_CTL1_ERRIE);
#endif
	}
	ret = spi_context_wait_for_completion(&data->ctx);
#else
	do {
		ret = spi_gd32_frame_exchange(dev);
		if (ret < 0) {
			break;
		}
	} while (spi_gd32_transfer_ongoing(data));

#ifdef CONFIG_SPI_ASYNC
	spi_context_complete(&data->ctx, dev, ret);
#endif
#endif

	while (SPI_TRANSFER_ONGOING(cfg->reg)) {
		/* Wait until last frame transfer complete. */
	}

#ifdef CONFIG_SPI_GD32_DMA
dma_error:
	SPI_DMA_DISABLE(cfg->reg);
#endif
	spi_context_cs_control(&data->ctx, false);

	SPI_CTL0(cfg->reg) &= ~(SPI_CTL0_SPIEN);

#ifdef CONFIG_DCACHE
	/* SCB_CleanInvalidateDCache(); */
#endif

error:
	spi_context_release(&data->ctx, ret);

	return ret;
}

static int spi_gd32_transceive(const struct device *dev,
			       const struct spi_config *config,
			       const struct spi_buf_set *tx_bufs,
			       const struct spi_buf_set *rx_bufs)
{
	return spi_gd32_transceive_impl(dev, config, tx_bufs, rx_bufs, NULL, NULL);
}

#ifdef CONFIG_SPI_ASYNC
static int spi_gd32_transceive_async(const struct device *dev,
				     const struct spi_config *config,
				     const struct spi_buf_set *tx_bufs,
				     const struct spi_buf_set *rx_bufs,
				     spi_callback_t cb,
				     void *userdata)
{
	return spi_gd32_transceive_impl(dev, config, tx_bufs, rx_bufs, cb, userdata);
}
#endif

#ifdef CONFIG_SPI_GD32_INTERRUPT

static void spi_gd32_complete(const struct device *dev, int status)
{
	struct spi_gd32_data *data = dev->data;
	const struct spi_gd32_config *cfg = dev->config;

#if defined(CONFIG_SOC_SERIES_GD32H7XX)
	SPI_INT(cfg->reg) &= ~(SPI_INT_RPIE | SPI_INT_TPIE | SPI_INT_FEIE);
#else
	SPI_CTL1(cfg->reg) &= ~(SPI_CTL1_RBNEIE | SPI_CTL1_TBEIE | SPI_CTL1_ERRIE);
#endif

#ifdef CONFIG_SPI_GD32_DMA
	for (size_t i = 0; i < spi_gd32_dma_enabled_num(dev); i++) {
		dma_stop(cfg->dma[i].dev, cfg->dma[i].channel);
	}
#endif

	spi_context_complete(&data->ctx, dev, status);
}

static void spi_gd32_isr(struct device *dev)
{
	const struct spi_gd32_config *cfg = dev->config;
	struct spi_gd32_data *data = dev->data;
	int err = 0;

	err = spi_gd32_get_err(cfg);
	if (err) {
		spi_gd32_complete(dev, err);
		return;
	}

	if (spi_gd32_transfer_ongoing(data)) {
		err = spi_gd32_frame_exchange(dev);
	}

	if (err || !spi_gd32_transfer_ongoing(data)) {
		spi_gd32_complete(dev, err);
	}
}

#endif /* SPI_GD32_INTERRUPT */

#ifdef CONFIG_SPI_GD32_DMA

static bool spi_gd32_chunk_transfer_finished(const struct device *dev)
{
	struct spi_gd32_data *data = dev->data;
	struct spi_gd32_dma_data *dma = data->dma;
	const size_t chunk_len = spi_context_max_continuous_chunk(&data->ctx);

	return (MIN(dma[TX].count, dma[RX].count) >= chunk_len);
}

static bool spi_gd32_chunk_tx_transfer_finished(const struct device *dev)
{
	struct spi_gd32_data *data = dev->data;
	struct spi_gd32_dma_data *dma = data->dma;
	const size_t chunk_len = spi_context_max_continuous_chunk(&data->ctx);

	return (dma[TX].count >= chunk_len);
}

static bool spi_gd32_chunk_rx_transfer_finished(const struct device *dev)
{
	struct spi_gd32_data *data = dev->data;
	struct spi_gd32_dma_data *dma = data->dma;
	const size_t chunk_len = spi_context_max_continuous_chunk(&data->ctx);

	return (dma[RX].count >= chunk_len);
}

static void spi_gd32_dma_callback(const struct device *dma_dev, void *arg,
				  uint32_t channel, int status)
{
	const struct device *dev = (const struct device *)arg;
	const struct spi_gd32_config *cfg = dev->config;
	struct spi_gd32_data *data = dev->data;
	const size_t chunk_len = spi_context_max_continuous_chunk(&data->ctx);
	int err = 0;

	if (status < 0) {
		LOG_ERR("dma:%p ch:%d callback gets error: %d", dma_dev, channel,
			status);
		spi_gd32_complete(dev, status);
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(cfg->dma); i++) {
		if (dma_dev == cfg->dma[i].dev &&
		    channel == cfg->dma[i].channel) {
			data->dma[i].count += chunk_len;
		}
	}

	/* Check transfer finished.
	 * The transmission of this chunk is complete if both the dma[TX].count
	 * and the dma[RX].count reach greater than or equal to the chunk_len.
	 * chunk_len is zero here means the transfer is already complete.
	 */
	if (spi_gd32_chunk_transfer_finished(dev)) {
		if (SPI_WORD_SIZE_GET(data->ctx.config->operation) == 8) {
			spi_context_update_tx(&data->ctx, 1, chunk_len);
			spi_context_update_rx(&data->ctx, 1, chunk_len);
		} else if (SPI_WORD_SIZE_GET(data->ctx.config->operation) == 16) {
			spi_context_update_tx(&data->ctx, 2, chunk_len);
			spi_context_update_rx(&data->ctx, 2, chunk_len);
		}
#if defined(CONFIG_SOC_SERIES_GD32H7XX)
		else if (SPI_WORD_SIZE_GET(data->ctx.config->operation) == 32) {
			spi_context_update_tx(&data->ctx, 4, chunk_len);
			spi_context_update_rx(&data->ctx, 4, chunk_len);
		}
#endif
		else {
			uint32_t frame_size = SPI_WORD_SIZE_GET(data->ctx.config->operation);

			LOG_ERR("Unsupported frame size %d bits", frame_size);
			spi_gd32_complete(dev, -ENOTSUP);
			return;
		}

		if (spi_gd32_transfer_ongoing(data)) {
			/* Next chunk is available, reset the count and
			 * continue processing
			 */
			data->dma[TX].count = 0;
			data->dma[RX].count = 0;
		} else {
			/* All data is processed, complete the process */
			spi_context_complete(&data->ctx, dev, 0);
			return;
		}
	}

	/* Check whether both TX and RX transfers are not finished */
	if (!spi_gd32_chunk_tx_transfer_finished(dev) &&
	    !spi_gd32_chunk_rx_transfer_finished(dev)) {
		SPI_DMA_DISABLE(cfg->reg);
		err = spi_gd32_start_dma_transceive(dev);
		if (err) {
			spi_gd32_complete(dev, err);
		}
	}
}

#endif /* DMA */

static int spi_gd32_release(const struct device *dev,
			    const struct spi_config *config)
{
	struct spi_gd32_data *data = dev->data;

	spi_context_unlock_unconditionally(&data->ctx);

	return 0;
}

static DEVICE_API(spi, spi_gd32_driver_api) = {
	.transceive = spi_gd32_transceive,
#ifdef CONFIG_SPI_ASYNC
	.transceive_async = spi_gd32_transceive_async,
#endif
#ifdef CONFIG_SPI_RTIO
	.iodev_submit = spi_rtio_iodev_default_submit,
#endif
	.release = spi_gd32_release
};

int spi_gd32_init(const struct device *dev)
{
	struct spi_gd32_data *data = dev->data;
	const struct spi_gd32_config *cfg = dev->config;
	int ret;
#ifdef CONFIG_SPI_GD32_DMA
	uint32_t ch_filter;
#endif

	(void)clock_control_on(GD32_CLOCK_CONTROLLER,
			       (clock_control_subsys_t)&cfg->clkid);

	(void)reset_line_toggle_dt(&cfg->reset);

	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret) {
		LOG_ERR("Failed to apply pinctrl state");
		return ret;
	}

#ifdef CONFIG_SPI_GD32_DMA
	if ((cfg->dma[RX].dev && !cfg->dma[TX].dev) ||
	    (cfg->dma[TX].dev && !cfg->dma[RX].dev)) {
		LOG_ERR("DMA must be enabled for both TX and RX channels");
		return -ENODEV;
	}

	for (size_t i = 0; i < spi_gd32_dma_enabled_num(dev); i++) {
		if (!device_is_ready(cfg->dma[i].dev)) {
			LOG_ERR("DMA %s not ready", cfg->dma[i].dev->name);
			return -ENODEV;
		}

		ch_filter = BIT(cfg->dma[i].channel);
		ret = dma_request_channel(cfg->dma[i].dev, &ch_filter);
		if (ret < 0) {
			LOG_ERR("dma_request_channel failed %d", ret);
			return ret;
		}
	}
#endif

	ret = spi_context_cs_configure_all(&data->ctx);
	if (ret < 0) {
		return ret;
	}

#ifdef CONFIG_SPI_GD32_INTERRUPT
	cfg->irq_configure(dev);
#endif

	spi_context_unlock_unconditionally(&data->ctx);

	return 0;
}

/*
 * DMA cell mapping based on binding type:
 * - gd,gd32-dma (3 cells): channel, slot, config
 * - gd,gd32-dma-v1 (4 cells): channel, slot, config, fifo_threshold
 * - gd,gd32-dmamux (3 cells): channel, slot, config
 *
 * Both gd,gd32-dma-v1 and gd,gd32-dmamux have 'slot' cell.
 * Only gd,gd32-dma-v1 has 'fifo_threshold' cell.
 */
#define DMA_IS_V1(idx, dir) \
	DT_NODE_HAS_COMPAT(DT_INST_DMAS_CTLR_BY_NAME(idx, dir), gd_gd32_dma_v1)

#define DMA_IS_DMAMUX(idx, dir) \
	DT_NODE_HAS_COMPAT(DT_INST_DMAS_CTLR_BY_NAME(idx, dir), gd_gd32_dmamux)

/* Get slot value: v1 and dmamux have 'slot' cell, basic gd32-dma (2 cells) does not */
#define DMA_GET_SLOT(idx, dir) \
	COND_CODE_1(DMA_IS_V1(idx, dir), \
		(DT_INST_DMAS_CELL_BY_NAME(idx, dir, slot)), \
		(COND_CODE_1(DMA_IS_DMAMUX(idx, dir), \
			(DT_INST_DMAS_CELL_BY_NAME(idx, dir, slot)), (0))))

#define DMA_INITIALIZER(idx, dir)                                              \
	{                                                                      \
	.dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(idx, dir)),     \
		.channel = DT_INST_DMAS_CELL_BY_NAME(idx, dir, channel),       \
		.slot = DMA_GET_SLOT(idx, dir),                                \
		.config = DT_INST_DMAS_CELL_BY_NAME(idx, dir, config),         \
		.fifo_threshold = COND_CODE_1(DMA_IS_V1(idx, dir),             \
			(DT_INST_DMAS_CELL_BY_NAME(idx, dir, fifo_threshold)), \
			(0)),                                                   \
	}

#define DMAS_DECL(idx)                                                         \
	{                                                                      \
		COND_CODE_1(DT_INST_DMAS_HAS_NAME(idx, rx),                    \
			    (DMA_INITIALIZER(idx, rx)), ({0})),                \
		COND_CODE_1(DT_INST_DMAS_HAS_NAME(idx, tx),                    \
			    (DMA_INITIALIZER(idx, tx)), ({0})),                \
	}

#define GD32_IRQ_CONFIGURE(idx)						   \
	static void spi_gd32_irq_configure_##idx(void)			   \
	{								   \
		IRQ_CONNECT(DT_INST_IRQN(idx), DT_INST_IRQ(idx, priority), \
			    spi_gd32_isr,				   \
			    DEVICE_DT_INST_GET(idx), 0);		   \
		irq_enable(DT_INST_IRQN(idx));				   \
	}

#define GD32_SPI_INIT(idx)						       \
	PINCTRL_DT_INST_DEFINE(idx);					       \
	IF_ENABLED(CONFIG_SPI_GD32_INTERRUPT, (GD32_IRQ_CONFIGURE(idx)));      \
	static struct spi_gd32_data spi_gd32_data_##idx = {		       \
		SPI_CONTEXT_INIT_LOCK(spi_gd32_data_##idx, ctx),	       \
		SPI_CONTEXT_INIT_SYNC(spi_gd32_data_##idx, ctx),	       \
		SPI_CONTEXT_CS_GPIOS_INITIALIZE(DT_DRV_INST(idx), ctx) };      \
	static struct spi_gd32_config spi_gd32_config_##idx = {		       \
		.reg = DT_INST_REG_ADDR(idx),				       \
		.clkid = DT_INST_CLOCKS_CELL(idx, id),			       \
		.reset = RESET_DT_SPEC_INST_GET(idx),			       \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(idx),		       \
		IF_ENABLED(CONFIG_SPI_GD32_DMA, (.dma = DMAS_DECL(idx),))      \
		IF_ENABLED(CONFIG_SPI_GD32_INTERRUPT,			       \
			   (.irq_configure = spi_gd32_irq_configure_##idx)) }; \
	SPI_DEVICE_DT_INST_DEFINE(idx, spi_gd32_init, NULL,			       \
			      &spi_gd32_data_##idx, &spi_gd32_config_##idx,    \
			      POST_KERNEL, CONFIG_SPI_INIT_PRIORITY,	       \
			      &spi_gd32_driver_api);

DT_INST_FOREACH_STATUS_OKAY(GD32_SPI_INIT)
