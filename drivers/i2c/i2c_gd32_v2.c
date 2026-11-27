/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Driver for the GD32 "ADD" I2C IP (TIMING/STAT/STATC/RDATA/TDATA register set).
 * The legacy GD32 I2C IP (STAT0/STAT1/CKCFG) is handled by i2c_gd32.c.
 */

#define DT_DRV_COMPAT gd_gd32_i2c_v2

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/gd32.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/toolchain.h>

#ifdef CONFIG_I2C_GD32_DMA
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/dma/dma_gd32.h>
#endif

#include <gd32_i2c.h>

LOG_MODULE_REGISTER(i2c_gd32_v2, CONFIG_I2C_LOG_LEVEL);

#include "i2c-priv.h"

/*
 * HAL naming map.
 *
 * The driver body uses the plain I2C_* / i2c_*(periph, ...) HAL names.
 * Most ADD-IP series provide them natively; GD32M53x gets them from the
 * soc.h single-instance shim (CONFIG_I2C_GD32_V2). Only series whose ADD
 * IP uses different HAL names need a branch: GD32F527 (I2C_ADD_* /
 * i2c_add_*()) and GD32E50x/E51x (I2C2_* / i2c2_*()). On these mixed-IP
 * series the plain macros belong to the legacy IP with different bit
 * values, so they are #undef'd and remapped here. HAL .c files are
 * compiled separately and are not affected.
 * Add any newly used HAL symbol to every branch.
 */
#if defined(CONFIG_SOC_SERIES_GD32F527)

#undef I2C_CTL1
#undef I2C_STAT
#undef I2C_RDATA
#undef I2C_TDATA
#undef I2C_TIMING
#undef I2C_CTL1_AUTOEND
#undef I2C_STAT_TBE
#undef I2C_STAT_TI
#undef I2C_STAT_RBNE
#undef I2C_STAT_ADDSEND
#undef I2C_STAT_NACK
#undef I2C_STAT_STPDET
#undef I2C_STAT_TC
#undef I2C_STAT_TCR
#undef I2C_STAT_BERR
#undef I2C_STAT_LOSTARB
#undef I2C_STAT_I2CBSY
#undef I2C_STAT_TR
#undef I2C_INT_ERR
#undef I2C_INT_TC
#undef I2C_INT_STPDET
#undef I2C_INT_NACK
#undef I2C_INT_ADDM
#undef I2C_INT_RBNE
#undef I2C_INT_TI
#undef I2C_FLAG_ADDSEND
#undef I2C_FLAG_NACK
#undef I2C_FLAG_STPDET
#undef I2C_FLAG_TC
#undef I2C_FLAG_BERR
#undef I2C_FLAG_LOSTARB
#undef I2C_DMA_TRANSMIT
#undef I2C_DMA_RECEIVE
#undef I2C_MASTER_TRANSMIT
#undef I2C_MASTER_RECEIVE
#undef I2C_ADDFORMAT_7BITS

#define I2C_CTL1(periph)   I2C_ADD_CTL1(periph)
#define I2C_STAT(periph)   I2C_ADD_STAT(periph)
#define I2C_RDATA(periph)  I2C_ADD_RDATA(periph)
#define I2C_TDATA(periph)  I2C_ADD_TDATA(periph)
#define I2C_TIMING(periph) I2C_ADD_TIMING(periph)

#define I2C_CTL1_AUTOEND    I2C_ADD_CTL1_AUTOEND
#define I2C_STAT_TBE        I2C_ADD_STAT_TBE
#define I2C_STAT_TI         I2C_ADD_STAT_TI
#define I2C_STAT_RBNE       I2C_ADD_STAT_RBNE
#define I2C_STAT_ADDSEND    I2C_ADD_STAT_ADDSEND
#define I2C_STAT_NACK       I2C_ADD_STAT_NACK
#define I2C_STAT_STPDET     I2C_ADD_STAT_STPDET
#define I2C_STAT_TC         I2C_ADD_STAT_TC
#define I2C_STAT_TCR        I2C_ADD_STAT_TCR
#define I2C_STAT_BERR       I2C_ADD_STAT_BERR
#define I2C_STAT_LOSTARB    I2C_ADD_STAT_LOSTARB
#define I2C_STAT_I2CBSY     I2C_ADD_STAT_I2CBSY
#define I2C_STAT_TR         I2C_ADD_STAT_TR
#define I2C_INT_ERR         I2C_ADD_INT_ERR
#define I2C_INT_TC          I2C_ADD_INT_TC
#define I2C_INT_STPDET      I2C_ADD_INT_STPDET
#define I2C_INT_NACK        I2C_ADD_INT_NACK
#define I2C_INT_ADDM        I2C_ADD_INT_ADDM
#define I2C_INT_RBNE        I2C_ADD_INT_RBNE
#define I2C_INT_TI          I2C_ADD_INT_TI
#define I2C_FLAG_ADDSEND    I2C_ADD_FLAG_ADDSEND
#define I2C_FLAG_NACK       I2C_ADD_FLAG_NACK
#define I2C_FLAG_STPDET     I2C_ADD_FLAG_STPDET
#define I2C_FLAG_TC         I2C_ADD_FLAG_TC
#define I2C_FLAG_BERR       I2C_ADD_FLAG_BERR
#define I2C_FLAG_LOSTARB    I2C_ADD_FLAG_LOSTARB
#define I2C_DMA_TRANSMIT    I2C_ADD_DMA_TRANSMIT
#define I2C_DMA_RECEIVE     I2C_ADD_DMA_RECEIVE
#define I2C_MASTER_TRANSMIT I2C_ADD_MASTER_TRANSMIT
#define I2C_MASTER_RECEIVE  I2C_ADD_MASTER_RECEIVE
#define I2C_ADDFORMAT_7BITS I2C_ADD_ADDFORMAT_7BITS

#define i2c_interrupt_enable            i2c_add_interrupt_enable
#define i2c_interrupt_disable           i2c_add_interrupt_disable
#define i2c_flag_get                    i2c_add_flag_get
#define i2c_flag_clear                  i2c_add_flag_clear
#define i2c_dma_enable                  i2c_add_dma_enable
#define i2c_dma_disable                 i2c_add_dma_disable
#define i2c_master_addressing           i2c_add_master_addressing
#define i2c_enable                      i2c_add_enable
#define i2c_disable                     i2c_add_disable
#define i2c_start_on_bus                i2c_add_start_on_bus
#define i2c_stop_on_bus                 i2c_add_stop_on_bus
#define i2c_data_transmit               i2c_add_data_transmit
#define i2c_data_receive                i2c_add_data_receive
#define i2c_stretch_scl_low_enable      i2c_add_stretch_scl_low_enable
#define i2c_timing_config               i2c_add_timing_config
#define i2c_master_clock_config         i2c_add_master_clock_config
#define i2c_address10_enable            i2c_add_address10_enable
#define i2c_address10_disable           i2c_add_address10_disable
#define i2c_address10_header_disable    i2c_add_address10_header_disable
#define i2c_automatic_end_enable        i2c_add_automatic_end_enable
#define i2c_automatic_end_disable       i2c_add_automatic_end_disable
#define i2c_reload_enable               i2c_add_reload_enable
#define i2c_reload_disable              i2c_add_reload_disable
#define i2c_transfer_byte_number_config i2c_add_transfer_byte_number_config
#define i2c_address_config              i2c_add_address_config
#define i2c_address_disable             i2c_add_address_disable

#elif defined(CONFIG_SOC_SERIES_GD32E50X) || defined(CONFIG_SOC_SERIES_GD32E51X)

/* I2C0/1 are legacy IP, I2C2 is ADD IP with I2C2_* names */
#undef I2C_CTL1
#undef I2C_STAT
#undef I2C_RDATA
#undef I2C_TDATA
#undef I2C_TIMING
#undef I2C_CTL1_AUTOEND
#undef I2C_STAT_TBE
#undef I2C_STAT_TI
#undef I2C_STAT_RBNE
#undef I2C_STAT_ADDSEND
#undef I2C_STAT_NACK
#undef I2C_STAT_STPDET
#undef I2C_STAT_TC
#undef I2C_STAT_TCR
#undef I2C_STAT_BERR
#undef I2C_STAT_LOSTARB
#undef I2C_STAT_I2CBSY
#undef I2C_STAT_TR
#undef I2C_INT_ERR
#undef I2C_INT_TC
#undef I2C_INT_STPDET
#undef I2C_INT_NACK
#undef I2C_INT_ADDM
#undef I2C_INT_RBNE
#undef I2C_INT_TI
#undef I2C_FLAG_ADDSEND
#undef I2C_FLAG_NACK
#undef I2C_FLAG_STPDET
#undef I2C_FLAG_TC
#undef I2C_FLAG_BERR
#undef I2C_FLAG_LOSTARB
#undef I2C_DMA_TRANSMIT
#undef I2C_DMA_RECEIVE
#undef I2C_MASTER_TRANSMIT
#undef I2C_MASTER_RECEIVE
#undef I2C_ADDFORMAT_7BITS

#define I2C_CTL1(periph)   I2C2_CTL1(periph)
#define I2C_STAT(periph)   I2C2_STAT(periph)
#define I2C_RDATA(periph)  I2C2_RDATA(periph)
#define I2C_TDATA(periph)  I2C2_TDATA(periph)
#define I2C_TIMING(periph) I2C2_TIMING(periph)

#define I2C_CTL1_AUTOEND    I2C2_CTL1_AUTOEND
#define I2C_STAT_TBE        I2C2_STAT_TBE
#define I2C_STAT_TI         I2C2_STAT_TI
#define I2C_STAT_RBNE       I2C2_STAT_RBNE
#define I2C_STAT_ADDSEND    I2C2_STAT_ADDSEND
#define I2C_STAT_NACK       I2C2_STAT_NACK
#define I2C_STAT_STPDET     I2C2_STAT_STPDET
#define I2C_STAT_TC         I2C2_STAT_TC
#define I2C_STAT_TCR        I2C2_STAT_TCR
#define I2C_STAT_BERR       I2C2_STAT_BERR
#define I2C_STAT_LOSTARB    I2C2_STAT_LOSTARB
#define I2C_STAT_I2CBSY     I2C2_STAT_I2CBSY
#define I2C_STAT_TR         I2C2_STAT_TR
#define I2C_INT_ERR         I2C2_INT_ERR
#define I2C_INT_TC          I2C2_INT_TC
#define I2C_INT_STPDET      I2C2_INT_STPDET
#define I2C_INT_NACK        I2C2_INT_NACK
#define I2C_INT_ADDM        I2C2_INT_ADDM
#define I2C_INT_RBNE        I2C2_INT_RBNE
#define I2C_INT_TI          I2C2_INT_TI
#define I2C_FLAG_ADDSEND    I2C2_FLAG_ADDSEND
#define I2C_FLAG_NACK       I2C2_FLAG_NACK
#define I2C_FLAG_STPDET     I2C2_FLAG_STPDET
#define I2C_FLAG_TC         I2C2_FLAG_TC
#define I2C_FLAG_BERR       I2C2_FLAG_BERR
#define I2C_FLAG_LOSTARB    I2C2_FLAG_LOSTARB
#define I2C_DMA_TRANSMIT    I2C2_DMA_TRANSMIT
#define I2C_DMA_RECEIVE     I2C2_DMA_RECEIVE
#define I2C_MASTER_TRANSMIT I2C2_MASTER_TRANSMIT
#define I2C_MASTER_RECEIVE  I2C2_MASTER_RECEIVE
#define I2C_ADDFORMAT_7BITS I2C2_ADDFORMAT_7BITS

/* I2C2-specific HAL functions carry an i2c2_ prefix */
#define i2c_interrupt_enable  i2c2_interrupt_enable
#define i2c_interrupt_disable i2c2_interrupt_disable
#define i2c_flag_get          i2c2_flag_get
#define i2c_flag_clear        i2c2_flag_clear
#define i2c_dma_enable        i2c2_dma_enable
#define i2c_dma_disable       i2c2_dma_disable
#define i2c_master_addressing i2c2_master_addressing

/*
 * The remaining functions keep their plain names: i2c_enable/disable,
 * i2c_start/stop_on_bus, i2c_data_transmit/receive and
 * i2c_stretch_scl_low_enable are shared with I2C0/1 (the HAL branches on
 * the peripheral); i2c_timing_config, i2c_master_clock_config, the
 * address/reload/automatic-end helpers are I2C2-only but already plain.
 */

#else /* ADD-only series: plain I2C_* / i2c_*() names are native */

#if defined(I2C_ADD_CTL0) || defined(I2C2_CTL0)
#error "Mixed-IP GD32 series needs an explicit ADD IP naming branch"
#endif

#endif /* HAL naming map */

#ifndef I2C_TIMING
#error "gd,gd32-i2c-v2 requires the ADD I2C IP (TIMING register)"
#endif

/* Catch a mapping that silently resolved to a same-named legacy macro */
BUILD_ASSERT(I2C_STAT_TI == BIT(1), "I2C ADD STAT.TI mapping mismatch");
BUILD_ASSERT(I2C_STAT_NACK == BIT(4), "I2C ADD STAT.NACK mapping mismatch");
BUILD_ASSERT(I2C_STAT_TC == BIT(6), "I2C ADD STAT.TC mapping mismatch");
BUILD_ASSERT(I2C_STAT_TCR == BIT(7), "I2C ADD STAT.TCR mapping mismatch");
BUILD_ASSERT(I2C_STAT_I2CBSY == BIT(15), "I2C ADD STAT.I2CBSY mapping mismatch");
BUILD_ASSERT(I2C_STAT_TR == BIT(16), "I2C ADD STAT.TR mapping mismatch");
BUILD_ASSERT(I2C_CTL1_AUTOEND == BIT(25), "I2C ADD CTL1.AUTOEND mapping mismatch");

/* Bus error */
#define I2C_GD32_V2_ERR_BERR BIT(0)
/* Arbitration lost */
#define I2C_GD32_V2_ERR_LARB BIT(1)
/* No ACK received */
#define I2C_GD32_V2_ERR_AERR BIT(2)
/* I2C bus busy */
#define I2C_GD32_V2_ERR_BUSY BIT(4)

/* Maximum BYTENUM per segment; longer transfers use RELOAD */
#define I2C_GD32_V2_MAX_SEG 255U

#ifdef CONFIG_I2C_GD32_DMA
enum i2c_gd32_v2_dma_direction {
	RX = 0,
	TX,
	NUM_OF_DIRECTION
};

struct i2c_gd32_v2_dma_config {
	const struct device *dev;
	uint32_t channel;
	uint32_t config;
	uint32_t slot;
	uint32_t fifo_threshold;
};

struct i2c_gd32_v2_dma_data {
	struct dma_config config;
	struct dma_block_config block;
	uint32_t count;
};
#endif

struct i2c_gd32_v2_config {
	uint32_t reg;
	uint32_t bitrate;
	uint16_t clkid;
	struct reset_dt_spec reset;
	const struct pinctrl_dev_config *pcfg;
	void (*irq_cfg_func)(void);
#ifdef CONFIG_I2C_GD32_DMA
	const struct i2c_gd32_v2_dma_config dma[NUM_OF_DIRECTION];
#endif
};

struct i2c_gd32_v2_data {
	struct k_sem bus_mutex;
	struct k_sem sync_sem;
	uint32_t dev_config;
	/* 7-bit or 10-bit target address of the current transfer */
	uint16_t addr;
	uint32_t xfer_len;
	struct i2c_msg *current;
	uint8_t errs;
	/* Grouped messages end with STOP (AUTOEND only on the final segment) */
	bool add_has_stop;
#ifdef CONFIG_I2C_TARGET
	struct i2c_target_config *target_cfg;
	/* Keeps the target-mode ISR path away from an active controller transfer */
	bool master_active;
	/* Next TI in a target read must be served by read_requested() */
	bool target_rd_first;
#endif
#ifdef CONFIG_I2C_GD32_DMA
	struct i2c_gd32_v2_dma_data dma[NUM_OF_DIRECTION];
	bool dma_enabled;
	uint32_t msg_count;
#endif
};

static inline void i2c_gd32_v2_disable_interrupts(const struct i2c_gd32_v2_config *cfg)
{
	i2c_interrupt_disable(cfg->reg, I2C_INT_ERR);
	i2c_interrupt_disable(cfg->reg, I2C_INT_STPDET);
	i2c_interrupt_disable(cfg->reg, I2C_INT_TC);
	i2c_interrupt_disable(cfg->reg, I2C_INT_NACK);
	i2c_interrupt_disable(cfg->reg, I2C_INT_TI);
	i2c_interrupt_disable(cfg->reg, I2C_INT_RBNE);
	i2c_interrupt_disable(cfg->reg, I2C_INT_ADDM);
}

static inline void i2c_gd32_v2_enable_interrupts(const struct i2c_gd32_v2_config *cfg)
{
	i2c_interrupt_enable(cfg->reg, I2C_INT_ERR);
	i2c_interrupt_enable(cfg->reg, I2C_INT_ADDM);
	i2c_interrupt_enable(cfg->reg, I2C_INT_STPDET);
	i2c_interrupt_enable(cfg->reg, I2C_INT_TC);
	i2c_interrupt_enable(cfg->reg, I2C_INT_NACK);
	i2c_interrupt_enable(cfg->reg, I2C_INT_TI);
	i2c_interrupt_enable(cfg->reg, I2C_INT_RBNE);
}

#ifdef CONFIG_I2C_TARGET
static inline void i2c_gd32_v2_enable_target_interrupts(const struct i2c_gd32_v2_config *cfg)
{
	i2c_interrupt_enable(cfg->reg, I2C_INT_ADDM);
	i2c_interrupt_enable(cfg->reg, I2C_INT_RBNE);
	i2c_interrupt_enable(cfg->reg, I2C_INT_TI);
	i2c_interrupt_enable(cfg->reg, I2C_INT_STPDET);
	i2c_interrupt_enable(cfg->reg, I2C_INT_NACK);
	i2c_interrupt_enable(cfg->reg, I2C_INT_ERR);
}
#endif

#ifdef CONFIG_I2C_GD32_DMA
static uint32_t dummy_tx;
static uint32_t dummy_rx;

static void i2c_gd32_v2_dma_callback(const struct device *dma_dev, void *arg, uint32_t channel,
				     int status);

static inline void i2c_gd32_v2_enable_dma_interrupts(const struct i2c_gd32_v2_config *cfg)
{
	/* Only control/error events; data moves through DMA */
	i2c_interrupt_enable(cfg->reg, I2C_INT_ERR);
	i2c_interrupt_enable(cfg->reg, I2C_INT_NACK);
	i2c_interrupt_enable(cfg->reg, I2C_INT_STPDET);
	i2c_interrupt_disable(cfg->reg, I2C_INT_TC);
	i2c_interrupt_disable(cfg->reg, I2C_INT_TI);
	i2c_interrupt_disable(cfg->reg, I2C_INT_RBNE);
	i2c_interrupt_disable(cfg->reg, I2C_INT_ADDM);
}

static bool i2c_gd32_v2_dma_enabled(const struct device *dev)
{
	const struct i2c_gd32_v2_config *cfg = dev->config;

	return (cfg->dma[TX].dev != NULL) && (cfg->dma[RX].dev != NULL);
}

static size_t i2c_gd32_v2_dma_enabled_num(const struct device *dev)
{
	return i2c_gd32_v2_dma_enabled(dev) ? 2 : 0;
}

static bool i2c_gd32_v2_use_dma(const struct device *dev)
{
	struct i2c_gd32_v2_data *data = dev->data;

	return data->dma_enabled && (data->current->len >= CONFIG_I2C_GD32_DMA_THRESHOLD) &&
	       i2c_gd32_v2_dma_enabled(dev);
}

static int i2c_gd32_v2_dma_setup(const struct device *dev, const uint32_t dir)
{
	const struct i2c_gd32_v2_config *cfg = dev->config;
	struct i2c_gd32_v2_data *data = dev->data;
	struct dma_config *dma_cfg = &data->dma[dir].config;
	struct dma_block_config *block_cfg = &data->dma[dir].block;
	const struct i2c_gd32_v2_dma_config *dma = &cfg->dma[dir];
	int ret;

	memset(dma_cfg, 0, sizeof(struct dma_config));
	memset(block_cfg, 0, sizeof(struct dma_block_config));

	dma_cfg->source_burst_length = 1;
	dma_cfg->dest_burst_length = 1;
	dma_cfg->user_data = (void *)dev;
	dma_cfg->dma_callback = i2c_gd32_v2_dma_callback;
	dma_cfg->block_count = 1U;
	dma_cfg->head_block = block_cfg;
	dma_cfg->dma_slot = dma->slot;
	dma_cfg->channel_priority = GD32_DMA_CONFIG_PRIORITY(dma->config);
	dma_cfg->channel_direction = GD32_DMA_CONFIG_DIRECTION(dma->config);
	dma_cfg->source_data_size = 1;
	dma_cfg->dest_data_size = 1;

	block_cfg->block_size = data->current->len;

	if (dir == TX) {
		block_cfg->dest_address = (uint32_t)&I2C_TDATA(cfg->reg);
		block_cfg->dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
		if (data->current->buf) {
			block_cfg->source_address = (uint32_t)data->current->buf;
			block_cfg->source_addr_adj = DMA_ADDR_ADJ_INCREMENT;
		} else {
			block_cfg->source_address = (uint32_t)&dummy_tx;
			block_cfg->source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
		}
	} else {
		block_cfg->source_address = (uint32_t)&I2C_RDATA(cfg->reg);
		block_cfg->source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
		if (data->current->buf) {
			block_cfg->dest_address = (uint32_t)data->current->buf;
			block_cfg->dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;
		} else {
			block_cfg->dest_address = (uint32_t)&dummy_rx;
			block_cfg->dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
		}
	}

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

static int i2c_gd32_v2_start_dma_transceive(const struct device *dev)
{
	const struct i2c_gd32_v2_config *cfg = dev->config;
	struct i2c_gd32_v2_data *data = dev->data;
	const size_t chunk_len = data->current->len;
	struct dma_status stat;
	int ret = 0;

	for (size_t i = 0; i < i2c_gd32_v2_dma_enabled_num(dev); i++) {
		dma_get_status(cfg->dma[i].dev, cfg->dma[i].channel, &stat);
		if ((chunk_len != data->dma[i].count) && !stat.busy) {
			ret = i2c_gd32_v2_dma_setup(dev, i);
			if (ret < 0) {
				break;
			}
		}
	}

	if (ret < 0) {
		for (size_t i = 0; i < i2c_gd32_v2_dma_enabled_num(dev); i++) {
			dma_stop(cfg->dma[i].dev, cfg->dma[i].channel);
		}
	}

	return ret;
}

static void i2c_gd32_v2_complete(const struct device *dev, int status)
{
	struct i2c_gd32_v2_data *data = dev->data;
	const struct i2c_gd32_v2_config *cfg = dev->config;

	i2c_dma_disable(cfg->reg, I2C_DMA_TRANSMIT);
	i2c_dma_disable(cfg->reg, I2C_DMA_RECEIVE);

	if (i2c_flag_get(cfg->reg, I2C_FLAG_STPDET)) {
		i2c_flag_clear(cfg->reg, I2C_FLAG_STPDET);
	}
	if (i2c_flag_get(cfg->reg, I2C_FLAG_NACK)) {
		i2c_flag_clear(cfg->reg, I2C_FLAG_NACK);
	}

	for (size_t i = 0; i < i2c_gd32_v2_dma_enabled_num(dev); i++) {
		dma_stop(cfg->dma[i].dev, cfg->dma[i].channel);
	}

	data->current->len = 0;

	if (status < 0 && data->errs == 0U) {
		data->errs |= I2C_GD32_V2_ERR_AERR;
	}

	k_sem_give(&data->sync_sem);
}

static bool i2c_gd32_v2_chunk_transfer_finished(const struct device *dev)
{
	struct i2c_gd32_v2_data *data = dev->data;
	const size_t chunk_len = data->current->len;

	if (data->current->flags & I2C_MSG_READ) {
		return data->dma[RX].count >= chunk_len;
	}

	return data->dma[TX].count >= chunk_len;
}

static void i2c_gd32_v2_dma_callback(const struct device *dma_dev, void *arg, uint32_t channel,
				     int status)
{
	const struct device *dev = (const struct device *)arg;
	const struct i2c_gd32_v2_config *cfg = dev->config;
	struct i2c_gd32_v2_data *data = dev->data;
	const size_t chunk_len = data->current->len;
	uint32_t dir = (data->current->flags & I2C_MSG_READ) ? RX : TX;
	int err;

	if (status < 0) {
		LOG_ERR("dma:%p ch:%d callback gets error: %d", dma_dev, channel, status);
		i2c_gd32_v2_complete(dev, status);
		return;
	}

	if (data->current->flags & I2C_MSG_STOP) {
		i2c_stop_on_bus(cfg->reg);
	}

	/* Half-duplex: only the channel of the active direction matters */
	if ((dma_dev != cfg->dma[dir].dev) || (channel != cfg->dma[dir].channel)) {
		return;
	}

	if (data->errs != 0U) {
		LOG_DBG("I2C error 0x%02x in DMA callback", data->errs);
		i2c_gd32_v2_complete(dev, -EIO);
		return;
	}

	data->dma[dir].count += chunk_len;

	if (i2c_gd32_v2_chunk_transfer_finished(dev)) {
		if ((data->current->flags & I2C_MSG_STOP) &&
		    !(I2C_CTL1(cfg->reg) & I2C_CTL1_AUTOEND)) {
			i2c_stop_on_bus(cfg->reg);
		}

		data->current->len = 0;
		data->xfer_len -= chunk_len;

		if (data->xfer_len > 0U && data->msg_count > 1U) {
			data->current++;
			data->msg_count--;
			for (size_t i = 0; i < ARRAY_SIZE(data->dma); i++) {
				data->dma[i].count = 0;
			}
			err = i2c_gd32_v2_start_dma_transceive(dev);
			if (err) {
				i2c_gd32_v2_complete(dev, err);
			}
		} else {
			i2c_gd32_v2_complete(dev, 0);
		}
		return;
	}

	err = i2c_gd32_v2_start_dma_transceive(dev);
	if (err) {
		i2c_gd32_v2_complete(dev, err);
	}
}
#endif /* CONFIG_I2C_GD32_DMA */

static int i2c_gd32_v2_bus_recovery(const struct device *dev)
{
	const struct i2c_gd32_v2_config *cfg = dev->config;
	struct i2c_gd32_v2_data *data = dev->data;

	i2c_disable(cfg->reg);
	i2c_flag_clear(cfg->reg, I2C_FLAG_NACK);
	i2c_flag_clear(cfg->reg, I2C_FLAG_BERR);
	i2c_flag_clear(cfg->reg, I2C_FLAG_LOSTARB);
	i2c_flag_clear(cfg->reg, I2C_FLAG_STPDET);
	i2c_enable(cfg->reg);

	for (uint32_t retry = 0; retry < 100U; retry++) {
		if (!(I2C_STAT(cfg->reg) & I2C_STAT_I2CBSY)) {
			return 0;
		}
	}

	LOG_ERR("I2C bus recovery failed");
	data->errs |= I2C_GD32_V2_ERR_BUSY;
	return -EBUSY;
}

static inline void i2c_gd32_v2_xfer_read(struct i2c_gd32_v2_data *data,
					 const struct i2c_gd32_v2_config *cfg)
{
	data->current->len--;
	*data->current->buf = (uint8_t)i2c_data_receive(cfg->reg);
	data->current->buf++;
	if ((data->xfer_len > 0U) && (data->current->len == 0U)) {
		data->current++;
	}
}

static inline void i2c_gd32_v2_xfer_write(struct i2c_gd32_v2_data *data,
					  const struct i2c_gd32_v2_config *cfg)
{
	data->current->len--;
	i2c_data_transmit(cfg->reg, *data->current->buf);
	data->current->buf++;
	if ((data->xfer_len > 0U) && (data->current->len == 0U)) {
		data->current++;
	}
}

#ifdef CONFIG_I2C_TARGET
static void i2c_gd32_v2_target_isr(const struct device *dev, uint32_t stat)
{
	struct i2c_gd32_v2_data *data = dev->data;
	const struct i2c_gd32_v2_config *cfg = dev->config;
	struct i2c_target_config *target = data->target_cfg;
	const struct i2c_target_callbacks *cb = target->callbacks;
	uint8_t val;

	if (stat & I2C_STAT_ADDSEND) {
		if (stat & I2C_STAT_TR) {
			/* Flush TDATA so the first byte comes from read_requested() */
			I2C_STAT(cfg->reg) |= I2C_STAT_TBE;
			data->target_rd_first = true;
		} else if (cb->write_requested != NULL) {
			(void)cb->write_requested(target);
		}
		i2c_flag_clear(cfg->reg, I2C_FLAG_ADDSEND);
	}

	if (stat & I2C_STAT_RBNE) {
		val = (uint8_t)i2c_data_receive(cfg->reg);
		if (cb->write_received != NULL) {
			(void)cb->write_received(target, val);
		}
	}

	if (stat & I2C_STAT_TI) {
		val = 0xFFU;
		if (data->target_rd_first) {
			data->target_rd_first = false;
			if (cb->read_requested != NULL) {
				(void)cb->read_requested(target, &val);
			}
		} else if (cb->read_processed != NULL) {
			(void)cb->read_processed(target, &val);
		}
		i2c_data_transmit(cfg->reg, val);
	}

	if (stat & I2C_STAT_NACK) {
		/* Controller NACKs the last byte of a target read */
		i2c_flag_clear(cfg->reg, I2C_FLAG_NACK);
	}

	if (stat & I2C_STAT_STPDET) {
		i2c_flag_clear(cfg->reg, I2C_FLAG_STPDET);
		if (cb->stop != NULL) {
			(void)cb->stop(target);
		}
	}
}
#endif /* CONFIG_I2C_TARGET */

static void i2c_gd32_v2_event_isr(const struct device *dev)
{
	struct i2c_gd32_v2_data *data = dev->data;
	const struct i2c_gd32_v2_config *cfg = dev->config;
	uint32_t stat = I2C_STAT(cfg->reg);

#ifdef CONFIG_I2C_TARGET
	if ((data->target_cfg != NULL) && !data->master_active) {
		i2c_gd32_v2_target_isr(dev, stat);
		return;
	}
#endif

	if (stat & I2C_STAT_NACK) {
		i2c_flag_clear(cfg->reg, I2C_FLAG_NACK);
		data->errs |= I2C_GD32_V2_ERR_AERR;
#ifdef CONFIG_I2C_GD32_DMA
		if (i2c_gd32_v2_use_dma(dev)) {
			i2c_dma_disable(cfg->reg, I2C_DMA_TRANSMIT);
			i2c_dma_disable(cfg->reg, I2C_DMA_RECEIVE);
			for (size_t i = 0; i < i2c_gd32_v2_dma_enabled_num(dev); i++) {
				dma_stop(cfg->dma[i].dev, cfg->dma[i].channel);
			}
		}
#endif
		i2c_stop_on_bus(cfg->reg);
		k_sem_give(&data->sync_sem);
		return;
	}

	if (stat & I2C_STAT_STPDET) {
		i2c_flag_clear(cfg->reg, I2C_FLAG_STPDET);
		i2c_automatic_end_disable(cfg->reg);
		k_sem_give(&data->sync_sem);
		i2c_gd32_v2_disable_interrupts(cfg);
		return;
	}

	if ((stat & I2C_STAT_RBNE) && (data->xfer_len > 0U)) {
		data->xfer_len--;
		i2c_gd32_v2_xfer_read(data, cfg);
	}

	if ((stat & I2C_STAT_TI) && (data->xfer_len > 0U)) {
		data->xfer_len--;
		i2c_gd32_v2_xfer_write(data, cfg);
	}

	if ((stat & I2C_STAT_TC) && (data->xfer_len == 0U)) {
		if (data->add_has_stop) {
			i2c_stop_on_bus(cfg->reg);
		}
		k_sem_give(&data->sync_sem);
		i2c_gd32_v2_disable_interrupts(cfg);
	}

	if (stat & I2C_STAT_TCR) {
		uint32_t seg = MIN(data->xfer_len, I2C_GD32_V2_MAX_SEG);

		i2c_transfer_byte_number_config(cfg->reg, (uint8_t)seg);

		if (data->xfer_len <= I2C_GD32_V2_MAX_SEG) {
			i2c_reload_disable(cfg->reg);
			if (data->add_has_stop) {
				i2c_automatic_end_enable(cfg->reg);
			}
		}

		if (!(data->current->flags & I2C_MSG_READ)) {
			i2c_interrupt_enable(cfg->reg, I2C_INT_TI);
		}
	}
}

static void i2c_gd32_v2_error_isr(const struct device *dev)
{
	struct i2c_gd32_v2_data *data = dev->data;
	const struct i2c_gd32_v2_config *cfg = dev->config;
	uint32_t stat = I2C_STAT(cfg->reg);

	if (stat & I2C_STAT_BERR) {
		i2c_flag_clear(cfg->reg, I2C_FLAG_BERR);
		data->errs |= I2C_GD32_V2_ERR_BERR;
	}

	if (stat & I2C_STAT_LOSTARB) {
		i2c_flag_clear(cfg->reg, I2C_FLAG_LOSTARB);
		data->errs |= I2C_GD32_V2_ERR_LARB;
	}

#ifdef CONFIG_I2C_TARGET
	if ((data->target_cfg != NULL) && !data->master_active) {
		return;
	}
#endif

	if (data->errs != 0U) {
		i2c_stop_on_bus(cfg->reg);
		k_sem_give(&data->sync_sem);
	}
}

static void i2c_gd32_v2_log_err(struct i2c_gd32_v2_data *data)
{
	if (data->errs & I2C_GD32_V2_ERR_BERR) {
		LOG_ERR("Bus error");
	}

	if (data->errs & I2C_GD32_V2_ERR_LARB) {
		LOG_ERR("Arbitration lost");
	}

	if (data->errs & I2C_GD32_V2_ERR_AERR) {
		LOG_DBG("No ACK received");
	}

	if (data->errs & I2C_GD32_V2_ERR_BUSY) {
		LOG_ERR("I2C bus busy");
	}
}

#ifdef CONFIG_I2C_TARGET
static void i2c_gd32_v2_target_setup(const struct i2c_gd32_v2_config *cfg, uint32_t addr)
{
	i2c_disable(cfg->reg);
	i2c_address_config(cfg->reg, addr, I2C_ADDFORMAT_7BITS);
	i2c_gd32_v2_enable_target_interrupts(cfg);
	i2c_stretch_scl_low_enable(cfg->reg);
	i2c_enable(cfg->reg);
}

static int i2c_gd32_v2_target_register(const struct device *dev,
				       struct i2c_target_config *target)
{
	struct i2c_gd32_v2_data *data = dev->data;
	const struct i2c_gd32_v2_config *cfg = dev->config;

	if ((target == NULL) || (target->flags & I2C_TARGET_FLAGS_ADDR_10_BITS)) {
		return -EINVAL;
	}

	if (target->callbacks == NULL) {
		LOG_ERR("I2C target callbacks cannot be NULL");
		return -EINVAL;
	}

	k_sem_take(&data->bus_mutex, K_FOREVER);
	if (data->target_cfg != NULL) {
		k_sem_give(&data->bus_mutex);
		return -EBUSY;
	}

	data->target_rd_first = false;
	data->target_cfg = target;
	i2c_gd32_v2_target_setup(cfg, target->address & 0x7FU);
	k_sem_give(&data->bus_mutex);

	return 0;
}

static int i2c_gd32_v2_target_unregister(const struct device *dev,
					 struct i2c_target_config *target)
{
	struct i2c_gd32_v2_data *data = dev->data;
	const struct i2c_gd32_v2_config *cfg = dev->config;
	int ret = 0;

	k_sem_take(&data->bus_mutex, K_FOREVER);
	if (data->target_cfg != target) {
		ret = -EINVAL;
	} else {
		i2c_disable(cfg->reg);
		i2c_address_disable(cfg->reg);
		i2c_gd32_v2_disable_interrupts(cfg);
		data->target_cfg = NULL;
	}
	k_sem_give(&data->bus_mutex);

	return ret;
}
#endif /* CONFIG_I2C_TARGET */

static void i2c_gd32_v2_xfer_begin(const struct device *dev)
{
	struct i2c_gd32_v2_data *data = dev->data;
	const struct i2c_gd32_v2_config *cfg = dev->config;
	bool addr10 = (data->dev_config & I2C_ADDR_10_BITS) != 0U;
	uint32_t total = data->xfer_len;
	uint32_t busy_retry = 10000U;
	uint32_t address;
	uint32_t direction;

	k_sem_reset(&data->sync_sem);
	data->errs = 0U;

	if (total == 0U) {
		k_sem_give(&data->sync_sem);
		return;
	}

#ifdef CONFIG_I2C_TARGET
	data->master_active = true;
#endif

	while ((I2C_STAT(cfg->reg) & I2C_STAT_I2CBSY) && busy_retry--) {
	}
	if (I2C_STAT(cfg->reg) & I2C_STAT_I2CBSY) {
		LOG_WRN("I2C bus stuck, attempting recovery");
		if (i2c_gd32_v2_bus_recovery(dev) < 0) {
			data->errs |= I2C_GD32_V2_ERR_BUSY;
			k_sem_give(&data->sync_sem);
			return;
		}
	}

	i2c_flag_clear(cfg->reg, I2C_FLAG_NACK);
	i2c_flag_clear(cfg->reg, I2C_FLAG_BERR);
	i2c_flag_clear(cfg->reg, I2C_FLAG_LOSTARB);
	i2c_flag_clear(cfg->reg, I2C_FLAG_STPDET);

	data->add_has_stop = (data->current->flags & I2C_MSG_STOP) != 0U;

	if (addr10) {
		i2c_address10_enable(cfg->reg);
	} else {
		i2c_address10_disable(cfg->reg);
	}

	if (total > I2C_GD32_V2_MAX_SEG) {
		i2c_reload_enable(cfg->reg);
		i2c_automatic_end_disable(cfg->reg);
	} else if (data->add_has_stop) {
		i2c_automatic_end_enable(cfg->reg);
		i2c_reload_disable(cfg->reg);
	} else {
		/* No STOP: the next message issues a repeated START */
		i2c_automatic_end_disable(cfg->reg);
		i2c_reload_disable(cfg->reg);
	}

	i2c_transfer_byte_number_config(cfg->reg, (uint8_t)MIN(total, I2C_GD32_V2_MAX_SEG));

	address = addr10 ? data->addr : ((uint32_t)data->addr << 1);
	direction = (data->current->flags & I2C_MSG_READ) ? I2C_MASTER_RECEIVE
							  : I2C_MASTER_TRANSMIT;
	i2c_master_addressing(cfg->reg, address, direction);

#ifdef CONFIG_I2C_GD32_DMA
	if (i2c_gd32_v2_use_dma(dev)) {
		i2c_gd32_v2_enable_dma_interrupts(cfg);
		i2c_start_on_bus(cfg->reg);
		return;
	}
#endif

	i2c_gd32_v2_enable_interrupts(cfg);
	i2c_start_on_bus(cfg->reg);
}

static int i2c_gd32_v2_xfer_end(const struct device *dev)
{
	struct i2c_gd32_v2_data *data = dev->data;
	const struct i2c_gd32_v2_config *cfg = dev->config;

	i2c_gd32_v2_disable_interrupts(cfg);

	if (data->current->flags & I2C_MSG_STOP) {
		while (I2C_STAT(cfg->reg) & I2C_STAT_I2CBSY) {
		}
	}

	i2c_address10_disable(cfg->reg);
	i2c_address10_header_disable(cfg->reg);

#ifdef CONFIG_I2C_TARGET
	if (data->target_cfg != NULL) {
		i2c_gd32_v2_target_setup(cfg, data->target_cfg->address & 0x7FU);
	}
	data->master_active = false;
#endif

	return (data->errs != 0U) ? -EIO : 0;
}

#ifdef CONFIG_I2C_GD32_DMA
static int i2c_gd32_v2_msg_dma(const struct device *dev, uint32_t dma_dir)
{
	struct i2c_gd32_v2_data *data = dev->data;
	const struct i2c_gd32_v2_config *cfg = dev->config;
	int ret;

	for (size_t i = 0; i < ARRAY_SIZE(data->dma); i++) {
		data->dma[i].count = 0;
	}
	data->errs = 0U;

	i2c_dma_enable(cfg->reg, dma_dir);

	ret = i2c_gd32_v2_start_dma_transceive(dev);
	if (ret < 0) {
		LOG_WRN("DMA start failed, falling back to PIO mode");
		i2c_dma_disable(cfg->reg, dma_dir);
		return -EAGAIN;
	}

	i2c_gd32_v2_xfer_begin(dev);

	ret = k_sem_take(&data->sync_sem, K_MSEC(CONFIG_I2C_GD32_DMA_TIMEOUT));
	if (ret < 0) {
		LOG_ERR("DMA transfer timeout");
		i2c_gd32_v2_complete(dev, -ETIMEDOUT);
	}

	if (data->errs != 0U) {
		i2c_gd32_v2_log_err(data);
	}

	return i2c_gd32_v2_xfer_end(dev);
}
#endif /* CONFIG_I2C_GD32_DMA */

static int i2c_gd32_v2_msg_xfer(const struct device *dev)
{
	struct i2c_gd32_v2_data *data = dev->data;

#ifdef CONFIG_I2C_GD32_DMA
	if (i2c_gd32_v2_use_dma(dev)) {
		uint32_t dma_dir = (data->current->flags & I2C_MSG_READ) ? I2C_DMA_RECEIVE
									 : I2C_DMA_TRANSMIT;
		int ret = i2c_gd32_v2_msg_dma(dev, dma_dir);

		if (ret != -EAGAIN) {
			return ret;
		}
	}
#endif

	i2c_gd32_v2_xfer_begin(dev);

	k_sem_take(&data->sync_sem, K_FOREVER);

	return i2c_gd32_v2_xfer_end(dev);
}

static int i2c_gd32_v2_transfer(const struct device *dev, struct i2c_msg *msgs, uint8_t num_msgs,
				uint16_t addr)
{
	struct i2c_gd32_v2_data *data = dev->data;
	const struct i2c_gd32_v2_config *cfg = dev->config;
	struct i2c_msg *current = msgs;
	uint8_t itr;
	int err = 0;

	/* First message flags implicitly contain I2C_MSG_RESTART flag. */
	current->flags |= I2C_MSG_RESTART;

	for (uint8_t i = 1; i <= num_msgs; i++) {
		if (i < num_msgs) {
			struct i2c_msg *next = current + 1;

			/* A direction change needs an explicit RESTART on the second message */
			if ((current->flags & I2C_MSG_RW_MASK) != (next->flags & I2C_MSG_RW_MASK) &&
			    ((next->flags & I2C_MSG_RESTART) == 0U)) {
				return -EINVAL;
			}

			/* Only the last message may carry STOP */
			if (current->flags & I2C_MSG_STOP) {
				return -EINVAL;
			}
		}

		if ((current->buf == NULL) || (current->len == 0U)) {
			return -EINVAL;
		}

		current++;
	}

	k_sem_take(&data->bus_mutex, K_FOREVER);

	i2c_enable(cfg->reg);

	if (data->dev_config & I2C_ADDR_10_BITS) {
		data->addr = addr & BIT_MASK(10);
	} else {
		data->addr = addr & BIT_MASK(7);
	}

	for (uint8_t i = 0; i < num_msgs; i = itr) {
		data->current = &msgs[i];
		data->xfer_len = msgs[i].len;
#ifdef CONFIG_I2C_GD32_DMA
		data->msg_count = 1;
#endif

		for (itr = i + 1; itr < num_msgs; itr++) {
			if ((data->current->flags & I2C_MSG_RW_MASK) !=
			    (msgs[itr].flags & I2C_MSG_RW_MASK)) {
				break;
			}
			data->xfer_len += msgs[itr].len;
#ifdef CONFIG_I2C_GD32_DMA
			data->msg_count++;
#endif
		}

		/* Grouped same-direction messages inherit STOP from the last one */
		if ((itr - i > 1) && (msgs[itr - 1].flags & I2C_MSG_STOP)) {
			data->current->flags |= I2C_MSG_STOP;
		}

		err = i2c_gd32_v2_msg_xfer(dev);
		if (err < 0) {
			i2c_gd32_v2_log_err(data);
			break;
		}
	}

#ifdef CONFIG_I2C_TARGET
	if (data->target_cfg == NULL) {
		i2c_disable(cfg->reg);
	}
#else
	i2c_disable(cfg->reg);
#endif

	k_sem_give(&data->bus_mutex);

	return err;
}

/*
 * Derive TIMING (PSC/SCLDELY/SDADELY) and SCLH/SCLL from the I2C kernel clock.
 * Timing limits follow the I2C specification (Standard/Fast mode).
 */
static int i2c_gd32_v2_configure(const struct device *dev, uint32_t dev_config)
{
	struct i2c_gd32_v2_data *data = dev->data;
	const struct i2c_gd32_v2_config *cfg = dev->config;
	uint32_t pclk;
	uint32_t bitrate_hz;
	bool fast_like = false;
	uint32_t psc = 0U;
	uint32_t ip_clk, total, sclh, scll;
	uint32_t t_psc, t_i2c_clk_ns;
	uint32_t tsu_dat_min_ns, taf_max_ns, tvd_dat_max_ns;
	uint32_t scl_dely, sda_dely, dnf_compensation, total_delay;
	int err = 0;

	k_sem_take(&data->bus_mutex, K_FOREVER);

	i2c_disable(cfg->reg);

	err = clock_control_get_rate(GD32_CLOCK_CONTROLLER, (clock_control_subsys_t)&cfg->clkid,
				     &pclk);
	if ((err < 0) || (pclk == 0U)) {
		err = (err < 0) ? err : -EIO;
		goto out;
	}

	switch (I2C_SPEED_GET(dev_config)) {
	case I2C_SPEED_STANDARD:
		bitrate_hz = I2C_BITRATE_STANDARD;
		break;
	case I2C_SPEED_FAST:
		bitrate_hz = I2C_BITRATE_FAST;
		fast_like = true;
		break;
	default:
		/* Fast-mode Plus needs per-series pad (FMP) setup that is not wired up yet */
		LOG_WRN("Unsupported I2C speed, using 100 kHz");
		bitrate_hz = I2C_BITRATE_STANDARD;
		break;
	}

	if (pclk > bitrate_hz * 8U) {
		psc = MIN((pclk / (bitrate_hz * 8U)) - 1U, 0x0FU);
	}

	ip_clk = pclk / (psc + 1U);
	total = CLAMP(ip_clk / bitrate_hz, 4U, 510U);

	if (fast_like) {
		/* tLOW >= 1.3us, tHIGH >= 0.6us */
		uint32_t tlow_min = (1300U * ip_clk) / (1000000000U / (psc + 1U));
		uint32_t thigh_min = (600U * ip_clk) / (1000000000U / (psc + 1U));

		scll = MAX(tlow_min, total * 2U / 3U);
		sclh = total - scll;
		if (sclh < thigh_min) {
			sclh = thigh_min;
			scll = total - sclh;
		}
	} else {
		/* tLOW >= 4.7us, tHIGH >= 4.0us */
		uint32_t tlow_min = (4700U * ip_clk) / (1000000000U / (psc + 1U));
		uint32_t thigh_min = (4000U * ip_clk) / (1000000000U / (psc + 1U));

		sclh = MAX(thigh_min, total / 2U);
		scll = MAX(tlow_min, total - sclh);
		if (sclh + scll > total) {
			scll = total * 55U / 100U;
			sclh = total - scll;
		}
	}

	sclh = CLAMP(sclh, 1U, 0xFFU);
	scll = CLAMP(scll, 1U, 0xFFU);

	t_psc = psc + 1U;
	t_i2c_clk_ns = 1000000000U / ip_clk;

	if (fast_like) {
		tsu_dat_min_ns = 100U;
		taf_max_ns = 300U;
		tvd_dat_max_ns = 900U;
	} else {
		tsu_dat_min_ns = 250U;
		taf_max_ns = 1000U;
		tvd_dat_max_ns = 3450U;
	}

	/* SCLDELY >= tSU,DAT(min) / ((PSC + 1) * tI2CCLK) - 1 */
	scl_dely = (tsu_dat_min_ns + (t_psc * t_i2c_clk_ns / 2U)) / (t_psc * t_i2c_clk_ns);
	if (scl_dely > 0U) {
		scl_dely -= 1U;
	}

	/* SDADELY >= (tVD,DAT(max) + tAF(max) - (DNF + 3) * tI2CCLK) / ((PSC + 1) * tI2CCLK) */
	dnf_compensation = 3U * t_i2c_clk_ns;
	total_delay = tvd_dat_max_ns + taf_max_ns;
	if (total_delay > dnf_compensation) {
		sda_dely = (total_delay - dnf_compensation + (t_psc * t_i2c_clk_ns / 2U)) /
			   (t_psc * t_i2c_clk_ns);
	} else {
		sda_dely = 0U;
	}

	scl_dely = CLAMP(scl_dely, 1U, 0x0FU);
	sda_dely = CLAMP(sda_dely, 1U, 0x0FU);

	i2c_timing_config(cfg->reg, psc, scl_dely, sda_dely);
	i2c_master_clock_config(cfg->reg, sclh, scll);

	i2c_enable(cfg->reg);
	data->dev_config = dev_config;

out:
	k_sem_give(&data->bus_mutex);

	return err;
}

static int i2c_gd32_v2_recover_bus(const struct device *dev)
{
	struct i2c_gd32_v2_data *data = dev->data;
	int ret;

	k_sem_take(&data->bus_mutex, K_FOREVER);
	ret = i2c_gd32_v2_bus_recovery(dev);
	k_sem_give(&data->bus_mutex);

	return ret;
}

static DEVICE_API(i2c, i2c_gd32_v2_driver_api) = {
	.configure = i2c_gd32_v2_configure,
	.transfer = i2c_gd32_v2_transfer,
	.recover_bus = i2c_gd32_v2_recover_bus,
#ifdef CONFIG_I2C_RTIO
	.iodev_submit = i2c_iodev_submit_fallback,
#endif
#ifdef CONFIG_I2C_TARGET
	.target_register = i2c_gd32_v2_target_register,
	.target_unregister = i2c_gd32_v2_target_unregister,
#endif
};

static int i2c_gd32_v2_init(const struct device *dev)
{
	struct i2c_gd32_v2_data *data = dev->data;
	const struct i2c_gd32_v2_config *cfg = dev->config;
	int err;

	err = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (err < 0) {
		return err;
	}

	k_sem_init(&data->bus_mutex, 1, 1);
	k_sem_init(&data->sync_sem, 0, K_SEM_MAX_LIMIT);

	(void)clock_control_on(GD32_CLOCK_CONTROLLER, (clock_control_subsys_t)&cfg->clkid);

	(void)reset_line_toggle_dt(&cfg->reset);

	cfg->irq_cfg_func();

	err = i2c_gd32_v2_configure(dev, I2C_MODE_CONTROLLER | i2c_map_dt_bitrate(cfg->bitrate));
	if (err < 0) {
		LOG_ERR("Failed to configure I2C (%d)", err);
		return err;
	}

#ifdef CONFIG_I2C_GD32_DMA
	if ((cfg->dma[RX].dev && !cfg->dma[TX].dev) || (cfg->dma[TX].dev && !cfg->dma[RX].dev)) {
		LOG_ERR("DMA must be enabled for both TX and RX channels");
		return -ENODEV;
	}

	for (size_t i = 0; i < i2c_gd32_v2_dma_enabled_num(dev); i++) {
		uint32_t ch_filter;

		if (!device_is_ready(cfg->dma[i].dev)) {
			LOG_ERR("DMA %s not ready", cfg->dma[i].dev->name);
			return -ENODEV;
		}

		ch_filter = BIT(cfg->dma[i].channel);
		err = dma_request_channel(cfg->dma[i].dev, &ch_filter);
		if (err < 0) {
			LOG_ERR("dma_request_channel failed %d", err);
			return err;
		}
	}

	data->dma_enabled = true;
#endif

	return 0;
}

#ifdef CONFIG_I2C_GD32_DMA
/*
 * DMA cells per controller binding:
 * gd,gd32-dma: channel, config
 * gd,gd32-dma-v1: channel, slot, config, fifo_threshold
 * gd,gd32-dmamux: channel, slot, config
 */
#define I2C_GD32_V2_DMA_IS_V1(idx, dir)                                                            \
	DT_NODE_HAS_COMPAT(DT_INST_DMAS_CTLR_BY_NAME(idx, dir), gd_gd32_dma_v1)

#define I2C_GD32_V2_DMA_IS_DMAMUX(idx, dir)                                                        \
	DT_NODE_HAS_COMPAT(DT_INST_DMAS_CTLR_BY_NAME(idx, dir), gd_gd32_dmamux)

#define I2C_GD32_V2_DMA_GET_SLOT(idx, dir)                                                         \
	COND_CODE_1(I2C_GD32_V2_DMA_IS_V1(idx, dir),                                              \
		    (DT_INST_DMAS_CELL_BY_NAME(idx, dir, slot)),                                   \
		    (COND_CODE_1(I2C_GD32_V2_DMA_IS_DMAMUX(idx, dir),                              \
				 (DT_INST_DMAS_CELL_BY_NAME(idx, dir, slot)), (0))))

#define I2C_GD32_V2_DMA_INITIALIZER(idx, dir)                                                      \
	{                                                                                          \
		.dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(idx, dir)),                         \
		.channel = DT_INST_DMAS_CELL_BY_NAME(idx, dir, channel),                           \
		.slot = I2C_GD32_V2_DMA_GET_SLOT(idx, dir),                                        \
		.config = DT_INST_DMAS_CELL_BY_NAME(idx, dir, config),                             \
		.fifo_threshold =                                                                  \
			COND_CODE_1(I2C_GD32_V2_DMA_IS_V1(idx, dir),                               \
				    (DT_INST_DMAS_CELL_BY_NAME(idx, dir, fifo_threshold)), (0)),   \
	}

#define I2C_GD32_V2_DMAS_DECL(idx)                                                                 \
	{                                                                                          \
		COND_CODE_1(DT_INST_DMAS_HAS_NAME(idx, rx),                                        \
			    (I2C_GD32_V2_DMA_INITIALIZER(idx, rx)), ({0})),                        \
		COND_CODE_1(DT_INST_DMAS_HAS_NAME(idx, tx),                                        \
			    (I2C_GD32_V2_DMA_INITIALIZER(idx, tx)), ({0})),                        \
	}
#endif /* CONFIG_I2C_GD32_DMA */

#define I2C_GD32_V2_INIT(inst)                                                                     \
	PINCTRL_DT_INST_DEFINE(inst);                                                              \
	static void i2c_gd32_v2_irq_cfg_func_##inst(void)                                          \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, event, irq),                                 \
			    DT_INST_IRQ_BY_NAME(inst, event, priority), i2c_gd32_v2_event_isr,     \
			    DEVICE_DT_INST_GET(inst), 0);                                          \
		irq_enable(DT_INST_IRQ_BY_NAME(inst, event, irq));                                 \
                                                                                                   \
		IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, error, irq),                                 \
			    DT_INST_IRQ_BY_NAME(inst, error, priority), i2c_gd32_v2_error_isr,     \
			    DEVICE_DT_INST_GET(inst), 0);                                          \
		irq_enable(DT_INST_IRQ_BY_NAME(inst, error, irq));                                 \
	}                                                                                          \
	static struct i2c_gd32_v2_data i2c_gd32_v2_data_##inst;                                    \
	static const struct i2c_gd32_v2_config i2c_gd32_v2_cfg_##inst = {                          \
		.reg = DT_INST_REG_ADDR(inst),                                                     \
		.bitrate = DT_INST_PROP(inst, clock_frequency),                                    \
		.clkid = DT_INST_CLOCKS_CELL(inst, id),                                            \
		.reset = RESET_DT_SPEC_INST_GET(inst),                                             \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                                      \
		.irq_cfg_func = i2c_gd32_v2_irq_cfg_func_##inst,                                   \
		IF_ENABLED(CONFIG_I2C_GD32_DMA, (.dma = I2C_GD32_V2_DMAS_DECL(inst),))};          \
	I2C_DEVICE_DT_INST_DEFINE(inst, i2c_gd32_v2_init, NULL, &i2c_gd32_v2_data_##inst,          \
				  &i2c_gd32_v2_cfg_##inst, POST_KERNEL, CONFIG_I2C_INIT_PRIORITY,  \
				  &i2c_gd32_v2_driver_api);

DT_INST_FOREACH_STATUS_OKAY(I2C_GD32_V2_INIT)
