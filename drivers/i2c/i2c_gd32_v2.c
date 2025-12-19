/*
 * Copyright (c) 2025 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
#include <zephyr/irq.h>

#include <errno.h>
LOG_MODULE_REGISTER(i2c_gd32_v2, CONFIG_I2C_LOG_LEVEL);

#include "i2c-priv.h"
#include "i2c_gd32.h"

#ifdef CONFIG_I2C_GD32_DMA
static uint32_t dummy_tx;
static uint32_t dummy_rx;

static inline void i2c_gd32_enable_dma_interrupts(const struct i2c_gd32_config *cfg)
{
	/* ADD IP: Enable control and error interrupts only in DMA mode */
	i2c_add_interrupt_enable(cfg->reg, I2C_ADD_INT_ERR);    /* Error interrupt */
	i2c_add_interrupt_enable(cfg->reg, I2C_ADD_INT_NACK);   /* NACK interrupt */
	i2c_add_interrupt_enable(cfg->reg, I2C_ADD_INT_STPDET); /* Stop detection interrupt */
	/* Disable all data transfer interrupts - DMA handles data transfer */
	i2c_add_interrupt_disable(cfg->reg, I2C_ADD_INT_TC);   /* Transfer complete interrupt */
	i2c_add_interrupt_disable(cfg->reg, I2C_ADD_INT_TI);   /* Disable transmit interrupt */
	i2c_add_interrupt_disable(cfg->reg, I2C_ADD_INT_RBNE); /* Disable receive interrupt */
	i2c_add_interrupt_disable(cfg->reg, I2C_ADD_INT_ADDM);
	/* Disable address match interrupt */
}
#endif

static inline void i2c_gd32_disable_interrupts(const struct i2c_gd32_config *cfg)
{
	/* ADD IP: Disable all I2C interrupts */
	i2c_add_interrupt_disable(cfg->reg, I2C_ADD_INT_ERR);    /* Error interrupt */
	i2c_add_interrupt_disable(cfg->reg, I2C_ADD_INT_STPDET); /* Stop detection interrupt */
	i2c_add_interrupt_disable(cfg->reg, I2C_ADD_INT_TC);     /* Transfer complete interrupt */
	i2c_add_interrupt_disable(cfg->reg, I2C_ADD_INT_NACK);   /* NACK interrupt */
	i2c_add_interrupt_disable(cfg->reg, I2C_ADD_INT_TI);     /* Transmit interrupt */
	i2c_add_interrupt_disable(cfg->reg, I2C_ADD_INT_RBNE);   /* Receive interrupt */
	i2c_add_interrupt_disable(cfg->reg, I2C_ADD_INT_ADDM);   /* Address match interrupt */
}

#ifdef CONFIG_I2C_GD32_DMA

static bool i2c_gd32_dma_enabled(const struct device *dev)
{
	const struct i2c_gd32_config *cfg = dev->config;

	if (cfg->dma[TX].dev && cfg->dma[RX].dev) {
		return true;
	}

	return false;
}

size_t i2c_gd32_dma_enabled_num(const struct device *dev)
{
	return i2c_gd32_dma_enabled(dev) ? 2 : 0;
}

static uint32_t i2c_gd32_dma_setup(const struct device *dev, const uint32_t dir)
{
	const struct i2c_gd32_config *cfg = dev->config;
	struct i2c_gd32_data *data = dev->data;
	struct dma_config *dma_cfg = &data->dma[dir].config;
	struct dma_block_config *block_cfg = &data->dma[dir].block;
	const struct i2c_gd32_dma_config *dma = &cfg->dma[dir];
	int ret;

	memset(dma_cfg, 0, sizeof(struct dma_config));
	memset(block_cfg, 0, sizeof(struct dma_block_config));

	dma_cfg->source_burst_length = 1;
	dma_cfg->dest_burst_length = 1;
	dma_cfg->user_data = (void *)dev;
	dma_cfg->dma_callback = i2c_gd32_dma_callback;
	dma_cfg->block_count = 1U;
	dma_cfg->head_block = block_cfg;
	dma_cfg->dma_slot = cfg->dma[dir].slot;
	dma_cfg->channel_priority = GD32_DMA_CONFIG_PRIORITY(cfg->dma[dir].config);
	dma_cfg->channel_direction = dir == TX ? MEMORY_TO_PERIPHERAL : PERIPHERAL_TO_MEMORY;

	/* I2C always uses 8-bit transfers */
	dma_cfg->source_data_size = 1;
	dma_cfg->dest_data_size = 1;

	block_cfg->block_size = data->current->len;

	if (dir == TX) {
		block_cfg->dest_address = (uint32_t)&I2C_ADD_TDATA(cfg->reg);
		block_cfg->dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
		if (data->current->buf) {
			block_cfg->source_address = (uint32_t)data->current->buf;
			block_cfg->source_addr_adj = DMA_ADDR_ADJ_INCREMENT;
		} else {
			block_cfg->source_address = (uint32_t)&dummy_tx;
			block_cfg->source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
		}
	}

	if (dir == RX) {
		block_cfg->source_address = (uint32_t)&I2C_ADD_RDATA(cfg->reg);
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
		LOG_ERR("dma_config %p failed %d\n", dma->dev, ret);
		return ret;
	}
	ret = dma_start(dma->dev, dma->channel);
	if (ret < 0) {
		LOG_ERR("dma_start %p failed %d\n", dma->dev, ret);
		return ret;
	}

	return 0;
}

static int i2c_gd32_start_dma_transceive(const struct device *dev)
{
	const struct i2c_gd32_config *cfg = dev->config;
	struct i2c_gd32_data *data = dev->data;
	const size_t chunk_len = data->current->len;
	struct dma_status stat;
	int ret = 0;

	for (size_t i = 0; i < i2c_gd32_dma_enabled_num(dev); i++) {
		dma_get_status(cfg->dma[i].dev, cfg->dma[i].channel, &stat);
		if ((chunk_len != data->dma[i].count) && !stat.busy) {
			ret = i2c_gd32_dma_setup(dev, i);
			if (ret < 0) {
				goto on_error;
			}
		}
	}

on_error:
	if (ret < 0) {
		for (size_t i = 0; i < i2c_gd32_dma_enabled_num(dev); i++) {
			dma_stop(cfg->dma[i].dev, cfg->dma[i].channel);
		}
	}
	return ret;
}

static void i2c_gd32_complete(const struct device *dev, int status)
{
	struct i2c_gd32_data *data = dev->data;
	const struct i2c_gd32_config *cfg = dev->config;

	/* ADD IP: Disable DMA functionality */
	i2c_add_dma_disable(cfg->reg, I2C_ADD_DMA_TRANSMIT);
	i2c_add_dma_disable(cfg->reg, I2C_ADD_DMA_RECEIVE);

	/* Clear ADD IP related status flags */
	if (i2c_add_flag_get(cfg->reg, I2C_ADD_FLAG_TC)) {
		LOG_DBG("ADD IP: TC flag detected (auto-clear)");
	}
	if (i2c_add_flag_get(cfg->reg, I2C_ADD_FLAG_STPDET)) {
		i2c_add_flag_clear(cfg->reg, I2C_ADD_FLAG_STPDET);
		LOG_DBG("ADD IP: STPDET flag cleared");
	}
	if (i2c_add_flag_get(cfg->reg, I2C_ADD_FLAG_NACK)) {
		i2c_add_flag_clear(cfg->reg, I2C_ADD_FLAG_NACK);
		LOG_DBG("ADD IP: NACK flag cleared");
	}

	for (size_t i = 0; i < i2c_gd32_dma_enabled_num(dev); i++) {
		dma_stop(cfg->dma[i].dev, cfg->dma[i].channel);
	}

	/* Update context for completed transfer */
	data->current->len = 0;

	/* If status indicates error, mark it in data->errs */
	if (status < 0 && data->errs == 0U) {
		data->errs |= I2C_GD32_ERR_AERR; /* Generic error if not already set */
	}

	/* Signal completion via semaphore */
	k_sem_give(&data->sync_sem);
}

static bool i2c_gd32_chunk_transfer_finished(const struct device *dev)
{
	struct i2c_gd32_data *data = dev->data;
	struct i2c_gd32_dma_data *dma = data->dma;
	const size_t chunk_len = data->current->len;

	/* I2C is half-duplex: only one direction active at a time */
	if (data->current->flags & I2C_MSG_READ) {
		/* For read operations, only check RX DMA count */
		return (dma[RX].count >= chunk_len);
	}

	/* For write operations, only check TX DMA count */
	return (dma[TX].count >= chunk_len);
}

void i2c_gd32_dma_callback_gd(const struct device *dma_dev, void *arg, uint32_t channel, int status)
{
	const struct device *dev = (const struct device *)arg;
	const struct i2c_gd32_config *cfg = dev->config;
	struct i2c_gd32_data *data = dev->data;
	const size_t chunk_len = data->current->len;
	int err = 0;

	if (status < 0) {
		LOG_ERR("dma:%p ch:%d callback gets error: %d", dma_dev, channel, status);
		i2c_gd32_complete(dev, status);
		return;
	}
	/* Generate STOP signal in DMA transfer complete ISR per manual */
	if (data->current->flags & I2C_MSG_STOP) {
		/* Legacy IP read: Generate STOP immediately after DMA complete */
		i2c_add_stop_on_bus(cfg->reg);
		LOG_DBG("Legacy IP: STOP generated after DMA RX completion");
	}

	/* Check operation type and only process corresponding DMA completion */
	if ((data->current->flags & I2C_MSG_READ)) {
		bool is_rx_dma = false;

		/* Check if this callback is from RX DMA channel */
		if (dma_dev == cfg->dma[RX].dev && channel == cfg->dma[RX].channel) {
			is_rx_dma = true;
		}
		/* For read operations, only process RX DMA completion */
		if (!is_rx_dma) {
			LOG_DBG("DMA callback: Ignoring TX DMA completion for read operation");
			return;
		}
	} else {
		/* Write operation - only process TX DMA completion */
		bool is_tx_dma = false;

		/* Check if this callback is from TX DMA channel */
		if (dma_dev == cfg->dma[TX].dev && channel == cfg->dma[TX].channel) {
			is_tx_dma = true;
		}
		/* For write operations, only process TX DMA completion */
		if (!is_tx_dma) {
			LOG_DBG("DMA callback: Ignoring RX DMA completion for write operation");
			return;
		}
	}

	/* Check if I2C errors occurred during DMA transfer */
	if (data->errs != 0U) {
		/* Special handling for EEPROM write operations */
		bool is_eeprom_write =
			(data->addr1 == 0x50) && !(data->current->flags & I2C_MSG_READ);

		if (is_eeprom_write && (data->errs & I2C_GD32_ERR_AERR)) {
			/* EEPROM NACK during write - this is expected during
			 * internal write cycle
			 */
			LOG_DBG("EEPROM NACK in DMA callback (expected), "
				"will retry in transfer function");
			/* Don't treat as fatal error - let the transfer function handle retry */
			i2c_gd32_complete(dev, -EIO); /* This will trigger retry logic */
			return;
		}
		/* Other I2C errors are fatal */
		LOG_ERR("I2C error detected in DMA callback: 0x%02x, "
			"stopping transfer",
			data->errs);
		i2c_gd32_complete(dev, -EIO);
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(cfg->dma); i++) {
		if (dma_dev == cfg->dma[i].dev && channel == cfg->dma[i].channel) {
			LOG_DBG("DMA callback: dev=%p ch=%d, old_count=%d, chunk_len=%d", dma_dev,
				channel, data->dma[i].count, chunk_len);
			data->dma[i].count += chunk_len;
			LOG_DBG("DMA callback: new_count=%d", data->dma[i].count);
		}
	}

	/* Check if current chunk transfer finished */
	if (i2c_gd32_chunk_transfer_finished(dev)) {
		/* Generate STOP signal in DMA transfer complete ISR per manual */
		/* ADD IP: Handle STOP signal based on AUTOEND status and transfer type */
		if (data->current->flags & I2C_MSG_STOP) {
			/* Check if AUTOEND is enabled */
			if (!(I2C_ADD_CTL1(cfg->reg) & I2C_ADD_CTL1_AUTOEND)) {
				/* Send STOP signal manually */
				i2c_add_stop_on_bus(cfg->reg);
				LOG_DBG("ADD IP: Manual STOP generated after DMA completion");
			} else {

				LOG_DBG("ADD IP: AUTOEND enabled, "
					"STOP will be generated automatically");
			}
		}

		/* Update I2C message context - move to next message if available */
		data->current->len = 0;
		data->xfer_len -= chunk_len;

		/* Check if there are more messages in the transfer sequence */
		if (data->xfer_len > 0U &&
		    (data->current + 1) < (data->current + data->msg_count)) {
			/* Move to next message */
			data->current++;
			/* Reset DMA counts for next message */
			for (size_t i = 0; i < ARRAY_SIZE(data->dma); i++) {
				data->dma[i].count = 0;
			}
			/* Continue with next message */
			err = i2c_gd32_start_dma_transceive(dev);
			if (err) {
				i2c_gd32_complete(dev, err);
			}
		} else {
			/* All messages complete */
			i2c_gd32_complete(dev, 0);
		}
		return;
	}

	/* Continue with current chunk if not finished */
	err = i2c_gd32_start_dma_transceive(dev);
	if (err) {
		i2c_gd32_complete(dev, err);
	}
}
#endif /* CONFIG_I2C_GD32_DMA */

static int i2c_gd32_bus_recovery(const struct device *dev)
{
	const struct i2c_gd32_config *cfg = dev->config;
	struct i2c_gd32_data *data = dev->data;
	uint32_t retry_count = 0;

	/* Disable I2C peripheral */
	I2C_ADD_CTL0(cfg->reg) &= ~I2C_ADD_CTL0_I2CEN;
	/* Clear all status flags */
	i2c_add_flag_clear(cfg->reg, I2C_ADD_FLAG_NACK);
	i2c_add_flag_clear(cfg->reg, I2C_ADD_FLAG_BERR);
	i2c_add_flag_clear(cfg->reg, I2C_ADD_FLAG_LOSTARB);
	i2c_add_flag_clear(cfg->reg, I2C_ADD_FLAG_STPDET); /* Re-enable I2C peripheral */
	I2C_ADD_CTL0(cfg->reg) |= I2C_ADD_CTL0_I2CEN;

	/* Check if bus is now free */
	for (retry_count = 0; retry_count < 100; retry_count++) {
		if (!(I2C_ADD_STAT(cfg->reg) & I2C_ADD_STAT_I2CBSY)) {
			return 0;
		}
	}

	LOG_ERR("I2C bus recovery failed");
	data->errs |= I2C_GD32_ERR_BUSY;
	return -EBUSY;
}

static inline void i2c_gd32_enable_interrupts(const struct i2c_gd32_config *cfg)
{
	/* Enable ADD IP interrupts: error + address match + stop +
	 * transfer complete + NACK + TX/RX
	 */
	i2c_add_interrupt_enable(cfg->reg, I2C_ADD_INT_ERR);
	i2c_add_interrupt_enable(cfg->reg, I2C_ADD_INT_ADDM);
	i2c_add_interrupt_enable(cfg->reg, I2C_ADD_INT_STPDET);
	i2c_add_interrupt_enable(cfg->reg, I2C_ADD_INT_TC);
	i2c_add_interrupt_enable(cfg->reg, I2C_ADD_INT_NACK);
	i2c_add_interrupt_enable(cfg->reg, I2C_ADD_INT_TI);
	i2c_add_interrupt_enable(cfg->reg, I2C_ADD_INT_RBNE);
}

static inline void i2c_gd32_xfer_read(struct i2c_gd32_data *data, const struct i2c_gd32_config *cfg)
{
	data->current->len--;
	*data->current->buf = (uint8_t)i2c_add_data_receive(cfg->reg);

	data->current->buf++;
	if ((data->xfer_len > 0U) && (data->current->len == 0U)) {
		data->current++;
	}
}

static inline void i2c_gd32_xfer_write(struct i2c_gd32_data *data,
				       const struct i2c_gd32_config *cfg)
{
	data->current->len--;
	i2c_add_data_transmit(cfg->reg, *data->current->buf);

	data->current->buf++;
	if ((data->xfer_len > 0U) && (data->current->len == 0U)) {
		data->current++;
	}
}

void i2c_gd32_event_isr_gd(const struct device *dev)
{
	struct i2c_gd32_data *data = dev->data;
	const struct i2c_gd32_config *cfg = dev->config;
	uint32_t stat;

	stat = I2C_ADD_STAT(cfg->reg); /* Keep for performance, multiple flag checks */

	/* Master mode handling continues below... */
	/* Handle flags that may cause repeated ISR entries: NACK/STPDET must be cleared */
	if (stat & I2C_ADD_STAT_NACK) {
		i2c_add_flag_clear(cfg->reg, I2C_ADD_FLAG_NACK);
		data->errs |= I2C_GD32_ERR_AERR;
#ifdef CONFIG_I2C_GD32_DMA
		/* Stop DMA on NACK error */
		if (data->dma_enabled && data->current->len >= CONFIG_I2C_GD32_DMA_THRESHOLD &&
		    i2c_gd32_dma_enabled(dev)) {
			i2c_add_dma_disable(cfg->reg, I2C_ADD_DMA_TRANSMIT);
			i2c_add_dma_disable(cfg->reg, I2C_ADD_DMA_RECEIVE);
			for (size_t i = 0; i < i2c_gd32_dma_enabled_num(dev); i++) {
				dma_stop(cfg->dma[i].dev, cfg->dma[i].channel);
			}
		}
#endif
		if (data->errs != 0U) {
			i2c_add_stop_on_bus(cfg->reg);
			k_sem_give(&data->sync_sem);
		}
		return;
	}
	/* STOP detection for ADD IP */
	if (stat & I2C_ADD_STAT_STPDET) {
		/* Clear STOP detection flag */
		i2c_add_flag_clear(cfg->reg, I2C_ADD_FLAG_STPDET);
		LOG_DBG("ADD IP: STOP detected, transfer complete");
		i2c_add_automatic_end_disable(cfg->reg);
		/* Signal transfer completion */
		k_sem_give(&data->sync_sem);
		/* Disable interrupts */
		i2c_gd32_disable_interrupts(cfg);
		return;
	}

	/* RBNE: Receive buffer not empty - read data */
	if ((stat & I2C_ADD_STAT_RBNE) && (data->xfer_len > 0U)) {
		/* Read a data byte from I2C_RDATA */
		data->xfer_len--;
		i2c_gd32_xfer_read(data, cfg);
	}

	/* TI: Transmit interrupt - write data */
	if (stat & I2C_ADD_STAT_TI) {
		if (data->xfer_len > 0U) {
			/* Send a data byte */
			data->xfer_len--;
			i2c_gd32_xfer_write(data, cfg);
		}
	}

	/* TC: Transfer Complete */
	if ((stat & I2C_ADD_STAT_TC) && data->xfer_len == 0U) {
		if (data->add_has_stop) {
			/* All data transferred */
			i2c_add_stop_on_bus(cfg->reg);
		}
		k_sem_give(&data->sync_sem);
		i2c_gd32_disable_interrupts(cfg);
	}

	/* TCR: Transfer Complete Reload (for >255 bytes transfers) */
	if (stat & I2C_ADD_STAT_TCR) {
		/* Current segment complete, configure next segment */
		uint32_t seg = (data->xfer_len > 255U) ? 255U : data->xfer_len;

		i2c_add_transfer_byte_number_config(cfg->reg, (uint8_t)seg);

		/* Disable reload if this is the last segment */
		if (data->xfer_len <= 255U) {
			i2c_add_reload_disable(cfg->reg);
			if (data->add_has_stop) {
				i2c_add_automatic_end_enable(cfg->reg);
			}
		}
		/* Hardware automatically continues transfer after TCR, no START needed */
		/* Re-enable TI interrupt for write operations to send next segment */
		if (!(data->current->flags & I2C_MSG_READ)) {
			i2c_add_interrupt_enable(cfg->reg, I2C_ADD_INT_TI);
		}
	}
}

void i2c_gd32_error_isr_gd(const struct device *dev)
{
	struct i2c_gd32_data *data = dev->data;
	const struct i2c_gd32_config *cfg = dev->config;
	uint32_t stat;

	stat = I2C_ADD_STAT(cfg->reg);
	if (stat & I2C_ADD_STAT_BERR) {
		i2c_add_flag_clear(cfg->reg, I2C_ADD_FLAG_BERR);
		data->errs |= I2C_GD32_ERR_BERR;
	}
	if (stat & I2C_ADD_STAT_LOSTARB) {
		i2c_add_flag_clear(cfg->reg, I2C_ADD_FLAG_LOSTARB);
		data->errs |= I2C_GD32_ERR_LARB;
	}

	if (data->errs != 0U) {
		i2c_add_stop_on_bus(cfg->reg);
		k_sem_give(&data->sync_sem);
	}
}

static void i2c_gd32_log_err(struct i2c_gd32_data *data)
{
	if (data->errs & I2C_GD32_ERR_BERR) {
		LOG_ERR("Bus error");
	}

	if (data->errs & I2C_GD32_ERR_LARB) {
		LOG_ERR("Arbitration lost");
	}

	if (data->errs & I2C_GD32_ERR_AERR) {
		LOG_DBG("No ACK received");
	}

	if (data->errs & I2C_GD32_ERR_BUSY) {
		LOG_ERR("I2C bus busy");
	}

	if (data->errs & I2C_GD32_ERR_OVFL) {
		LOG_ERR("Transfer length overflow / unsupported sequence");
	}
}

#ifdef CONFIG_I2C_TARGET
static int i2c_gd32_target_register(const struct device *dev, struct i2c_target_config *target)
{
	struct i2c_gd32_data *data = dev->data;
	const struct i2c_gd32_config *cfg = dev->config;

	if (target == NULL || target->flags & I2C_TARGET_FLAGS_ADDR_10_BITS) {
		return -EINVAL; /* Only 7-bit */
	}

	/* Validate callback structure */
	if (target->callbacks == NULL) {
		LOG_ERR("I2C target callbacks cannot be NULL");
		return -EINVAL;
	}

	/* Validate callback pointer is in valid memory range */
	if ((uint32_t)target->callbacks < 0x20000000 || (uint32_t)target->callbacks >= 0x30000000) {
		LOG_ERR("I2C target callbacks outside valid memory range: %p", target->callbacks);
		return -EFAULT;
	}

	k_sem_take(&data->bus_mutex, K_FOREVER);
	if (data->target_cfg != NULL) {
		k_sem_give(&data->bus_mutex);
		return -EBUSY;
	}

	/* Configure hardware to respond to the address */
	uint32_t addr = target->address & 0x7FU;
	/* ADD IP target mode configuration */
	i2c_add_disable(cfg->reg);

	/* Configure primary address */
	i2c_add_address_config(cfg->reg, addr, I2C_ADD_ADDFORMAT_7BITS);

	/* Enable address matching interrupt */
	i2c_add_interrupt_enable(cfg->reg, I2C_ADD_INT_ADDM);
	i2c_add_interrupt_enable(cfg->reg, I2C_ADD_INT_RBNE);
	i2c_add_interrupt_enable(cfg->reg, I2C_ADD_INT_TI);
	i2c_add_interrupt_enable(cfg->reg, I2C_ADD_INT_STPDET);
	i2c_add_interrupt_enable(cfg->reg, I2C_ADD_INT_NACK);
	i2c_add_interrupt_enable(cfg->reg, I2C_ADD_INT_ERR);

	/* Enable stretching SCL when data not ready */
	i2c_add_stretch_scl_low_enable(cfg->reg);

	i2c_add_enable(cfg->reg);

	LOG_INF("I2C ADD target registered addr=0x%02x", addr);

	data->target_cfg = target;
	k_sem_give(&data->bus_mutex);

	return 0;
}

static int i2c_gd32_target_unregister(const struct device *dev, struct i2c_target_config *target)
{
	struct i2c_gd32_data *data = dev->data;
	const struct i2c_gd32_config *cfg = dev->config;
	int ret = 0;

	k_sem_take(&data->bus_mutex, K_FOREVER);
	if (data->target_cfg != target) {
		ret = -EINVAL;
	} else {
		/* Disable ADD IP target mode */
		i2c_add_disable(cfg->reg);
		i2c_add_address_disable(cfg->reg);
		/* Disable target interrupts */
		i2c_add_interrupt_disable(cfg->reg, I2C_ADD_INT_ADDM);
		i2c_add_interrupt_disable(cfg->reg, I2C_ADD_INT_RBNE);
		i2c_add_interrupt_disable(cfg->reg, I2C_ADD_INT_TI);
		i2c_add_interrupt_disable(cfg->reg, I2C_ADD_INT_STPDET);
		i2c_add_interrupt_disable(cfg->reg, I2C_ADD_INT_NACK);
		i2c_add_interrupt_disable(cfg->reg, I2C_ADD_INT_ERR);

		data->target_cfg = NULL;
	}
	k_sem_give(&data->bus_mutex);
	return ret;
}
#endif /* CONFIG_I2C_TARGET */

static void i2c_gd32_xfer_begin(const struct device *dev)
{
	struct i2c_gd32_data *data = dev->data;
	const struct i2c_gd32_config *cfg = dev->config;

	k_sem_reset(&data->sync_sem);

	data->errs = 0U;
	data->is_restart = false;

	bool addr10 = (data->dev_config & I2C_ADDR_10_BITS) != 0U;
	uint32_t total = data->xfer_len;

	if (total == 0U) {
		k_sem_give(&data->sync_sem);
		return;
	}

	/* 1. Wait for bus idle and check for stuck condition */
	uint32_t busy_retry = 10000;

	while ((I2C_ADD_STAT(cfg->reg) & I2C_ADD_STAT_I2CBSY) && busy_retry--) {
		/* NOP */
	}
	if (I2C_ADD_STAT(cfg->reg) & I2C_ADD_STAT_I2CBSY) {
		LOG_WRN("I2C bus stuck, attempting recovery");
		/* Try bus recovery */
		if (i2c_gd32_bus_recovery(dev) < 0) {
			data->errs |= I2C_GD32_ERR_BUSY;
			k_sem_give(&data->sync_sem);
			return;
		}
	}
	/* 2. Clear potential error and STOP detection flags */
	i2c_add_flag_clear(cfg->reg, I2C_ADD_FLAG_NACK);
	i2c_add_flag_clear(cfg->reg, I2C_ADD_FLAG_BERR);
	i2c_add_flag_clear(cfg->reg, I2C_ADD_FLAG_LOSTARB);
	i2c_add_flag_clear(cfg->reg, I2C_ADD_FLAG_STPDET);
	/* 3. Configure transfer parameters using GD32 ADD IP functions */
	data->add_has_stop = (data->current->flags & I2C_MSG_STOP) != 0U;
	uint32_t seg = (total > 255U) ? 255U : total;

	/* Configure 10-bit addressing mode */
	if (addr10) {
		i2c_add_address10_enable(cfg->reg);
	} else {
		i2c_add_address10_disable(cfg->reg);
	}

	/* Configure automatic end mode and reload */
	if (total > 255U) {
		/* Multi-segment transfer: enable reload mode */
		i2c_add_reload_enable(cfg->reg);
		i2c_add_automatic_end_disable(cfg->reg);
	} else if (data->add_has_stop) {
		/* Single segment with STOP: enable AUTOEND */
		i2c_add_automatic_end_enable(cfg->reg);
		i2c_add_reload_disable(cfg->reg);
	} else {
		/* Single segment without STOP (will use RESTART): disable both */
		i2c_add_automatic_end_disable(cfg->reg);
		i2c_add_reload_disable(cfg->reg);
	}

	/* Configure transfer byte number */
	i2c_add_transfer_byte_number_config(cfg->reg, (uint8_t)seg);

	/* Configure master addressing and direction */
	uint32_t address = addr10 ? (data->addr1 & 0x3FFU) : ((data->addr1 & 0x7FU) << 1);
	uint32_t direction = (data->current->flags & I2C_MSG_READ) ? I2C_ADD_MASTER_RECEIVE
								   : I2C_ADD_MASTER_TRANSMIT;

	i2c_add_master_addressing(cfg->reg, address, direction);

	/* DMA must be initialized before START bit is set per manual */
#ifdef CONFIG_I2C_GD32_DMA
	if (data->dma_enabled && data->current->len >= CONFIG_I2C_GD32_DMA_THRESHOLD &&
	    i2c_gd32_dma_enabled(dev)) {
		/* 2. Configure DMA specific interrupts (DENR/DENT already set) */
		i2c_gd32_enable_dma_interrupts(cfg);
		i2c_add_start_on_bus(cfg->reg);
		return;
	}
#endif
	i2c_gd32_enable_interrupts(cfg);
	i2c_add_start_on_bus(cfg->reg);
}

static int i2c_gd32_xfer_end(const struct device *dev)
{
	struct i2c_gd32_data *data = dev->data;
	const struct i2c_gd32_config *cfg = dev->config;

	i2c_gd32_disable_interrupts(cfg);
	if ((data->current->flags & I2C_MSG_STOP)) {
		while (I2C_ADD_STAT(cfg->reg) & I2C_ADD_STAT_I2CBSY) {
		}
	}
	/* Clear 10-bit mode (if set) */
	i2c_add_address10_disable(cfg->reg);
	i2c_add_address10_header_disable(cfg->reg);

	/* Restore target mode if target is registered */
#ifdef CONFIG_I2C_TARGET
	if (data->target_cfg != NULL) {
		uint32_t addr = data->target_cfg->address & 0x7FU;

		i2c_add_disable(cfg->reg);
		/* Reconfigure as target */
		i2c_add_address_config(cfg->reg, addr, I2C_ADD_ADDFORMAT_7BITS);
		/* Re-enable target interrupts */
		i2c_add_interrupt_enable(cfg->reg, I2C_ADD_INT_ADDM);
		i2c_add_interrupt_enable(cfg->reg, I2C_ADD_INT_RBNE);
		i2c_add_interrupt_enable(cfg->reg, I2C_ADD_INT_TI);
		i2c_add_interrupt_enable(cfg->reg, I2C_ADD_INT_STPDET);
		i2c_add_stretch_scl_low_enable(cfg->reg);
		i2c_add_enable(cfg->reg);
	}

	data->master_active = false;
#endif

	if (data->errs) {
		return -EIO;
	}

	return 0;
}

static int i2c_gd32_msg_read(const struct device *dev)
{
	struct i2c_gd32_data *data = dev->data;

#ifdef CONFIG_I2C_GD32_DMA
	const struct i2c_gd32_config *cfg = dev->config;
	int ret;
	/* Use DMA for transfers larger than threshold and if DMA is enabled */
	if (data->dma_enabled && data->current->len >= CONFIG_I2C_GD32_DMA_THRESHOLD &&
	    i2c_gd32_dma_enabled(dev)) {
		/* Additional safety checks before DMA transfer */
		if (!data->current->buf) {
			LOG_ERR("Invalid RX buffer pointer");
			return -EINVAL;
		}

		/* Ensure buffer is in valid memory range */
		if ((uint32_t)data->current->buf < 0x20000000 ||
		    (uint32_t)data->current->buf >= 0x30000000) {
			return -EFAULT;
		}
		/* Reset DMA counts */
		for (size_t i = 0; i < ARRAY_SIZE(data->dma); i++) {
			data->dma[i].count = 0;
		}
		/* Clear any previous I2C errors before starting DMA transfer */
		data->errs = 0U;

		i2c_add_dma_enable(cfg->reg, I2C_ADD_DMA_RECEIVE);
		ret = i2c_gd32_start_dma_transceive(dev);
		if (ret == 0) {
			/* Begin I2C transfer setup */
			i2c_gd32_xfer_begin(dev);
			/* Wait for DMA completion */
			ret = k_sem_take(&data->sync_sem, K_MSEC(CONFIG_I2C_GD32_DMA_TIMEOUT));
			if (ret == 0) {
				/* Check for I2C errors even if DMA completed */
				if (data->errs != 0U) {
					LOG_ERR("RX DMA completed but I2C errors detected: 0x%02x",
						data->errs);
					i2c_gd32_log_err(data);
					return i2c_gd32_xfer_end(dev);
				}
				/* Send STOP signal for successful DMA read completion */
				i2c_add_stop_on_bus(cfg->reg);
				return i2c_gd32_xfer_end(dev);
			}
			LOG_ERR("DMA RX transfer timeout, falling back to PIO");
			/* Disable DMA and fall through to PIO mode */
			i2c_gd32_complete(dev, -ETIMEDOUT);
		}
		/* If DMA start failed, fall back to PIO mode */
		LOG_WRN("DMA RX start failed, falling back to PIO mode");
	}
#endif

	/* PIO mode transfer */
	i2c_gd32_xfer_begin(dev);

	k_sem_take(&data->sync_sem, K_FOREVER);

	return i2c_gd32_xfer_end(dev);
}

static int i2c_gd32_msg_write(const struct device *dev)
{
	struct i2c_gd32_data *data = dev->data;

#ifdef CONFIG_I2C_GD32_DMA
	const struct i2c_gd32_config *cfg = dev->config;
	int ret = 0;
	/* Use DMA for transfers larger than threshold and if DMA is enabled */
	if (data->dma_enabled && data->current->len >= CONFIG_I2C_GD32_DMA_THRESHOLD &&
	    i2c_gd32_dma_enabled(dev)) {
		/* Additional safety checks before DMA transfer */
		if (!data->current->buf) {
			LOG_ERR("Invalid TX buffer pointer");
			return -EINVAL;
		}

		/* Ensure buffer is in valid memory range */
		if ((uint32_t)data->current->buf < 0x20000000 ||
		    (uint32_t)data->current->buf >= 0x30000000) {
			LOG_ERR("TX buffer outside SRAM range: 0x%08x",
				(uint32_t)data->current->buf);
			return -EFAULT;
		}

		/* Reset DMA counts */
		for (size_t i = 0; i < ARRAY_SIZE(data->dma); i++) {
			data->dma[i].count = 0;
		}

		/* Clear any previous I2C errors before starting DMA transfer */
		data->errs = 0U;
		/*  Configure DMA transfer direction before interrupt configuration */
		i2c_add_dma_enable(cfg->reg, I2C_ADD_DMA_TRANSMIT);
		/* Start DMA transfer */
		ret = i2c_gd32_start_dma_transceive(dev);
		if (ret == 0) {
			/* Begin I2C transfer setup */
			i2c_gd32_xfer_begin(dev);
			/* Wait for DMA completion */
			ret = k_sem_take(&data->sync_sem, K_MSEC(CONFIG_I2C_GD32_DMA_TIMEOUT));
			if (ret == 0) {
				/* Check for I2C errors even if DMA completed */
				if (data->errs != 0U) {
					i2c_gd32_log_err(data);
					return i2c_gd32_xfer_end(dev);
				}

				return i2c_gd32_xfer_end(dev);
			}
			LOG_ERR("DMA TX transfer timeout, falling back to PIO");
			i2c_gd32_complete(dev, -ETIMEDOUT);
		}
		/* If DMA start failed, fall back to PIO mode */
		LOG_WRN("DMA TX start failed, falling back to PIO mode");
	}
#endif

	/* PIO mode transfer */
	i2c_gd32_xfer_begin(dev);

	k_sem_take(&data->sync_sem, K_FOREVER);

	return i2c_gd32_xfer_end(dev);
}

int i2c_gd32_transfer_gd(const struct device *dev, struct i2c_msg *msgs, uint8_t num_msgs,
			 uint16_t addr)
{
	struct i2c_gd32_data *data = dev->data;
	const struct i2c_gd32_config *cfg = dev->config;
	struct i2c_msg *current, *next;
	uint8_t itr;
	int err = 0;

	current = msgs;

	/* First message flags implicitly contain I2C_MSG_RESTART flag. */
	current->flags |= I2C_MSG_RESTART;

	for (uint8_t i = 1; i <= num_msgs; i++) {

		if (i < num_msgs) {
			next = current + 1;
			/*
			 * If there have a R/W transfer state change between messages,
			 * An explicit I2C_MSG_RESTART flag is needed for the second message.
			 */
			if ((current->flags & I2C_MSG_RW_MASK) != (next->flags & I2C_MSG_RW_MASK)) {
				if ((next->flags & I2C_MSG_RESTART) == 0U) {
					return -EINVAL;
				}
			}

			/* Only the last message need I2C_MSG_STOP flag to free the Bus. */
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

	i2c_add_enable(cfg->reg);

	if (data->dev_config & I2C_ADDR_10_BITS) {
		data->addr1 = 0xF0 | ((addr & BITS(8, 9)) >> 8U);
		data->addr2 = addr & BITS(0, 7);
	} else {
		data->addr1 = addr & BITS(0, 6);
	}

	for (uint8_t i = 0; i < num_msgs; i = itr) {
		data->current = &msgs[i];
		data->xfer_len = msgs[i].len;

#ifdef CONFIG_I2C_GD32_DMA
		/* Set message count for DMA transfers */
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

		if (itr - i > 1) {
			if (msgs[itr - 1].flags & I2C_MSG_STOP) {
				data->current->flags |= I2C_MSG_STOP;
			}
		}

		if (data->current->flags & I2C_MSG_READ) {
			err = i2c_gd32_msg_read(dev);
		} else {
			/* Special handling for EEPROM write operations */
			err = i2c_gd32_msg_write(dev);
		}

		if (err < 0) {

			i2c_gd32_log_err(data);
			break;
		}
	}

	i2c_add_disable(cfg->reg);

	k_sem_give(&data->bus_mutex);

	return err;
}

int i2c_gd32_configure_gd(const struct device *dev, uint32_t dev_config)
{
	struct i2c_gd32_data *data = dev->data;
	const struct i2c_gd32_config *cfg = dev->config;
	uint32_t pclk1, freq;
	int err = 0;

	k_sem_take(&data->bus_mutex, K_FOREVER);

	i2c_add_disable(cfg->reg);

	(void)clock_control_get_rate(GD32_CLOCK_CONTROLLER, (clock_control_subsys_t)&cfg->clkid,
				     &pclk1);

	/* i2c clock frequency (MHz for legacy checks) */
	freq = pclk1 / 1000000U;
	if (freq > I2CCLK_MAX) {
		LOG_ERR("I2C max clock freq %u, current is %u\n", I2CCLK_MAX, freq);
		err = -ENOTSUP;
		goto error;
	}

	/* Select target bitrate */
	uint32_t bitrate_hz;
	bool fast_like = false;

	switch (I2C_SPEED_GET(dev_config)) {
	case I2C_SPEED_STANDARD:
		bitrate_hz = I2C_BITRATE_STANDARD; /* 100k */
		break;
	case I2C_SPEED_FAST:
		bitrate_hz = I2C_BITRATE_FAST; /* 400k */
		fast_like = true;
		break;
#ifdef I2C_FMPCFG
	case I2C_SPEED_FAST_PLUS:
		bitrate_hz = I2C_BITRATE_FAST_PLUS; /* 1M */
		fast_like = true;
		break;
#endif
	default:
		/* Check if a custom bitrate is specified in device tree */
		if (dev_config & I2C_SPEED_MASK) {
			/* Extract custom bitrate from dev_config if available */
			bitrate_hz = I2C_BITRATE_STANDARD; /* fallback to 100k */
			LOG_INF("ADD IP: Using default 100kHz for unsupported speed mode");
		} else {
			err = -EINVAL;
			goto error;
		}
		break;
	}

	/* Calculate prescaler PSC: make internal clock approximately bitrate * 8 */
	uint32_t target_internal = bitrate_hz * 8U;
	uint32_t psc = 0U;

	if (pclk1 > target_internal) {
		psc = pclk1 / target_internal;
		if (psc > 0U) {
			psc -= 1U; /* register stores PSC = divider-1 */
		}
		if (psc > 0x0F) {
			psc = 0x0F; /* 4 bits */
		}
	}

	/* Calculate SCL clock period */
	uint32_t ip_clk = pclk1 / (psc + 1U);
	uint32_t total = ip_clk / bitrate_hz;

	if (total < 4U) {
		total = 4U; /* guard */
	}
	if (total > 510U) { /* limit to 0xFF + 0xFF */
		total = 510U;
	}

	uint32_t sclh, scll;

	if (fast_like) {
		/* Fast/Fast+ mode: follow I2C specification timing
		 * requirements
		 */
		if (bitrate_hz >= 1000000U) {
			/* Fast+ mode: tLOW ≥ 0.5μs, tHIGH ≥ 0.26μs for 1MHz */
			uint32_t tlow_min_cycles = (500U * ip_clk) / (1000000000U / (psc + 1U));
			uint32_t thigh_min_cycles = (260U * ip_clk) / (1000000000U / (psc + 1U));

			scll = (tlow_min_cycles > total * 2U / 3U) ? tlow_min_cycles
								   : total * 2U / 3U;
			sclh = total - scll;
			if (sclh < thigh_min_cycles) {
				sclh = thigh_min_cycles;
				scll = total - sclh;
			}
		} else {
			/* Fast mode: tLOW ≥ 1.3μs, tHIGH ≥ 0.6μs for 400kHz */
			uint32_t tlow_min_cycles = (1300U * ip_clk) / (1000000000U / (psc + 1U));
			uint32_t thigh_min_cycles = (600U * ip_clk) / (1000000000U / (psc + 1U));

			scll = (tlow_min_cycles > total * 2U / 3U) ? tlow_min_cycles
								   : total * 2U / 3U;
			sclh = total - scll;
			if (sclh < thigh_min_cycles) {
				sclh = thigh_min_cycles;
				scll = total - sclh;
			}
		}
	} else {
		/* Standard mode: tLOW ≥ 4.7μs, tHIGH ≥ 4.0μs, typically 1:1 ratio */
		uint32_t tlow_min_cycles = (4700U * ip_clk) / (1000000000U / (psc + 1U));
		uint32_t thigh_min_cycles = (4000U * ip_clk) / (1000000000U / (psc + 1U));

		sclh = (thigh_min_cycles > total / 2U) ? thigh_min_cycles : total / 2U;
		scll = (tlow_min_cycles > total - sclh) ? tlow_min_cycles : total - sclh;
		/* Adjust if total doesn't fit */
		if (sclh + scll > total) {
			scll = total * 55U / 100U; /* 55% low, 45% high approximation */
			sclh = total - scll;
		}
	}

	/* Apply hardware limits and ensure non-zero values */
	if (sclh == 0U) {
		sclh = 1U;
	}
	if (scll == 0U) {
		scll = 1U;
	}
	if (sclh > 0xFF) {
		sclh = 0xFF;
	}
	if (scll > 0xFF) {
		scll = 0xFF;
	}

	uint32_t scl_dely, sda_dely;
	uint32_t t_psc = (psc + 1U);                  /* PSC+1 factor */
	uint32_t t_i2c_clk_ns = 1000000000U / ip_clk; /* ns per I2C internal clock */

	if (fast_like) {
		/* Fast/Fast+ mode timing parameters from GD32F5xx manual table 24-5 */
		uint32_t tsu_dat_min_ns, taf_max_ns, tvd_dat_max_ns;

		if (bitrate_hz >= 1000000U) {
			/* Fast+ mode (1MHz) */
			tsu_dat_min_ns = 50U;  /* tSU,DAT min = 50ns */
			taf_max_ns = 120U;     /* tAF max = 120ns */
			tvd_dat_max_ns = 450U; /* tVD,DAT max = 0.45μs = 450ns */
		} else {
			/* Fast mode (400kHz) */
			tsu_dat_min_ns = 100U; /* tSU,DAT min = 100ns */
			taf_max_ns = 300U;     /* tAF max = 300ns */
			tvd_dat_max_ns = 900U; /* tVD,DAT max = 0.9μs = 900ns */
		}

		/* SCL delay calculation: tSCLDELY ≥ {tSU,DAT(min)}/[(PSC+1)*tI2CCLK]} - 1 */
		scl_dely = (tsu_dat_min_ns + (t_psc * t_i2c_clk_ns / 2)) / (t_psc * t_i2c_clk_ns);
		if (scl_dely > 0U) {
			scl_dely -= 1U; /* Per manual: subtract 1 */
		}

		/* SDA delay calculation: Consider tVD,DAT and tAF per manual formula */
		/* tSDADELY ≥ {tVD,DAT(max) + tAF(max) - [(DNF+3)*tI2CCLK]}/[(PSC+1)*tI2CCLK] */
		/* Assume DNF = 0 (digital noise filter disabled) */
		uint32_t dnf_compensation = 3U * t_i2c_clk_ns; /* (DNF+3)*tI2CCLK with DNF=0 */
		uint32_t total_delay = tvd_dat_max_ns + taf_max_ns;

		if (total_delay > dnf_compensation) {
			sda_dely = (total_delay - dnf_compensation + (t_psc * t_i2c_clk_ns / 2)) /
				   (t_psc * t_i2c_clk_ns);
		} else {
			sda_dely = 0U;
		}
	} else {
		/* Standard mode timing parameters from GD32F5xx manual table 24-5 */
		uint32_t tsu_dat_min_ns = 250U;  /* tSU,DAT min = 250ns */
		uint32_t taf_max_ns = 1000U;     /* tAF max = 1000ns */
		uint32_t tvd_dat_max_ns = 3450U; /* tVD,DAT max = 3.45μs = 3450ns */

		/* SCL delay calculation for Standard mode */
		scl_dely = (tsu_dat_min_ns + (t_psc * t_i2c_clk_ns / 2)) / (t_psc * t_i2c_clk_ns);
		if (scl_dely > 0U) {
			scl_dely -= 1U;
		}

		/* SDA delay calculation for Standard mode */
		uint32_t dnf_compensation = 3U * t_i2c_clk_ns;
		uint32_t total_delay = tvd_dat_max_ns + taf_max_ns;

		if (total_delay > dnf_compensation) {
			sda_dely = (total_delay - dnf_compensation + (t_psc * t_i2c_clk_ns / 2)) /
				   (t_psc * t_i2c_clk_ns);

		} else {
			sda_dely = 0U;
		}
	}

	/* Apply hardware constraints */
	if (scl_dely > 0x0F) {
		scl_dely = 0x0F; /* 4-bit field */
	}
	if (sda_dely > 0x0F) {
		sda_dely = 0x0F; /* 4-bit field */
	}

	/* Ensure minimum values per manual requirements */
	if (scl_dely == 0U) {
		scl_dely = 1U;
	}
	if (sda_dely == 0U) {
		sda_dely = 1U;
	}

	/* Configure timing parameters using calculated values */
	i2c_add_timing_config(cfg->reg, psc, scl_dely, sda_dely);
	i2c_add_master_clock_config(cfg->reg, sclh, scll);

	/* Re-enable */
	i2c_add_enable(cfg->reg);
	data->dev_config = dev_config;
	goto done;

error:
	k_sem_give(&data->bus_mutex);
	return err;

done:
	k_sem_give(&data->bus_mutex);
	return err;
}
