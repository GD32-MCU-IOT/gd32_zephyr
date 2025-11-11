/*
 * Copyright (c) 2021, ATL Electronics
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_SERIAL_USART_GD32_H_
#define ZEPHYR_DRIVERS_SERIAL_USART_GD32_H_

#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/pinctrl.h>

#ifdef CONFIG_UART_ASYNC_API
/**
 * @brief DMA direction enumeration for USART operations
 */
enum usart_gd32_dma_direction {
	USART_DMA_TX = 0,	/**< Transmit DMA channel */
	USART_DMA_RX,		/**< Receive DMA channel */
	USART_DMA_NUM		/**< Total number of DMA channels */
};

/**
 * @brief DMA configuration structure for GD32 USART
 *
 * This structure encapsulates all DMA-related configuration
 * and runtime state for a single DMA channel.
 */
struct gd32_usart_dma {
	/** DMA controller device */
	const struct device *dev;
	/** DMA channel number */
	uint32_t channel;
	/** DMA request slot/trigger source */
	uint32_t slot;
	/** DMA configuration flags */
	uint32_t config;
	/** FIFO threshold level */
	uint32_t fifo_threshold;
	/** Runtime DMA configuration */
	struct dma_config dma_cfg;
	/** DMA block configuration for current transfer */
	struct dma_block_config dma_blk_cfg;
};
#endif /* CONFIG_UART_ASYNC_API */

/**
 * @brief GD32 USART device configuration structure
 *
 * This structure contains compile-time configuration data
 * that is typically derived from device tree.
 */
struct gd32_usart_config {
	/** USART register base address */
	uint32_t reg;
	/** Clock ID for this USART instance */
	uint16_t clkid;
	/** Reset controller specification */
	struct reset_dt_spec reset;
	/** Pin control configuration */
	const struct pinctrl_dev_config *pcfg;
	/** Parity configuration */
	uint32_t parity;
#if defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_ASYNC_API)
	/** IRQ configuration function */
	uart_irq_config_func_t irq_config_func;
#endif
};

/**
 * @brief GD32 USART device runtime data structure
 *
 * This structure holds the runtime state and configuration for a GD32 USART device.
 * It's organized into functional groups for better maintainability.
 */
struct gd32_usart_data {
	/** Current baud rate configuration */
	uint32_t baud_rate;

#ifdef CONFIG_UART_ASYNC_API
	/* ========== DMA Async API Support ========== */
	/** DMA channel configurations for TX and RX operations */
	struct gd32_usart_dma dma[USART_DMA_NUM];
	/** User callback function for async operations */
	uart_callback_t async_cb;
	/** User data passed to async callback */
	void *async_cb_data;

	/* ========== TX State Management ========== */
	/** Current TX buffer pointer */
	const uint8_t *async_tx_buf;
	/** Total TX buffer length */
	size_t async_tx_len;
	/** Current DMA block configuration for chained TX */
	const struct dma_block_config *async_tx_blk;
	/** TX timeout value in microseconds */
	int32_t async_tx_timeout;
	/** Work queue item for TX timeout handling */
	struct k_work_delayable async_tx_timeout_work;

	/* ========== RX State Management ========== */
	/** Current RX buffer pointer */
	uint8_t *async_rx_buf;
	/** Total RX buffer length */
	size_t async_rx_len;
	/** Current offset in RX buffer (bytes already processed) */
	size_t async_rx_offset;
	/** Last received byte count (for timeout detection) */
	size_t async_rx_counter;
	/** RX operation enabled flag */
	bool async_rx_enabled;
	/** RX timeout value in microseconds */
	int32_t async_rx_timeout;
	/** Work queue item for RX timeout handling */
	struct k_work_delayable async_rx_timeout_work;
	/** Device pointer for work queue callbacks (TODO: can be removed with CONTAINER_OF) */
	const struct device *dev;
#endif

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	/* ========== Interrupt-Driven API Support ========== */
	/** User callback for interrupt-driven operations */
	uart_irq_callback_user_data_t user_cb;
	/** User data passed to interrupt callback */
	void *user_data;
#endif
};

#endif /* ZEPHYR_DRIVERS_SERIAL_USART_GD32_H_ */
