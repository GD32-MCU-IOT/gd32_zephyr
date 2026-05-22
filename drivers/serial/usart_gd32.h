/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
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

#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
	/** Cached UART configuration for runtime configure API */
	struct uart_config uart_cfg;
#endif

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
	/** Next RX buffer (cached by buf_rsp, consumed by DMA callback) */
	uint8_t *rx_next_buffer;
	/** Next RX buffer length */
	size_t rx_next_buffer_len;
	/** Device pointer for work queue callbacks */
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

/* ========== M53x USART->UART compatibility layer ========== */
/* M53x has UART (not USART). Map usart_xxx API to uart_xxx so the driver
 * works without modification. Follows the same pattern as E51x I2C_ADD
 * compatibility in i2c_gd32_common.h.
 */
#ifdef CONFIG_SOC_SERIES_GD32M53X

/* Register/data access */
#define USART_STAT(x)              UART_STAT0(x)
#define USART_STAT0(x)             UART_STAT0(x)
#define USART_DATA(x)              UART_DATA(x)

/* Flags */
#define USART_FLAG_RBNE            UART_FLAG_RBNE
#define USART_FLAG_TBE             UART_FLAG_TBE
#define USART_FLAG_TC              UART_FLAG_TC
#define USART_FLAG_ORERR           UART_FLAG_ORERR
#define USART_FLAG_NERR            UART_FLAG_NERR
#define USART_FLAG_FERR            UART_FLAG_FERR
#define USART_FLAG_PERR            UART_FLAG_PERR
#define USART_FLAG_IDLE            UART_FLAG_IDLE

/* Interrupts */
#define USART_INT_RBNE             UART_INT_RBNE
#define USART_INT_TBE              UART_INT_TBE
#define USART_INT_TC               UART_INT_TC
#define USART_INT_ERR              UART_INT_ERR
#define USART_INT_IDLE             UART_INT_IDLE
#define USART_INT_PERR             UART_INT_PERR
#define USART_INT_FLAG_RBNE        UART_INT_FLAG_RBNE
#define USART_INT_FLAG_TC          UART_INT_FLAG_TC
#define USART_INT_FLAG_IDLE        UART_INT_FLAG_IDLE

/* RX/TX enable */
#define USART_RECEIVE_ENABLE       UART_RECEIVE_ENABLE
#define USART_TRANSMIT_ENABLE      UART_TRANSMIT_ENABLE

/* DMA enable/disable */
#define USART_RECEIVE_DMA_ENABLE   UART_RECEIVE_DMA_ENABLE
#define USART_RECEIVE_DMA_DISABLE  UART_RECEIVE_DMA_DISABLE
#define USART_TRANSMIT_DMA_ENABLE  UART_TRANSMIT_DMA_ENABLE
#define USART_TRANSMIT_DMA_DISABLE UART_TRANSMIT_DMA_DISABLE

/* Stop bits */
#define USART_STB_1BIT             UART_STB_1BIT
#define USART_STB_2BIT             UART_STB_2BIT
#define USART_STB_0_5BIT           CTL1_STB(1)
#define USART_STB_1_5BIT           CTL1_STB(3)

/* Parity */
#define USART_PM_NONE              UART_PM_NONE
#define USART_PM_ODD               UART_PM_ODD
#define USART_PM_EVEN              UART_PM_EVEN

/* Word length */
#define USART_WL_8BIT              UART_WL_8BIT
#define USART_WL_9BIT              UART_WL_9BIT
#define USART_WL_7BIT              UART_WL_7BIT

/* Hardware flow control (M53x UART has no HW flow control) */
#define USART_RTS_ENABLE           0U
#define USART_RTS_DISABLE          1U
#define USART_CTS_ENABLE           0U
#define USART_CTS_DISABLE          1U

/* Function mappings */
#define usart_baudrate_set         uart_baudrate_set
#define usart_word_length_set      uart_word_length_set
#define usart_stop_bit_set         uart_stop_bit_set
#define usart_parity_config        uart_parity_config
#define usart_enable               uart_enable
#define usart_disable              uart_disable
#define usart_transmit_config      uart_transmit_config
#define usart_receive_config       uart_receive_config
#define usart_interrupt_enable     uart_interrupt_enable
#define usart_interrupt_disable    uart_interrupt_disable
#define usart_interrupt_flag_get   uart_interrupt_flag_get
#define usart_interrupt_flag_clear uart_interrupt_flag_clear
#define usart_flag_get             uart_flag_get
#define usart_flag_clear           uart_flag_clear
#define usart_data_receive         uart_data_receive
#define usart_data_transmit        uart_data_transmit
#define usart_dma_receive_config   uart_dma_receive_config
#define usart_dma_transmit_config  uart_dma_transmit_config

/* Flow control stubs (M53x UART has no hardware flow control support) */
#define usart_hardware_flow_rts_config(periph, rtsconfig)	\
	do {							\
		(void)(periph);				\
		(void)(rtsconfig);			\
	} while (0)
#define usart_hardware_flow_cts_config(periph, ctsconfig)	\
	do {							\
		(void)(periph);				\
		(void)(ctsconfig);			\
	} while (0)

#endif /* CONFIG_SOC_SERIES_GD32M53X */

#endif /* ZEPHYR_DRIVERS_SERIAL_USART_GD32_H_ */
