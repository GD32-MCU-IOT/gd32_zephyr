/*
 * Copyright (c) 2025 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_I2C_I2C_GD32_H_
#define ZEPHYR_DRIVERS_I2C_I2C_GD32_H_

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
#ifdef CONFIG_SOC_SERIES_GD32F527
#define GD32_I2C_IS_LEGACY(periph) ((periph) == I2C0 || (periph) == I2C1 || (periph) == I2C2)
#endif

#ifdef CONFIG_SOC_SERIES_GD32E51X
/* E51x: I2C0/1 = legacy IP, I2C2 = ADD IP */
#define GD32_I2C_IS_LEGACY(periph) ((periph) == I2C0 || (periph) == I2C1)

/* E51x HAL compatibility layer - map I2C_ADD_* to I2C2_* */
/* Registers */
#define I2C_ADD_CTL0(periph)           I2C2_CTL0(periph)
#define I2C_ADD_CTL1(periph)           I2C2_CTL1(periph)
#define I2C_ADD_STAT(periph)           I2C2_STAT(periph)
#define I2C_ADD_STATC(periph)          I2C2_STATC(periph)
#define I2C_ADD_TDATA(periph)          I2C2_TDATA(periph)
#define I2C_ADD_RDATA(periph)          I2C2_RDATA(periph)
#define I2C_ADD_TIMING(periph)         I2C2_TIMING(periph)

/* CTL0 bits */
#define I2C_ADD_CTL0_I2CEN             I2C2_CTL0_I2CEN
#define I2C_ADD_CTL0_DENT              I2C2_CTL0_DENT
#define I2C_ADD_CTL0_DENR              I2C2_CTL0_DENR
#define I2C_ADD_CTL0_TIE               I2C2_CTL0_TIE
#define I2C_ADD_CTL0_RBNEIE            I2C2_CTL0_RBNEIE

/* CTL1 bits */
#define I2C_ADD_CTL1_AUTOEND           I2C2_CTL1_AUTOEND
#define I2C_ADD_CTL1_RELOAD            I2C2_CTL1_RELOAD
#define I2C_ADD_CTL1_START             I2C2_CTL1_START
#define I2C_ADD_CTL1_STOP              I2C2_CTL1_STOP

/* STAT bits */
#define I2C_ADD_STAT_I2CBSY            I2C2_STAT_I2CBSY
#define I2C_ADD_STAT_NACK              I2C2_STAT_NACK
#define I2C_ADD_STAT_STPDET            I2C2_STAT_STPDET
#define I2C_ADD_STAT_TC                I2C2_STAT_TC
#define I2C_ADD_STAT_TCR               I2C2_STAT_TCR
#define I2C_ADD_STAT_TBE               I2C2_STAT_TBE
#define I2C_ADD_STAT_TI                I2C2_STAT_TI
#define I2C_ADD_STAT_RBNE              I2C2_STAT_RBNE
#define I2C_ADD_STAT_ADDSEND           I2C2_STAT_ADDSEND
#define I2C_ADD_STAT_BERR              I2C2_STAT_BERR
#define I2C_ADD_STAT_LOSTARB           I2C2_STAT_LOSTARB

/* STATC bits */
#define I2C_ADD_STATC_NACKC            I2C2_STATC_NACKC

/* Interrupt flags */
#define I2C_ADD_INT_ERR                I2C2_INT_ERR
#define I2C_ADD_INT_TC                 I2C2_INT_TC
#define I2C_ADD_INT_STPDET             I2C2_INT_STPDET
#define I2C_ADD_INT_NACK               I2C2_INT_NACK
#define I2C_ADD_INT_TI                 I2C2_INT_TI
#define I2C_ADD_INT_RBNE               I2C2_INT_RBNE
#define I2C_ADD_INT_ADDM               I2C2_INT_ADDM

/* Flags */
#define I2C_ADD_FLAG_TBE               I2C2_FLAG_TBE
#define I2C_ADD_FLAG_TI                I2C2_FLAG_TI
#define I2C_ADD_FLAG_RBNE              I2C2_FLAG_RBNE
#define I2C_ADD_FLAG_ADDSEND           I2C2_FLAG_ADDSEND
#define I2C_ADD_FLAG_TC                I2C2_FLAG_TC
#define I2C_ADD_FLAG_TCR               I2C2_FLAG_TCR
#define I2C_ADD_FLAG_STPDET            I2C2_FLAG_STPDET
#define I2C_ADD_FLAG_NACK              I2C2_FLAG_NACK
#define I2C_ADD_FLAG_BERR              I2C2_FLAG_BERR
#define I2C_ADD_FLAG_LOSTARB           I2C2_FLAG_LOSTARB
#define I2C_ADD_FLAG_OUERR             I2C2_FLAG_OUERR
#define I2C_ADD_FLAG_PECERR            I2C2_FLAG_PECERR
#define I2C_ADD_FLAG_TIMEOUT           I2C2_FLAG_TIMEOUT
#define I2C_ADD_FLAG_SMBALT            I2C2_FLAG_SMBALT
#define I2C_ADD_FLAG_I2CBSY            I2C2_FLAG_I2CBSY
#define I2C_ADD_FLAG_TR                I2C2_FLAG_TR

/* DMA */
#define I2C_ADD_DMA_TRANSMIT           I2C2_DMA_TRANSMIT
#define I2C_ADD_DMA_RECEIVE            I2C2_DMA_RECEIVE

/* Master transfer direction */
#define I2C_ADD_MASTER_TRANSMIT        I2C2_MASTER_TRANSMIT
#define I2C_ADD_MASTER_RECEIVE         I2C2_MASTER_RECEIVE

/* Address format */
#define I2C_ADD_ADDFORMAT_7BITS        I2C2_ADDFORMAT_7BITS
#define I2C_ADD_ADDFORMAT_10BITS       I2C2_ADDFORMAT_10BITS

/* Function mappings */
#define i2c_add_interrupt_enable       i2c2_interrupt_enable
#define i2c_add_interrupt_disable      i2c2_interrupt_disable
#define i2c_add_flag_get               i2c2_flag_get
#define i2c_add_flag_clear             i2c2_flag_clear
#define i2c_add_dma_enable             i2c2_dma_enable
#define i2c_add_dma_disable            i2c2_dma_disable
#define i2c_add_enable                 i2c_enable
#define i2c_add_disable                i2c_disable
#define i2c_add_start_on_bus(periph)   (I2C2_CTL1(periph) |= I2C2_CTL1_START)
#define i2c_add_stop_on_bus(periph)    (I2C2_CTL1(periph) |= I2C2_CTL1_STOP)
#define i2c_add_automatic_end_enable   i2c_automatic_end_enable
#define i2c_add_automatic_end_disable  i2c_automatic_end_disable
#define i2c_add_transfer_byte_number_config i2c_transfer_byte_number_config
#define i2c_add_address10_enable       i2c_address10_enable
#define i2c_add_address10_disable      i2c_address10_disable
#define i2c_add_address10_header_disable i2c_address10_header_disable
#define i2c_add_master_addressing      i2c2_master_addressing
#define i2c_add_timing_config          i2c_timing_config
#define i2c_add_master_clock_config    i2c_master_clock_config
#endif /* CONFIG_SOC_SERIES_GD32E51X */

#define GD32_I2C_IS_ADD(periph)    (!GD32_I2C_IS_LEGACY(periph))
#endif

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

void i2c_gd32_dma_callback(const struct device *dma_dev, void *arg, uint32_t channel, int status);
void i2c_gd32_dma_callback_gd(const struct device *dma_dev, void *arg, uint32_t channel,
			      int status);
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
int i2c_gd32_transfer_gd(const struct device *dev, struct i2c_msg *msgs, uint8_t num_msgs,
			 uint16_t addr);
int i2c_gd32_configure_gd(const struct device *dev, uint32_t dev_config);

#endif /* ZEPHYR_DRIVERS_I2C_I2C_GD32_H_ */
