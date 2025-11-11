/*
 * Copyright (c) 2021, ATL Electronics
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT gd_gd32_usart

#include <errno.h>

#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/gd32.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(usart_gd32, CONFIG_UART_LOG_LEVEL);

#include <gd32_usart.h>
#include "usart_gd32.h"

/* Unify GD32 HAL USART status register name to USART_STAT */
#ifndef USART_STAT
#define USART_STAT USART_STAT0
#endif

/* ========== DMA ASYNC API Support ========== */
#ifdef CONFIG_UART_ASYNC_API
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/dma/dma_gd32.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
/* USART Register Offset Definitions */
#define USART_DATA_REG(reg) ((reg) + 0x04)
#define USART_CTL2_REG(reg) ((reg) + 0x14)

/* DMA Initialization Macros */
#define USART_DMA_INITIALIZER(idx, dir)                                       \
	/* 参数idx/dir仅用于DT宏，不会产生副作用 */                      \
	{                                                                      \
		.dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(idx, dir)),         \
		.channel = DT_INST_DMAS_CELL_BY_NAME(idx, dir, channel),           \
		.slot = COND_CODE_1(                                               \
			DT_HAS_COMPAT_STATUS_OKAY(gd_gd32_dma_v1),                     \
			(DT_INST_DMAS_CELL_BY_NAME(idx, dir, slot)), (0)),             \
		.config = DT_INST_DMAS_CELL_BY_NAME(idx, dir, config),             \
		.fifo_threshold = COND_CODE_1(                                     \
			DT_HAS_COMPAT_STATUS_OKAY(gd_gd32_dma_v1),                     \
			(DT_INST_DMAS_CELL_BY_NAME(idx, dir, fifo_threshold)),         \
			(0)),                                                           \
	}

#define USART_DMAS_DECL(idx)                                                   \
	/* 参数idx仅用于DT宏，不会产生副作用 */                        \
	{                                                                      \
		COND_CODE_1(DT_INST_DMAS_HAS_NAME(idx, tx),                       \
			(USART_DMA_INITIALIZER(idx, tx)), ({0})),                  \
		COND_CODE_1(DT_INST_DMAS_HAS_NAME(idx, rx),                       \
			(USART_DMA_INITIALIZER(idx, rx)), ({0})),                  \
	}

#endif /* CONFIG_UART_ASYNC_API */


/* ========== Forward Declarations ========== */
#ifdef CONFIG_UART_ASYNC_API
static int usart_gd32_async_rx_disable(const struct device *dev);
static int usart_gd32_async_rx_enable(
	const struct device *dev,
	uint8_t *buf,
	size_t len,
	int32_t timeout
);
static int usart_gd32_async_rx_buf_rsp(
	const struct device *dev,
	uint8_t *buf,
	size_t len
);
static void usart_gd32_async_dma_tx_callback(const struct device *dma_dev,
					    void *user_data,
					    uint32_t channel,
					    int status);
static void usart_gd32_async_dma_rx_callback(
	const struct device *dma_dev,
	void *user_data,
	uint32_t channel,
	int status
);
static int usart_gd32_async_tx(const struct device *dev,
			      const uint8_t *buf,
			      size_t len,
			      int32_t timeout);
static void usart_gd32_dma_rx_flush(const struct device *dev);
static void usart_gd32_async_rx_timeout_work(struct k_work *work);
static void usart_gd32_async_tx_timeout_work(struct k_work *work);
static int usart_gd32_async_tx_abort(const struct device *dev);

/* ========== DMA Async API Implementation ========== */
/* Chain-style DMA block iterator, positioned before structure definition */
static const struct dma_block_config *gd32_async_tx_next_block(
	struct gd32_usart_data *data
)
{
	const struct dma_block_config *cur = data->async_tx_blk;

	if (cur) {
	data->async_tx_blk = cur->next_block;
	}

	return cur;
}

static int usart_gd32_async_callback_set(
	const struct device *dev,
	uart_callback_t cb,
	void *user_data
)
{
	struct gd32_usart_data *data = dev->data;

	data->async_cb = cb;
	data->async_cb_data = user_data;

	return 0;
}

/**
 * @brief TX timeout handler - based on STM32 implementation
 *
 * This function is called when TX operation times out.
 * It aborts the current TX operation.
 */
static void usart_gd32_async_tx_timeout_work(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct gd32_usart_data *data = CONTAINER_OF(dwork, struct gd32_usart_data,
							async_tx_timeout_work);
	const struct device *dev = data->dev;

	LOG_DBG("TX timeout, aborting transmission");
	usart_gd32_async_tx_abort(dev);
}

static void usart_gd32_async_dma_tx_callback(const struct device *dma_dev,
					void *user_data,
					uint32_t channel,
					int status)
{
	ARG_UNUSED(dma_dev);
	ARG_UNUSED(channel);
	ARG_UNUSED(status);

	struct device *dev = (struct device *)user_data;
	struct gd32_usart_data *data;
	const struct gd32_usart_config *cfg;

	if (!dev) {
		return;
	}

	data = dev->data;
	cfg = dev->config;

	/* Cancel TX timeout timer */
	k_work_cancel_delayable(&data->async_tx_timeout_work);

	dma_stop(data->dma[USART_DMA_TX].dev, data->dma[USART_DMA_TX].channel);
	usart_dma_transmit_config(cfg->reg, DISABLE);
	/* Chain transmission continue */
	if (data->async_tx_blk) {
		const struct dma_block_config *next = gd32_async_tx_next_block(data);

		if (next) {
			struct gd32_usart_dma *dma = &data->dma[USART_DMA_TX];
			struct dma_config *dma_cfg = &dma->dma_cfg;
			struct dma_block_config *blk_cfg = &dma->dma_blk_cfg;
			memcpy(blk_cfg, next, sizeof(struct dma_block_config));
			if (blk_cfg->block_size &&
				blk_cfg->source_address &&
				blk_cfg->dest_address) {
				dma_cfg->head_block = blk_cfg;
				dma_cfg->channel_direction = MEMORY_TO_PERIPHERAL;
				dma_cfg->dma_callback = usart_gd32_async_dma_tx_callback;
				dma_cfg->user_data = (void *)dev;
				dma_stop(dma->dev, dma->channel);
				usart_dma_transmit_config(cfg->reg, USART_CTL2_DENT);
				if (dma_config(dma->dev, dma->channel, dma_cfg) == 0 &&
				    dma_start(dma->dev, dma->channel) == 0) {
					/* start TX timeout timer */
					if (data->async_tx_timeout != SYS_FOREVER_US &&
					    data->async_tx_timeout > 0) {
						k_work_reschedule(&data->async_tx_timeout_work,
								K_USEC(data->async_tx_timeout));
					}
					return; /* Continue chain */
				}
			}
		}
		data->async_tx_blk = NULL; /* Chain transmission failed */
	}

	const uint8_t *done_buf = data->async_tx_buf;
	size_t done_len = data->async_tx_len;

	data->async_tx_buf = NULL;
	data->async_tx_len = 0;
	if (data->async_cb && done_buf) {
		struct uart_event evt = {
			.type = UART_TX_DONE,
			.data.tx.buf = done_buf,
			.data.tx.len = done_len,
		};
		data->async_cb(dev, &evt, data->async_cb_data);
	}
}

static int usart_gd32_async_tx(const struct device *dev,
				const uint8_t *buf,
				size_t len,
				int32_t timeout)
{
	struct gd32_usart_data *data = dev->data;
	const struct gd32_usart_config *cfg = dev->config;
	struct gd32_usart_dma *dma = &data->dma[USART_DMA_TX];
	struct dma_config *dma_cfg = &dma->dma_cfg;
	struct dma_block_config *blk_cfg = &dma->dma_blk_cfg;
	int ret;


	if (!buf || len == 0) {
		return -EINVAL;
	}

	if (data->async_tx_buf) {
		return -EBUSY;
	}
	data->async_tx_buf = buf;
	data->async_tx_len = len;
	data->async_tx_blk = NULL;
	data->async_tx_timeout = timeout;

	/* Check if this is chain block */
	if (len == sizeof(struct dma_block_config) && buf != NULL) {
		/* Upper layer passes dma_block_config structure */
		data->async_tx_blk = (const struct dma_block_config *)buf;
	}

	LOG_DBG("TX: buf=%p, len=%zu, timeout=%d", buf, len, timeout);

	if (data->async_tx_blk) {
		/* Chain mode: send first block */
		const struct dma_block_config *cur = gd32_async_tx_next_block(data);
		if (!cur) {
			return -EINVAL;
		}
		memcpy(blk_cfg, cur, sizeof(struct dma_block_config));
		dma_cfg->head_block = blk_cfg;
		dma_cfg->block_count = 1U;
		dma_cfg->dma_slot = dma->slot;
		dma_cfg->channel_direction = MEMORY_TO_PERIPHERAL;
		dma_cfg->source_data_size = 1;
		dma_cfg->dest_data_size = 1;
		dma_cfg->dma_callback = usart_gd32_async_dma_tx_callback;
		dma_cfg->user_data = (void *)dev;
		dma_stop(dma->dev, dma->channel);
		usart_dma_transmit_config(cfg->reg, USART_CTL2_DENT);
		ret = dma_config(dma->dev, dma->channel, dma_cfg);
		if (ret != 0) {
			usart_dma_transmit_config(cfg->reg, DISABLE);
			data->async_tx_buf = NULL;
			return ret;
		}
		ret = dma_start(dma->dev, dma->channel);
		if (ret != 0) {
			usart_dma_transmit_config(cfg->reg, DISABLE);
			data->async_tx_buf = NULL;
			return ret;
		}
		/* 启动TX超时计时器 */
		if (timeout != SYS_FOREVER_US && timeout > 0) {
			k_work_reschedule(&data->async_tx_timeout_work, K_USEC(timeout));
		}
		return 0;
	} else {
		/* Normal block */
		memset(dma_cfg, 0, sizeof(struct dma_config));
		memset(blk_cfg, 0, sizeof(struct dma_block_config));
		blk_cfg->block_size = len;
		blk_cfg->source_address = (uint32_t)buf;
		blk_cfg->dest_address = (uint32_t)USART_DATA_REG(cfg->reg);
		blk_cfg->source_addr_adj = DMA_ADDR_ADJ_INCREMENT;
		blk_cfg->dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
		dma_cfg->head_block = blk_cfg;
		dma_cfg->block_count = 1U;
		dma_cfg->dma_slot = dma->slot;
		dma_cfg->channel_direction = MEMORY_TO_PERIPHERAL;
		dma_cfg->source_data_size = 1;
		dma_cfg->dest_data_size = 1;
		dma_cfg->dma_callback = usart_gd32_async_dma_tx_callback;
		dma_cfg->user_data = (void *)dev;
		dma_stop(dma->dev, dma->channel);
		usart_dma_transmit_config(cfg->reg, USART_CTL2_DENT);
		ret = dma_config(dma->dev, dma->channel, dma_cfg);
		if (ret != 0) {
			usart_dma_transmit_config(cfg->reg, DISABLE);
			data->async_tx_buf = NULL;
			return ret;
		}
		ret = dma_start(dma->dev, dma->channel);
		if (ret != 0) {
			usart_dma_transmit_config(cfg->reg, DISABLE);
			data->async_tx_buf = NULL;
			return ret;
		}
		/* 启动TX超时计时器 */
		if (timeout != SYS_FOREVER_US && timeout > 0) {
			k_work_reschedule(&data->async_tx_timeout_work, K_USEC(timeout));
		}
		return 0;
	}
}

static int usart_gd32_async_tx_abort(const struct device *dev)
{
	struct gd32_usart_data *data = dev->data;
	const struct gd32_usart_config *cfg = dev->config;
	struct gd32_usart_dma *dma = &data->dma[USART_DMA_TX];

	LOG_DBG("TX abort requested");

	/* Cancel TX timeout timer */
	k_work_cancel_delayable(&data->async_tx_timeout_work);

	dma_stop(dma->dev, dma->channel);
	usart_dma_transmit_config(cfg->reg, DISABLE);
	if (data->async_tx_buf && data->async_cb) {
		struct uart_event evt = {
			.type = UART_TX_ABORTED,
			.data.tx.buf = data->async_tx_buf,
			.data.tx.len = data->async_tx_len,
		};
		data->async_cb(dev, &evt, data->async_cb_data);
	}
	data->async_tx_buf = NULL;
	data->async_tx_len = 0;
	return 0;
}

/**
 * @brief STM32-style timeout handler with automatic timeout reset
 *
 * This handler is based on IDLE interrupt delay report and provides
 * automatic timeout management for RX operations.
 */
static void usart_gd32_async_rx_timeout_work(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct gd32_usart_data *data = CONTAINER_OF(dwork, struct gd32_usart_data,
							   async_rx_timeout_work);
	const struct device *dev = data->dev;

	if (!data->async_rx_enabled || !data->async_rx_buf) {
		return;
	}

	/* Check if there is new data arriving */
	struct gd32_usart_dma *dma = &data->dma[USART_DMA_RX];
	struct dma_status stat;
	bool has_new_data = false;

	if (dma_get_status(dma->dev, dma->channel, &stat) == 0) {
		size_t current_rx_len = data->async_rx_len - stat.pending_length;
		if (current_rx_len > data->async_rx_counter) {
			/* New data arrived, reset timeout and continue waiting */
			data->async_rx_counter = current_rx_len;
			has_new_data = true;
			k_work_reschedule(&data->async_rx_timeout_work,
							  K_USEC(data->async_rx_timeout));
			return;
		}
	}

	/* Timeout period without new data, flush already received data */
	usart_gd32_dma_rx_flush(dev);

	/* Note: No need to restart DMA like the old code did.
	 * After flush, DMA continues running and will automatically receive
	 * more data into the remaining buffer space (STM32 behavior). */
}

/**
 * @brief STM32-style DMA RX flush function (aligned with STM32 behavior)
 *
 * Flushes any pending RX data from DMA buffer without stopping DMA.
 * This maintains continuous reception capability like STM32 driver.
 */
static void usart_gd32_dma_rx_flush(const struct device *dev)
{
	struct gd32_usart_data *data = dev->data;

	if (!data->async_rx_enabled || !data->async_rx_buf) {
		return;
	}

	struct gd32_usart_dma *dma = &data->dma[USART_DMA_RX];
	struct dma_status stat;

	/* Get current DMA status without stopping DMA (STM32 style) */
	if (dma_get_status(dma->dev, dma->channel, &stat) == 0) {
		size_t rx_rcv_len = data->async_rx_len - stat.pending_length;
		data->async_rx_counter = rx_rcv_len;


		if (rx_rcv_len > data->async_rx_offset) {
			size_t new_bytes = rx_rcv_len - data->async_rx_offset;

			if (data->async_cb && new_bytes > 0) {
				struct uart_event evt = {
					.type = UART_RX_RDY,
					.data.rx.buf = data->async_rx_buf,
					.data.rx.len = new_bytes,
					.data.rx.offset = data->async_rx_offset,
				};
				data->async_cb(dev, &evt, data->async_cb_data);
			}
			data->async_rx_offset = rx_rcv_len;
		}
	}

	/* Note: Unlike the old version, we don't stop DMA or disable RX here.
	 * DMA continues running for continuous reception (STM32 behavior).
	 * The application must call uart_rx_disable() if it wants to stop. */
}

static void usart_gd32_async_dma_rx_callback(
	const struct device *dma_dev,
	void *user_data,
	uint32_t channel,
	int status
)
{
	struct device *dev = (struct device *)user_data;
	struct gd32_usart_data *data = dev->data;
	const struct gd32_usart_config *cfg = dev->config;

	if (!data->async_rx_enabled || !data->async_rx_buf) {
		return;
	}

	/* Stop current DMA operation properly */
	usart_dma_receive_config(cfg->reg, DISABLE);
	dma_stop(data->dma[USART_DMA_RX].dev, data->dma[USART_DMA_RX].channel);
	k_work_cancel_delayable(&data->async_rx_timeout_work);

	/* Get actual received data length */
	struct gd32_usart_dma *dma = &data->dma[USART_DMA_RX];
	struct dma_status stat;
	size_t current_rx_len = 0;

	if (dma_get_status(dma->dev, dma->channel, &stat) == 0) {
		current_rx_len = data->async_rx_len - stat.pending_length;
	}

	/* Report currently received data */
	if (data->async_cb && current_rx_len > data->async_rx_offset) {
		size_t new_bytes = current_rx_len - data->async_rx_offset;
		struct uart_event evt = {
			.type = UART_RX_RDY,
			.data.rx.buf = data->async_rx_buf,
			.data.rx.len = new_bytes,
			.data.rx.offset = data->async_rx_offset,
		};
		data->async_cb(dev, &evt, data->async_cb_data);
		data->async_rx_offset = current_rx_len;
	}

	/* Send RX_BUF_REQUEST event to request new buffer */
	if (data->async_cb) {
		struct uart_event evt = {
			.type = UART_RX_BUF_REQUEST,
		};

		data->async_cb(dev, &evt, data->async_cb_data);
	}

	/* Note: RX_DISABLED will be sent here.
	 * Wait for app to provide new buffer via uart_rx_buf_rsp.
	 * If app doesn't provide new buffer,
	 * IDLE interrupt or timeout will handle remaining data and send RX_DISABLED
	 */
}

static int usart_gd32_async_rx_enable(
	const struct device *dev,
	uint8_t *buf,
	size_t len,
	int32_t timeout
)
{
	struct gd32_usart_data *data = dev->data;
	const struct gd32_usart_config *cfg =
		(const struct gd32_usart_config *)dev->config;
	int ret;
	if (!buf || len == 0) {
		return -EINVAL;
	}
	if (data->async_rx_enabled) {
		return -EBUSY;
	}
	data->async_rx_buf = buf;
	data->async_rx_len = len;
	data->async_rx_offset = 0;
	data->async_rx_counter = 0;
	data->async_rx_enabled = true;
	data->async_rx_timeout = timeout;

	/* Clear receive buffer to indicate fresh state */
	memset(buf, 0, len);
	/* DMA setup */
	struct gd32_usart_dma *dma = &data->dma[USART_DMA_RX];
	struct dma_config *dma_cfg = &dma->dma_cfg;
	struct dma_block_config *blk_cfg = &dma->dma_blk_cfg;

	memset(dma_cfg, 0, sizeof(struct dma_config));
	memset(blk_cfg, 0, sizeof(struct dma_block_config));
	blk_cfg->block_size = len;
	blk_cfg->source_address = (uint32_t)USART_DATA_REG(cfg->reg);
	blk_cfg->dest_address = (uint32_t)buf;
	blk_cfg->source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
	blk_cfg->dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;
	dma_cfg->head_block = blk_cfg;
	dma_cfg->block_count = 1U;
	dma_cfg->dma_slot = dma->slot;
	dma_cfg->channel_direction = PERIPHERAL_TO_MEMORY;
	dma_cfg->source_data_size = 1;
	dma_cfg->dest_data_size = 1;
	dma_cfg->dma_callback = usart_gd32_async_dma_rx_callback;
	dma_cfg->user_data = (void *)dev;

	/* Clear IDLE flag before enabling DMA to prevent spurious interrupts */
	usart_interrupt_flag_clear(cfg->reg, USART_FLAG_IDLE);
	/* 1. Configure DMA */
	ret = dma_config(dma->dev, dma->channel, dma_cfg);
	if (ret != 0) {
		usart_dma_receive_config(cfg->reg, DISABLE);
		data->async_rx_enabled = false;
		data->async_rx_buf = NULL;
		return ret;
	}
	/* 2. Enable USART DMA */
	usart_dma_receive_config(cfg->reg, USART_CTL2_DENR);
	/* Read CTL2 to confirm DENR bit is set */
	uint32_t ctl2_after_enable = REG32(USART_CTL2_REG(cfg->reg));
	if ((ctl2_after_enable & USART_CTL2_DENR) == 0) {
		/* Note: DENR bit check for debug purposes */
	}
	/* 3. Start DMA */
	ret = dma_start(dma->dev, dma->channel);
	if (ret != 0) {
		usart_dma_receive_config(cfg->reg, DISABLE);
		data->async_rx_enabled = false;
		data->async_rx_buf = NULL;
		return ret;
	}
	/* 3.5 Forcefully write DENR again to prevent hardware conflicts */
	usart_dma_receive_config(cfg->reg, USART_CTL2_DENR);

	/* Debug: Check DMA status and CTL2 */
	struct dma_status start_stat;
	if (dma_get_status(dma->dev, dma->channel, &start_stat) == 0) {
		/* DMA status check for debug */
	}

	/* 4. Enable IDLE and RBNE interrupts for data flow and completion flags */
	usart_interrupt_flag_clear(cfg->reg, USART_FLAG_IDLE);
	usart_interrupt_flag_clear(cfg->reg, USART_FLAG_RBNE);
	usart_interrupt_enable(cfg->reg, USART_INT_IDLE);
	usart_interrupt_enable(cfg->reg, USART_INT_RBNE);
	/* Data buffering, always enable interrupt */

	return 0;
}

static int usart_gd32_async_rx_disable(const struct device *dev)
{
	struct gd32_usart_data *data = dev->data;
	const struct gd32_usart_config *cfg =
		(const struct gd32_usart_config *)dev->config;
	struct gd32_usart_dma *dma = &data->dma[USART_DMA_RX];

	if (!data->async_rx_enabled) {
		if (data->async_cb) {
			struct uart_event evt = {
				.type = UART_RX_DISABLED,
			};
			data->async_cb(dev, &evt, data->async_cb_data);
		}
		return -EFAULT;
	}

	/* Disable IDLE interrupt first */
	usart_interrupt_disable(cfg->reg, USART_INT_IDLE);

	/* Flush any remaining RX data */
	usart_gd32_dma_rx_flush(dev);

	/* Disable USART DMA */
	usart_dma_receive_config(cfg->reg, DISABLE);

	/* Cancel timeout timer */
	k_work_cancel_delayable(&data->async_rx_timeout_work);

	/* Now stop DMA channel - this is the key position change */
	dma_stop(dma->dev, dma->channel);

	/* Disable RBNE interrupt and clear flags */
	usart_interrupt_disable(cfg->reg, USART_INT_RBNE);
	usart_interrupt_flag_clear(cfg->reg, USART_FLAG_IDLE);
	usart_interrupt_flag_clear(cfg->reg, USART_FLAG_RBNE);
	if (data->async_rx_enabled && data->async_cb) {
		struct uart_event evt = {
			.type = UART_RX_DISABLED,
		};
		data->async_cb(dev, &evt, data->async_cb_data);
	}
	data->async_rx_enabled = false;
	data->async_rx_buf = NULL;
	data->async_rx_len = 0;
	data->async_rx_offset = 0;
	data->async_rx_counter = 0;
	/* Reset same buffer scenario */
	return 0;
}

static int usart_gd32_async_rx_buf_rsp(
	const struct device *dev,
	uint8_t *buf,
	size_t len
)
{
	struct gd32_usart_data *data = dev->data;
	const struct gd32_usart_config *cfg = dev->config;

	if (!buf || len == 0) {
		return -EINVAL;
	}

	/* Configure new buffer and restart DMA operation */
	struct gd32_usart_dma *dma = &data->dma[USART_DMA_RX];
	struct dma_config *dma_cfg = &dma->dma_cfg;
	struct dma_block_config *blk_cfg = &dma->dma_blk_cfg;

	/* Ensure DMA is stopped before reconfiguration */
	dma_stop(dma->dev, dma->channel);
	usart_dma_receive_config(cfg->reg, DISABLE);

	/* Setup new DMA configuration */
	memset(blk_cfg, 0, sizeof(struct dma_block_config));
	blk_cfg->block_size = len;
	blk_cfg->source_address = (uint32_t)USART_DATA_REG(cfg->reg);
	blk_cfg->dest_address = (uint32_t)buf;
	blk_cfg->source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
	blk_cfg->dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;
	dma_cfg->head_block = blk_cfg;
	dma_cfg->block_count = 1U;

	/* Update state */
	data->async_rx_buf = buf;
	data->async_rx_len = len;
	data->async_rx_offset = 0;
	data->async_rx_counter = 0;
	data->async_rx_enabled = true;

	/* Clear new buffer to indicate fresh state */
	memset(buf, 0, len);

	/* Configure DMA */
	int ret = dma_config(dma->dev, dma->channel, dma_cfg);
	if (ret != 0) {
		data->async_rx_enabled = false;
		data->async_rx_buf = NULL;
		return ret;
	}

	/* Enable USART DMA reception */
	usart_dma_receive_config(cfg->reg, USART_CTL2_DENR);

	/* Start DMA */
	ret = dma_start(dma->dev, dma->channel);
	if (ret != 0) {
		usart_dma_receive_config(cfg->reg, DISABLE);
		data->async_rx_enabled = false;
		data->async_rx_buf = NULL;
		return ret;
	}

	LOG_DBG("RX buf response: new buffer configured, len=%zu", len);

	return 0;
}
#endif

#if defined(CONFIG_UART_INTERRUPT_DRIVEN) || \
	defined(CONFIG_UART_ASYNC_API)
static void usart_gd32_isr(const struct device *dev)
{
	struct gd32_usart_data *const data = dev->data;
#ifdef CONFIG_UART_ASYNC_API
	const struct gd32_usart_config *cfg = dev->config;
	uint32_t stat_reg;
	bool idle_flag, rbne_flag, tc_flag;

	/* Read interrupt status */
	stat_reg = REG32(cfg->reg);  /* USART_STAT0 */

	/* Check all possible interrupt sources */
	idle_flag = usart_interrupt_flag_get(cfg->reg, USART_INT_FLAG_IDLE);
	rbne_flag = usart_interrupt_flag_get(cfg->reg, USART_INT_FLAG_RBNE);
	tc_flag = usart_interrupt_flag_get(cfg->reg, USART_INT_FLAG_TC);

	if (idle_flag) {
		/* GD32 IDLE flag clearing requires reading status register
		 * first then data register
		 */
		uint32_t status = REG32(cfg->reg); /* read USART_STAT0 */
		uint32_t data_reg = REG32(cfg->reg + 0x04); /* read USART_DATA */
		(void)status;
		(void)data_reg; /* avoid compiler warnings */

		/* Double-check if flag is still set */
		if (usart_interrupt_flag_get(cfg->reg,
				     USART_INT_FLAG_IDLE)) {
			/* force clear if still set */
			usart_interrupt_flag_clear(cfg->reg, USART_FLAG_IDLE);
		}

		/* IDLE interrupt: data transmission completed,
		 * check current DMA status
		 */
		if (data->async_rx_enabled && data->async_rx_buf) {
			struct gd32_usart_dma *dma = &data->dma[USART_DMA_RX];
			struct dma_status stat;

			if (dma_get_status(dma->dev, dma->channel, &stat) == 0) {
				size_t current_rx_len = data->async_rx_len -
							stat.pending_length;

				if (current_rx_len > data->async_rx_offset) {
					/* new data needs to be reported */
					if (data->async_rx_timeout == 0) {
						/* no timeout: flush immediately */
						usart_gd32_dma_rx_flush(dev);
					} else {
						/* start timer for delayed processing */
						k_work_reschedule(&data->async_rx_timeout_work,
								K_USEC(data->async_rx_timeout));
					}
				}
			}
		}
		return;
	}

	/* If IDLE interrupt not handled, process other normal interrupts
	 * and check DMA status
	 */
	if (rbne_flag || tc_flag) {
		/* check if async RX is in progress */
		if (data->async_rx_enabled && data->async_rx_buf) {
			struct gd32_usart_dma *dma = &data->dma[USART_DMA_RX];
			struct dma_status stat;

			if (dma_get_status(dma->dev, dma->channel, &stat) == 0) {
				size_t rx_rcv_len = data->async_rx_len -
							stat.pending_length;

				/* If DMA received new data, start/restart
				 * timeout timer
				 */
				if (rx_rcv_len > data->async_rx_offset) {
					/* update latest received count */
					data->async_rx_counter = rx_rcv_len;

					/* start timer or process immediately */
					if (data->async_rx_timeout > 0) {
						k_work_reschedule(&data->async_rx_timeout_work,
								K_USEC(data->async_rx_timeout));
					} else {
						/* no timeout: process immediately */
						usart_gd32_dma_rx_flush(dev);
					}
					return;
				}
			}
		}
	}
#endif /* CONFIG_UART_ASYNC_API */

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	if (data->user_cb) {
		data->user_cb(dev, data->user_data);
	}
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */
}
#endif /* defined(CONFIG_UART_INTERRUPT_DRIVEN) || \
		  defined(CONFIG_UART_ASYNC_API) */

static int usart_gd32_init(const struct device *dev)
{
	const struct gd32_usart_config *const cfg = dev->config;
	struct gd32_usart_data *const data = dev->data;
	uint32_t word_length;
	uint32_t parity;
	int ret;

#ifdef CONFIG_UART_ASYNC_API
	data->dev = dev;
#endif

	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		return ret;
	}

	/*
	 * In order to keep the transfer data size to 8 bits(1 byte),
	 * append word length to 9BIT if parity bit enabled.
	 */
	switch (cfg->parity) {
	case UART_CFG_PARITY_NONE:
		parity = USART_PM_NONE;
		word_length = USART_WL_8BIT;
		break;
	case UART_CFG_PARITY_ODD:
		parity = USART_PM_ODD;
		word_length = USART_WL_9BIT;
		break;
	case UART_CFG_PARITY_EVEN:
		parity = USART_PM_EVEN;
		word_length = USART_WL_9BIT;
		break;
	default:
		return -ENOTSUP;
	}

	(void)clock_control_on(GD32_CLOCK_CONTROLLER,
			       (clock_control_subsys_t)&cfg->clkid);

	(void)reset_line_toggle_dt(&cfg->reset);

	usart_baudrate_set(cfg->reg, data->baud_rate);
	usart_parity_config(cfg->reg, parity);
	usart_word_length_set(cfg->reg, word_length);
	/* Default to 1 stop bit */
	usart_stop_bit_set(cfg->reg, USART_STB_1BIT);
	usart_receive_config(cfg->reg, USART_RECEIVE_ENABLE);
	usart_transmit_config(cfg->reg, USART_TRANSMIT_ENABLE);
	usart_enable(cfg->reg);

#if defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_ASYNC_API)
	cfg->irq_config_func(dev);
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

#ifdef CONFIG_UART_ASYNC_API
	/* Initialize timeout work queues for async operations */
	k_work_init_delayable(&data->async_rx_timeout_work,
			      usart_gd32_async_rx_timeout_work);
	k_work_init_delayable(&data->async_tx_timeout_work,
			      usart_gd32_async_tx_timeout_work);
#endif

	return 0;
}

static int usart_gd32_poll_in(const struct device *dev, unsigned char *c)
{
	const struct gd32_usart_config *const cfg = dev->config;
	uint32_t status;

	status = usart_flag_get(cfg->reg, USART_FLAG_RBNE);

	if (!status) {
		return -EPERM;
	}

	*c = usart_data_receive(cfg->reg);

	return 0;
}

static void usart_gd32_poll_out(const struct device *dev, unsigned char c)
{
	const struct gd32_usart_config *const cfg = dev->config;

	usart_data_transmit(cfg->reg, c);

	while (usart_flag_get(cfg->reg, USART_FLAG_TBE) == RESET) {
		;
	}
}

static int usart_gd32_err_check(const struct device *dev)
{
	const struct gd32_usart_config *const cfg = dev->config;
	uint32_t status = USART_STAT(cfg->reg);
	int errors = 0;

	if (status & USART_FLAG_ORERR) {
		usart_flag_clear(cfg->reg, USART_FLAG_ORERR);

		errors |= UART_ERROR_OVERRUN;
	}

	if (status & USART_FLAG_PERR) {
		usart_flag_clear(cfg->reg, USART_FLAG_PERR);

		errors |= UART_ERROR_PARITY;
	}

	if (status & USART_FLAG_FERR) {
		usart_flag_clear(cfg->reg, USART_FLAG_FERR);

		errors |= UART_ERROR_FRAMING;
	}

	usart_flag_clear(cfg->reg, USART_FLAG_NERR);

	return errors;
}

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
int usart_gd32_fifo_fill(const struct device *dev, const uint8_t *tx_data,
			 int len)
{
	const struct gd32_usart_config *const cfg = dev->config;
	int num_tx = 0U;

	while ((len - num_tx > 0) &&
	       usart_flag_get(cfg->reg, USART_FLAG_TBE)) {
		usart_data_transmit(cfg->reg, tx_data[num_tx++]);
	}

	return num_tx;
}

int usart_gd32_fifo_read(const struct device *dev, uint8_t *rx_data,
			 const int size)
{
	const struct gd32_usart_config *const cfg = dev->config;
	int num_rx = 0U;

	while ((size - num_rx > 0) &&
	       usart_flag_get(cfg->reg, USART_FLAG_RBNE)) {
		rx_data[num_rx++] = usart_data_receive(cfg->reg);
	}

	return num_rx;
}

void usart_gd32_irq_tx_enable(const struct device *dev)
{
	const struct gd32_usart_config *const cfg = dev->config;

	usart_interrupt_enable(cfg->reg, USART_INT_TC);
}

void usart_gd32_irq_tx_disable(const struct device *dev)
{
	const struct gd32_usart_config *const cfg = dev->config;

	usart_interrupt_disable(cfg->reg, USART_INT_TC);
}

int usart_gd32_irq_tx_ready(const struct device *dev)
{
	const struct gd32_usart_config *const cfg = dev->config;

	return usart_flag_get(cfg->reg, USART_FLAG_TBE) &&
	       usart_interrupt_flag_get(cfg->reg, USART_INT_FLAG_TC);
}

int usart_gd32_irq_tx_complete(const struct device *dev)
{
	const struct gd32_usart_config *const cfg = dev->config;

	return usart_flag_get(cfg->reg, USART_FLAG_TC);
}

void usart_gd32_irq_rx_enable(const struct device *dev)
{
	const struct gd32_usart_config *const cfg = dev->config;

	usart_interrupt_enable(cfg->reg, USART_INT_RBNE);
}

void usart_gd32_irq_rx_disable(const struct device *dev)
{
	const struct gd32_usart_config *const cfg = dev->config;

	usart_interrupt_disable(cfg->reg, USART_INT_RBNE);
}

int usart_gd32_irq_rx_ready(const struct device *dev)
{
	const struct gd32_usart_config *const cfg = dev->config;

	return usart_flag_get(cfg->reg, USART_FLAG_RBNE);
}

void usart_gd32_irq_err_enable(const struct device *dev)
{
	const struct gd32_usart_config *const cfg = dev->config;

	usart_interrupt_enable(cfg->reg, USART_INT_ERR);
	usart_interrupt_enable(cfg->reg, USART_INT_PERR);
}

void usart_gd32_irq_err_disable(const struct device *dev)
{
	const struct gd32_usart_config *const cfg = dev->config;

	usart_interrupt_disable(cfg->reg, USART_INT_ERR);
	usart_interrupt_disable(cfg->reg, USART_INT_PERR);
}

int usart_gd32_irq_is_pending(const struct device *dev)
{
	const struct gd32_usart_config *const cfg = dev->config;

	return ((usart_flag_get(cfg->reg, USART_FLAG_RBNE) &&
		 usart_interrupt_flag_get(cfg->reg, USART_INT_FLAG_RBNE)) ||
		(usart_flag_get(cfg->reg, USART_FLAG_TC) &&
		 usart_interrupt_flag_get(cfg->reg, USART_INT_FLAG_TC)));
}

int usart_gd32_irq_update(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 1;
}

void usart_gd32_irq_callback_set(const struct device *dev,
				 uart_irq_callback_user_data_t cb,
				 void *user_data)
{
	struct gd32_usart_data *const data = dev->data;

	data->user_cb = cb;
	data->user_data = user_data;
}
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

static DEVICE_API(uart, usart_gd32_driver_api) = {
	.poll_in = usart_gd32_poll_in,
	.poll_out = usart_gd32_poll_out,
	.err_check = usart_gd32_err_check,
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	.fifo_fill = usart_gd32_fifo_fill,
	.fifo_read = usart_gd32_fifo_read,
	.irq_tx_enable = usart_gd32_irq_tx_enable,
	.irq_tx_disable = usart_gd32_irq_tx_disable,
	.irq_tx_ready = usart_gd32_irq_tx_ready,
	.irq_tx_complete = usart_gd32_irq_tx_complete,
	.irq_rx_enable = usart_gd32_irq_rx_enable,
	.irq_rx_disable = usart_gd32_irq_rx_disable,
	.irq_rx_ready = usart_gd32_irq_rx_ready,
	.irq_err_enable = usart_gd32_irq_err_enable,
	.irq_err_disable = usart_gd32_irq_err_disable,
	.irq_is_pending = usart_gd32_irq_is_pending,
	.irq_update = usart_gd32_irq_update,
	.irq_callback_set = usart_gd32_irq_callback_set,
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */
#ifdef CONFIG_UART_ASYNC_API
	.callback_set = usart_gd32_async_callback_set,
	.tx = usart_gd32_async_tx,
	.tx_abort = usart_gd32_async_tx_abort,
	.rx_enable = usart_gd32_async_rx_enable,
	.rx_disable = usart_gd32_async_rx_disable,
	.rx_buf_rsp = usart_gd32_async_rx_buf_rsp,
#endif

};

#if defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_ASYNC_API)
#define GD32_USART_IRQ_HANDLER(n) \
	static void usart_gd32_config_func_##n(const struct device *dev) \
	{ \
		IRQ_CONNECT(DT_INST_IRQN(n), \
			DT_INST_IRQ(n, priority), \
			usart_gd32_isr, \
			DEVICE_DT_INST_GET(n), \
			0); \
		irq_enable(DT_INST_IRQN(n)); \
	}
#define GD32_USART_IRQ_HANDLER_FUNC_INIT(n)					\
	.irq_config_func = usart_gd32_config_func_##n
#else /* CONFIG_UART_INTERRUPT_DRIVEN */
#define GD32_USART_IRQ_HANDLER(n)
#define GD32_USART_IRQ_HANDLER_FUNC_INIT(n)
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

#define GD32_USART_INIT(n) \
	PINCTRL_DT_INST_DEFINE(n); \
	GD32_USART_IRQ_HANDLER(n) \
	static struct gd32_usart_data usart_gd32_data_##n = { \
		.baud_rate = DT_INST_PROP(n, current_speed), \
		IF_ENABLED(CONFIG_UART_ASYNC_API, (.dma = USART_DMAS_DECL(n),)) \
	}; \
	static const struct gd32_usart_config usart_gd32_config_##n = { \
		.reg = DT_INST_REG_ADDR(n), \
		.clkid = DT_INST_CLOCKS_CELL(n, id), \
		.reset = RESET_DT_SPEC_INST_GET(n), \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n), \
		.parity = DT_INST_ENUM_IDX(n, parity), \
		GD32_USART_IRQ_HANDLER_FUNC_INIT(n) \
	}; \
	DEVICE_DT_INST_DEFINE(n, usart_gd32_init, \
		       NULL, \
		       &usart_gd32_data_##n, \
		       &usart_gd32_config_##n, PRE_KERNEL_1, \
		       CONFIG_SERIAL_INIT_PRIORITY, \
		       &usart_gd32_driver_api);

DT_INST_FOREACH_STATUS_OKAY(GD32_USART_INIT)
