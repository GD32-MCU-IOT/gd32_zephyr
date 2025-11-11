/*
 * Copyright (c) 2021 BrainCo Inc.
 * Copyright (c) 2025 Claude-H7
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_I2C_I2C_GD32_H_
#define ZEPHYR_DRIVERS_I2C_I2C_GD32_H_
#define DT_DRV_COMPAT gd_gd32_i2c

#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/gd32.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/i2c.h>

#ifdef CONFIG_I2C_GD32_DMA
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/dma/dma_gd32.h>
#include <zephyr/cache.h>
#endif
#include <gd32_i2c.h>

#ifdef CONFIG_I2C_GD32_I2C_V3
/* I2C0/1/2: legacy IP, I2C3/4/5: novel ADD IP */
#define GD32_I2C_IS_LEGACY(periph)  ((periph) == I2C0 || (periph) == I2C1 || (periph) == I2C2)
#define GD32_I2C_IS_ADD(periph)     (!GD32_I2C_IS_LEGACY(periph))
#endif

/* I2C input clock frequency limits (MHz) for GD32F527 */
#define I2CCLK_MAX          ((uint32_t)0x00000036U)  /*  MHz maximum peripheral clock */
#define I2CCLK_MIN          ((uint32_t)0x00000002U)  /*   MHz minimum for standard mode */
#define I2CCLK_FM_MIN       ((uint32_t)0x00000008U)  /*   MHz minimum for fast mode (400 kHz) */
#define I2CCLK_FM_PLUS_MIN  ((uint32_t)0x00000018U)  /*  MHz minimum for fast mode plus (1 MHz) */

/* Bus error */
#define I2C_GD32_ERR_BERR BIT(0)
#define I2C_GD32_ERR_LARB BIT(1)
/* No ACK received */
#define I2C_GD32_ERR_AERR BIT(2)
/* I2C bus busy */
#define I2C_GD32_ERR_BUSY BIT(4)
/* Transfer length unsupported (e.g. >255 bytes on ADD IP without reload support) */
#define I2C_GD32_ERR_OVFL BIT(5)

#ifdef CONFIG_I2C_GD32_DMA

enum i2c_gd32_dma_direction {
	RX = 0,
	TX,
	NUM_OF_DIRECTION
};

struct i2c_gd32_dma_config {
	const struct device *dev;
	uint32_t channel;
	uint32_t config;
	uint32_t slot;
	uint32_t fifo_threshold;
};

struct i2c_gd32_dma_data {
	struct dma_config config;
	struct dma_block_config block;
	uint32_t count;
};

void i2c_gd32_dma_callback(const struct device *dma_dev, void *arg,
				  uint32_t channel, int status);
void i2c_gd32_dma_callback_gd(const struct device *dma_dev, void *arg,
				  uint32_t channel, int status);
size_t i2c_gd32_dma_enabled_num(const struct device *dev);
#endif
struct i2c_gd32_config {
	uint32_t reg;
	uint32_t bitrate;
	uint16_t clkid;
	struct reset_dt_spec reset;
	const struct pinctrl_dev_config *pcfg;
	void (*irq_cfg_func)(void);
#ifdef CONFIG_I2C_GD32_DMA
	const struct i2c_gd32_dma_config dma[NUM_OF_DIRECTION];
#endif
};

struct i2c_gd32_data {
	struct k_sem bus_mutex;
	struct k_sem sync_sem;
	uint32_t dev_config;
	uint16_t addr1;
	uint16_t addr2;
	uint32_t xfer_len;
	struct i2c_msg *current;
	uint8_t errs;
	bool is_restart;
#if defined(CONFIG_I2C_GD32_I2C_V2) || defined(CONFIG_I2C_GD32_I2C_V3)
	bool add_has_stop; /* Original grouped message wants STOP (AUTOEND only on final segment) */
#endif
#ifdef CONFIG_I2C_TARGET
	/* Slave (target) mode – legacy IP only (I2C0/1/2). ADD IP target mode not implemented */
	struct i2c_target_config *target_cfg;
	/* Track whether a master-mode transfer is active to avoid colliding with target ISR */
	bool master_active;
	/* Temporary single-byte buffer for slave transfers (optional) */
	uint8_t slave_tmp;
#endif
#ifdef CONFIG_I2C_GD32_DMA
	struct i2c_gd32_dma_data dma[NUM_OF_DIRECTION];
	/* Runtime DMA control flag */
	bool dma_enabled;
	/* Message count for multi-message transfers */
	uint32_t msg_count;
#endif
};

void i2c_gd32_event_isr_gd(const struct device *dev);

void i2c_gd32_error_isr_gd(const struct device *dev);
int i2c_gd32_transfer_gd(const struct device *dev,
			     struct i2c_msg *msgs,
			     uint8_t num_msgs,
			     uint16_t addr);
int i2c_gd32_configure_gd(const struct device *dev,
			      uint32_t dev_config);

#endif /* ZEPHYR_DRIVERS_I2C_I2C_GD32_H_ */
