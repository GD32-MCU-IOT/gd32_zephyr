/*
 * Copyright (c) 2021 BrainCo Inc.
 * Copyright (c) 2025 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/irq.h>
#include <zephyr/logging/log.h>

#include <errno.h>
LOG_MODULE_REGISTER(i2c_gd32, CONFIG_I2C_LOG_LEVEL);

#include "i2c-priv.h"
#include "i2c_gd32.h"

#ifdef CONFIG_I2C_GD32_DMA
static uint32_t dummy_tx;
static uint32_t dummy_rx;
static inline void i2c_gd32_enable_dma_interrupts(const struct i2c_gd32_config *cfg)
{
	/* Legacy IP: Enable error and event interrupts only */
	i2c_interrupt_enable(cfg->reg, I2C_INT_ERR); /* Error interrupt */
	i2c_interrupt_enable(cfg->reg, I2C_INT_EV);  /* Event interrupt */
	/* Disable buffer interrupts - DMA handles data transfer */
	i2c_interrupt_disable(cfg->reg, I2C_INT_BUF); /* Disable buffer interrupt (TBE/RBNE) */
	/* DMAON bit will be set after ADDSEND event handling per manual */
}
#endif

static inline void i2c_gd32_disable_interrupts(const struct i2c_gd32_config *cfg)
{
	/* Legacy IP: Disable all I2C interrupts */
	i2c_interrupt_disable(cfg->reg, I2C_INT_ERR); /* Error interrupt */
	i2c_interrupt_disable(cfg->reg, I2C_INT_EV);  /* Event interrupt */
	i2c_interrupt_disable(cfg->reg, I2C_INT_BUF); /* Buffer interrupt */
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
	/* Set DMALST bit for read operations with length >= 2 per manual */
	if (dir == RX && data->current->len >= 2 && (data->current->flags & I2C_MSG_READ)) {
		i2c_dma_last_transfer_config(cfg->reg, I2C_DMALST_ON);
		LOG_DBG("Legacy IP: DMALST set for multi-byte reception");
	}
	if (dir == TX) {
		block_cfg->dest_address = (uint32_t)&I2C_DATA(cfg->reg);
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
		block_cfg->source_address = (uint32_t)&I2C_DATA(cfg->reg);
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

	/* Generate STOP signal in DMA transfer complete ISR per manual */
	if (data->current->flags & I2C_MSG_STOP) {
		/* Legacy IP read: Generate STOP immediately after DMA complete */
		i2c_stop_on_bus(cfg->reg);
		LOG_DBG("Legacy IP: STOP generated after DMA RX completion");
	}
	/* Legacy IP: Disable DMA functionality and clear DMALST bit */
	i2c_dma_config(cfg->reg, I2C_DMA_OFF);
	/* Clear DMALST bit if it was set previously */
	if (data->current && (data->current->flags & I2C_MSG_READ) && data->current->len >= 2) {
		i2c_dma_last_transfer_config(cfg->reg, I2C_DMALST_OFF);
		LOG_DBG("Legacy IP: DMALST cleared after DMA completion");
	}
	LOG_DBG("Legacy IP: DMAON disabled after transfer completion");

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

static inline void i2c_gd32_enable_interrupts(const struct i2c_gd32_config *cfg)
{
	i2c_interrupt_enable(cfg->reg, I2C_INT_ERR);
	i2c_interrupt_enable(cfg->reg, I2C_INT_EV);
}

static inline void i2c_gd32_xfer_read(struct i2c_gd32_data *data, const struct i2c_gd32_config *cfg)
{
	data->current->len--;
	*data->current->buf = i2c_data_receive(cfg->reg);
	data->current->buf++;
	if ((data->xfer_len > 0U) && (data->current->len == 0U)) {
		data->current++;
	}
}

static inline void i2c_gd32_xfer_write(struct i2c_gd32_data *data,
				       const struct i2c_gd32_config *cfg)
{
	data->current->len--;
	i2c_data_transmit(cfg->reg, *data->current->buf);
	data->current->buf++;

	if ((data->xfer_len > 0U) && (data->current->len == 0U)) {
		data->current++;
	}
}

static void i2c_gd32_handle_rbne(const struct device *dev)
{
	struct i2c_gd32_data *data = dev->data;
	const struct i2c_gd32_config *cfg = dev->config;

#ifdef CONFIG_I2C_GD32_DMA
	/* This function should not be called in DMA mode */
	if (data->dma_enabled && data->current->len >= CONFIG_I2C_GD32_DMA_THRESHOLD &&
	    i2c_gd32_dma_enabled(dev)) {
		i2c_interrupt_disable(cfg->reg, I2C_INT_BUF);
		return;
	}
#endif

	switch (data->xfer_len) {
	case 0:
		/* Unwanted data received, ignore it. */
		k_sem_give(&data->sync_sem);
		break;
	case 1:
		/* Only one byte remains: read and finish. */
		data->xfer_len--;
		i2c_gd32_xfer_read(data, cfg);
		k_sem_give(&data->sync_sem);
		break;
	case 2:
		__fallthrough;
	case 3:
		i2c_interrupt_disable(cfg->reg, I2C_INT_BUF);
		break;
	default:
		/* More than 3 bytes remaining: read immediately. */
		data->xfer_len--;
		i2c_gd32_xfer_read(data, cfg);
		break;
	}
}

static void i2c_gd32_handle_tbe(const struct device *dev)
{
	struct i2c_gd32_data *data = dev->data;
	const struct i2c_gd32_config *cfg = dev->config;

#ifdef CONFIG_I2C_GD32_DMA
	/* This function should not be called in DMA mode */
	if (data->dma_enabled && data->current->len >= CONFIG_I2C_GD32_DMA_THRESHOLD &&
	    i2c_gd32_dma_enabled(dev)) {
		i2c_interrupt_disable(cfg->reg, I2C_INT_BUF);
		return;
	}
#endif

	if (data->xfer_len > 0U) {
		data->xfer_len--;
		if (data->xfer_len == 0U) {
			/*
			 * This is the last data to transmit, disable the TBE interrupt.
			 * Use the BTC interrupt to indicate the write data complete state.
			 */
			i2c_interrupt_disable(cfg->reg, I2C_INT_BUF);
		}
		i2c_gd32_xfer_write(data, cfg);

	} else {
		if (data->current->flags & I2C_MSG_STOP) {
			/* Generate STOP for final single byte */
			i2c_stop_on_bus(cfg->reg);
		} else {
			i2c_interrupt_disable(cfg->reg, I2C_INT_EV);
		}
		k_sem_give(&data->sync_sem);
	}
}

static void i2c_gd32_handle_btc(const struct device *dev)
{
	struct i2c_gd32_data *data = dev->data;
	const struct i2c_gd32_config *cfg = dev->config;

	if (data->current->flags & I2C_MSG_READ) {
		uint32_t counter = 0U;

		switch (data->xfer_len) {
		case 2:
			/* Generate STOP before reading last two bytes (legacy HAL) */
			i2c_stop_on_bus(cfg->reg);

			for (counter = 2U; counter > 0; counter--) {
				data->xfer_len--;
				i2c_gd32_xfer_read(data, cfg);
			}

			k_sem_give(&data->sync_sem);

			break;
		case 3:
			/* Disable ACK (prepare for last two bytes) */
			i2c_ack_config(cfg->reg, I2C_ACK_DISABLE);

			data->xfer_len--;
			i2c_gd32_xfer_read(data, cfg);

			break;
		default:
			i2c_gd32_handle_rbne(dev);
			break;
		}
	} else {
		i2c_gd32_handle_tbe(dev);
	}
}

static void i2c_gd32_handle_addsend(const struct device *dev)
{
	struct i2c_gd32_data *data = dev->data;
	const struct i2c_gd32_config *cfg = dev->config;

	if ((data->current->flags & I2C_MSG_READ) && (data->xfer_len <= 2U)) {
		/* Disable ACK for 1 or 2 byte reception */
		i2c_ack_config(cfg->reg, I2C_ACK_DISABLE);
	}

	/* Clear ADDSEND bit */
	i2c_flag_clear(cfg->reg, I2C_FLAG_ADDSEND);

#ifdef CONFIG_I2C_GD32_DMA
	/* Set DMAON bit after clearing ADDSEND in DMA mode per manual */
	if (data->dma_enabled && data->current->len >= CONFIG_I2C_GD32_DMA_THRESHOLD &&
	    i2c_gd32_dma_enabled(dev)) {
		/* DMAON bit must be set after ADDSEND is cleared per manual */
		i2c_dma_config(cfg->reg, I2C_DMA_ON);

		/* Special handling for single byte read */
		if ((data->current->flags & I2C_MSG_READ) && (data->xfer_len == 1U)) {
			/* Single byte read: set NACK and STOP immediately */
			i2c_ack_config(cfg->reg, I2C_ACK_DISABLE);
			i2c_stop_on_bus(cfg->reg);
			LOG_DBG("Legacy IP: Single byte read - NACK and STOP set");
		}
		return;
	}
#endif

	if (data->is_restart) {
		data->is_restart = false;
		data->current->flags &= ~I2C_MSG_RW_MASK;
		data->current->flags |= I2C_MSG_READ;
		/* Enter repeated start condition via HAL */
		i2c_start_on_bus(cfg->reg);
		return;
	}

	if ((data->current->flags & I2C_MSG_READ) && (data->xfer_len == 1U)) {
		/* Generate STOP for final single byte */
		i2c_stop_on_bus(cfg->reg);
	}
}

void i2c_gd32_event_isr_gd(const struct device *dev)
{
	struct i2c_gd32_data *data = dev->data;
	const struct i2c_gd32_config *cfg = dev->config;
	uint32_t stat;

	stat = I2C_STAT0(cfg->reg);

#ifdef CONFIG_I2C_TARGET
	/* Legacy target mode handling (only if no active master transfer) */
	if (GD32_I2C_IS_LEGACY(cfg->reg) && data->target_cfg && !data->master_active &&
	    data->target_cfg->callbacks) {
		/* Check addressing */
		if (stat & I2C_STAT0_ADDSEND) {
			/* Clear ADDSEND by reading STAT0 then STAT1 */
			I2C_STAT0(cfg->reg);
			I2C_STAT1(cfg->reg);
			if (data->target_cfg->callbacks &&
			    data->target_cfg->callbacks->write_requested) {
				data->target_cfg->callbacks->write_requested(data->target_cfg);
			}
		}
		/* Data received from controller */
		if (stat & I2C_STAT0_RBNE) {
			uint8_t v = i2c_data_receive(cfg->reg);

			if (data->target_cfg->callbacks &&
			    data->target_cfg->callbacks->write_received) {
				data->target_cfg->callbacks->write_received(data->target_cfg, v);
			}
		}
		/* Ready to transmit to controller */
		if (stat & I2C_STAT0_TBE) {
			uint8_t out = 0xFF;
			bool provide = false;

			if (data->target_cfg->callbacks &&
			    data->target_cfg->callbacks->read_requested) {
				provide = data->target_cfg->callbacks->read_requested(
					data->target_cfg, &out);
			}
			if (!provide && data->target_cfg->callbacks &&
			    data->target_cfg->callbacks->read_processed) {
				/* Fallback to processed callback if primary not supplied */
				provide = data->target_cfg->callbacks->read_processed(
					data->target_cfg, &out);
			}
			if (!provide) {
				out = 0xFF; /* default */
			}
			i2c_data_transmit(cfg->reg, out);
		}
		/* Stop condition (in slave mode signaled as STPDET in STAT0) */
		if (i2c_flag_get(cfg->reg, I2C_FLAG_STPDET)) {
			/* Clear STPDET flag using HAL */
			i2c_flag_clear(cfg->reg, I2C_FLAG_STPDET);
			i2c_stop_on_bus(cfg->reg); /* ensure bus freed */
			if (data->target_cfg->callbacks && data->target_cfg->callbacks->stop) {
				data->target_cfg->callbacks->stop(data->target_cfg);
			}
		}
		return; /* Target-mode handled */
	}
#endif /* CONFIG_I2C_TARGET */

	if (stat & I2C_STAT0_SBSEND) {
		/* Start bit sent: send address + direction via HAL */
		if (data->current->flags & I2C_MSG_READ) {
			i2c_master_addressing(cfg->reg, (data->addr1 << 1U), I2C_RECEIVER);
#ifndef CONFIG_I2C_GD32_DMA
			i2c_interrupt_enable(cfg->reg, I2C_INT_BUF);
#endif
		} else {
			i2c_master_addressing(cfg->reg, (data->addr1 << 1U), I2C_TRANSMITTER);
		}
	} else if (stat & I2C_STAT0_ADD10SEND) {
		/* Second part of 10-bit address */
		i2c_master_addressing(cfg->reg, data->addr2,
				      (data->current->flags & I2C_MSG_READ) ? I2C_RECEIVER
									    : I2C_TRANSMITTER);
	} else if (stat & I2C_STAT0_ADDSEND) {
		i2c_gd32_handle_addsend(dev);
#ifdef CONFIG_I2C_GD32_DMA
		/* Skip data transfer interrupts (TBE/RBNE) in DMA mode */
		if (data->dma_enabled && data->current->len >= CONFIG_I2C_GD32_DMA_THRESHOLD &&
		    i2c_gd32_dma_enabled(dev)) {
			/* Handle only BTC interrupt for transfer control */
			i2c_interrupt_disable(cfg->reg, I2C_INT_EV); /* Event interrupt */
			return; /* Skip other data interrupts in DMA mode */
		}
#endif
	}

	/* Data transfer handling for PIO mode */
	if (stat & I2C_STAT0_BTC) {
		i2c_gd32_handle_btc(dev);
	} else if (stat & I2C_STAT0_RBNE) {
		i2c_gd32_handle_rbne(dev);
	} else if (stat & I2C_STAT0_TBE) {
		i2c_gd32_handle_tbe(dev);
	}
}

void i2c_gd32_error_isr_gd(const struct device *dev)
{
	struct i2c_gd32_data *data = dev->data;
	const struct i2c_gd32_config *cfg = dev->config;
	uint32_t stat;

	stat = I2C_STAT0(cfg->reg);

	if (stat & I2C_STAT0_BERR) {
		/* Clear bus error flag using HAL */
		i2c_flag_clear(cfg->reg, I2C_FLAG_BERR);
		data->errs |= I2C_GD32_ERR_BERR;
	}

	if (stat & I2C_STAT0_LOSTARB) {
		i2c_flag_clear(cfg->reg, I2C_FLAG_LOSTARB);
		data->errs |= I2C_GD32_ERR_LARB;
	}

	if (stat & I2C_STAT0_AERR) {
		i2c_flag_clear(cfg->reg, I2C_FLAG_AERR);
		data->errs |= I2C_GD32_ERR_AERR;
#ifdef CONFIG_I2C_GD32_DMA
		/* Stop DMA on NACK error */
		if (data->dma_enabled && data->current->len >= CONFIG_I2C_GD32_DMA_THRESHOLD &&
		    i2c_gd32_dma_enabled(dev)) {
			i2c_dma_config(cfg->reg, I2C_DMA_OFF);
			for (size_t i = 0; i < i2c_gd32_dma_enabled_num(dev); i++) {
				dma_stop(cfg->dma[i].dev, cfg->dma[i].channel);
			}
		}
#endif
	}

	if (data->errs != 0U) {
		/* Enter stop condition */
		i2c_stop_on_bus(cfg->reg);
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
	/* Legacy IP target mode configuration */
	i2c_disable(cfg->reg);
	/* Put peripheral in target mode (stay in I2C mode, 7-bit) */
	i2c_mode_addr_config(cfg->reg, I2C_I2CMODE_ENABLE, I2C_ADDFORMAT_7BITS, addr);
	i2c_enable(cfg->reg);

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
		data->target_cfg = NULL;
	}
	k_sem_give(&data->bus_mutex);
	return ret;
}
#endif /* CONFIG_I2C_TARGET */

static int i2c_gd32_bus_recovery(const struct device *dev)
{
	const struct i2c_gd32_config *cfg = dev->config;
	struct i2c_gd32_data *data = dev->data;

	/* Disable I2C peripheral */
	i2c_disable(cfg->reg);

	/* Software reset - set then clear SRESET bit */
	i2c_software_reset_config(cfg->reg, I2C_SRESET_SET);
	i2c_software_reset_config(cfg->reg, I2C_SRESET_RESET);

	/* Reconfigure I2C clock parameters after reset */
	switch (I2C_SPEED_GET(data->dev_config)) {
	case I2C_SPEED_STANDARD:
		i2c_clock_config(cfg->reg, I2C_BITRATE_STANDARD, I2C_DTCY_2);
		break;
	case I2C_SPEED_FAST:
		i2c_clock_config(cfg->reg, I2C_BITRATE_FAST, I2C_DTCY_16_9);
#ifdef I2C_FMPCFG
		I2C_FMPCFG(cfg->reg) &= ~I2C_FMPCFG_FMPEN;
#endif
		break;
#ifdef I2C_FMPCFG
	case I2C_SPEED_FAST_PLUS:
		i2c_clock_config(cfg->reg, I2C_BITRATE_FAST_PLUS, I2C_DTCY_16_9);
		I2C_FMPCFG(cfg->reg) |= I2C_FMPCFG_FMPEN;
		break;
#endif
	default:
		break;
	}

	/* Re-enable I2C peripheral */
	i2c_enable(cfg->reg);

	/* Check if bus is now free */

	if (!i2c_flag_get(cfg->reg, I2C_FLAG_I2CBSY)) {
		return 0;
	}

	LOG_ERR("I2C bus recovery failed");
	data->errs |= I2C_GD32_ERR_BUSY;
	return -EBUSY;
}

static void i2c_gd32_xfer_begin(const struct device *dev)
{
	struct i2c_gd32_data *data = dev->data;
	const struct i2c_gd32_config *cfg = dev->config;

	k_sem_reset(&data->sync_sem);

	data->errs = 0U;
	data->is_restart = false;
	/* Legacy */
#ifdef CONFIG_I2C_TARGET
	data->master_active = true;
#endif
	if ((data->current->flags & I2C_MSG_READ) == 0) {
		/* Wait for bus idle and check for stuck condition */
		uint32_t busy_retry = 10000;

		while (i2c_flag_get(cfg->reg, I2C_FLAG_I2CBSY) && busy_retry--) {
			/* NOP */
		}
		if (i2c_flag_get(cfg->reg, I2C_FLAG_I2CBSY)) {
			LOG_WRN("I2C bus stuck, attempting recovery");
			/* Try bus recovery */
			if (i2c_gd32_bus_recovery(dev) < 0) {
				data->errs |= I2C_GD32_ERR_BUSY;
				k_sem_give(&data->sync_sem);
				return;
			}
		}
	}

	/* Configure as I2C master mode */
	i2c_mode_addr_config(cfg->reg, I2C_I2CMODE_ENABLE, I2C_ADDFORMAT_7BITS, 0);
	i2c_enable(cfg->reg);

	i2c_ack_config(cfg->reg, I2C_ACK_ENABLE);
	if (data->current->flags & I2C_MSG_READ) {
		if (data->xfer_len == 2U) {
			/* Configure ACK position for 2-byte read */
			i2c_ackpos_config(cfg->reg, I2C_ACKPOS_NEXT);
		}
		if (data->dev_config & I2C_ADDR_10_BITS) {
			data->is_restart = true;
			data->current->flags &= ~I2C_MSG_RW_MASK;
		}
	}
	/* Use DMA-specific interrupt configuration if DMA is active */
#ifdef CONFIG_I2C_GD32_DMA
	if (data->dma_enabled && data->current->len >= CONFIG_I2C_GD32_DMA_THRESHOLD &&
	    i2c_gd32_dma_enabled(dev)) {
		i2c_gd32_enable_dma_interrupts(cfg);
		i2c_start_on_bus(cfg->reg);
		return;
	}
#endif
	i2c_gd32_enable_interrupts(cfg);
	if (data->current->flags & I2C_MSG_WRITE) {
		i2c_interrupt_enable(cfg->reg, I2C_INT_BUF);
	}
	i2c_start_on_bus(cfg->reg);
}

static int i2c_gd32_xfer_end(const struct device *dev)
{
	struct i2c_gd32_data *data = dev->data;
	const struct i2c_gd32_config *cfg = dev->config;

	i2c_gd32_disable_interrupts(cfg);

	if ((data->current->flags & I2C_MSG_STOP) || (data->errs != 0U)) {
		while (i2c_flag_get(cfg->reg, I2C_FLAG_I2CBSY)) {
			/* NOP */
		}
	} else {
		(void)i2c_data_receive(cfg->reg);
	}
	/* Restore target mode if target is registered */
#ifdef CONFIG_I2C_TARGET
	if (data->target_cfg != NULL) {
		uint32_t addr = data->target_cfg->address & 0x7FU;

		i2c_disable(cfg->reg);
		/* Put peripheral back in target mode */
		i2c_mode_addr_config(cfg->reg, I2C_I2CMODE_ENABLE, I2C_ADDFORMAT_7BITS, addr);
		i2c_enable(cfg->reg);
	}
#endif
	if (data->errs) {
		return -EIO;
	}
#ifdef CONFIG_I2C_TARGET
	data->master_active = false;
#endif
	return 0;
}

static int i2c_gd32_msg_read(const struct device *dev)
{
	struct i2c_gd32_data *data = dev->data;

#ifdef CONFIG_I2C_GD32_DMA
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
			LOG_ERR("RX buffer outside SRAM range: 0x%08x",
				(uint32_t)data->current->buf);
			return -EFAULT;
		}
		/* Reset DMA counts */
		for (size_t i = 0; i < ARRAY_SIZE(data->dma); i++) {
			data->dma[i].count = 0;
		}
		/* Clear any previous I2C errors before starting DMA transfer */
		data->errs = 0U;
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
					LOG_ERR("RX DMA completed but I2C errors detected: 0x%02x",
						data->errs);
					i2c_gd32_log_err(data);
					return i2c_gd32_xfer_end(dev);
				}
				/* Send STOP signal for successful DMA read completion */
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

	i2c_enable(cfg->reg);

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

		/* If we grouped several same-direction msgs, carry STOP flag from the last one */
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

	i2c_disable(cfg->reg);

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

	/* Disable legacy peripheral */
	i2c_disable(cfg->reg);

	(void)clock_control_get_rate(GD32_CLOCK_CONTROLLER, (clock_control_subsys_t)&cfg->clkid,
				     &pclk1);

	/* i2c clock frequency (MHz for legacy checks) */
	freq = pclk1 / 1000000U;
	if (freq > I2CCLK_MAX) {
		LOG_ERR("I2C max clock freq %u, current is %u\n", I2CCLK_MAX, freq);
		err = -ENOTSUP;
		goto error;
	}

	switch (I2C_SPEED_GET(dev_config)) {
	case I2C_SPEED_STANDARD:
		if (freq < I2CCLK_MIN) {
			LOG_ERR("I2C standard-mode min clock freq %u, current is %u\n", I2CCLK_MIN,
				freq);
			err = -ENOTSUP;
			goto error;
		}
		/* Use HAL function for clock configuration */
		i2c_clock_config(cfg->reg, I2C_BITRATE_STANDARD, I2C_DTCY_2);

		break;
	case I2C_SPEED_FAST:
		if (freq < I2CCLK_FM_MIN) {
			LOG_ERR("I2C fast-mode min clock freq %u, current is %u\n", I2CCLK_FM_MIN,
				freq);
			err = -ENOTSUP;
			goto error;
		}

		/* Use HAL function for fast mode clock configuration */
		i2c_clock_config(cfg->reg, I2C_BITRATE_FAST, I2C_DTCY_16_9);

#ifdef I2C_FMPCFG
		/* Disable transfer mode: fast-mode plus */
		I2C_FMPCFG(cfg->reg) &= ~I2C_FMPCFG_FMPEN;
#endif /* I2C_FMPCFG */

		break;
#ifdef I2C_FMPCFG
	case I2C_SPEED_FAST_PLUS:
		if (freq < I2CCLK_FM_PLUS_MIN) {
			LOG_ERR("I2C fast-mode plus min clock freq %u, current is %u\n",
				I2CCLK_FM_PLUS_MIN, freq);
			err = -ENOTSUP;
			goto error;
		}

		/* Use HAL function for fast mode plus clock configuration */
		i2c_clock_config(cfg->reg, I2C_BITRATE_FAST_PLUS, I2C_DTCY_16_9);

		/* Enable transfer mode: fast-mode plus */
		I2C_FMPCFG(cfg->reg) |= I2C_FMPCFG_FMPEN;

		break;
#endif /* I2C_FMPCFG */
	default:
		err = -EINVAL;
		goto error;
	}

	data->dev_config = dev_config;
error:
	k_sem_give(&data->bus_mutex);

	return err;
}

#ifdef CONFIG_I2C_GD32_DMA
/* Debug helper functions for runtime DMA control */

/**
 * @brief Disable DMA for a specific I2C device (for debugging)
 * @param dev I2C device
 */
void i2c_gd32_disable_dma(const struct device *dev)
{
	struct i2c_gd32_data *data = dev->data;

	data->dma_enabled = false;
	LOG_INF("DMA disabled for I2C device %s", dev->name);
}

/**
 * @brief Enable DMA for a specific I2C device
 * @param dev I2C device
 */
void i2c_gd32_enable_dma(const struct device *dev)
{
	struct i2c_gd32_data *data = dev->data;
	const struct i2c_gd32_config *cfg = dev->config;

	if (cfg->dma[TX].dev || cfg->dma[RX].dev) {
		data->dma_enabled = true;
		LOG_INF("DMA enabled for I2C device %s", dev->name);
	} else {
		LOG_WRN("DMA not available for I2C device %s", dev->name);
	}
}

/**
 * @brief Get DMA status for a specific I2C device
 * @param dev I2C device
 * @return true if DMA is enabled, false otherwise
 */
bool i2c_gd32_is_dma_enabled(const struct device *dev)
{
	struct i2c_gd32_data *data = dev->data;

	return data->dma_enabled;
}
#endif /* CONFIG_I2C_GD32_DMA */
