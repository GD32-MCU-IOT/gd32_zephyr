/*
 * Copyright (c) 2026, GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_CAN_GD32_V2_H_
#define ZEPHYR_DRIVERS_CAN_GD32_V2_H_

#include <zephyr/drivers/can.h>
#include <zephyr/drivers/clock_control/gd32.h>
#include <zephyr/drivers/pinctrl.h>
#include <soc.h>

#define CAN_GD32_TX_MAILBOX_NUM   (5U)
#define CAN_GD32_TX_MAILBOX_START (14U)

struct can_gd32_mailbox {
	can_tx_callback_t tx_callback;
	void *callback_arg;
};

struct can_gd32_data {
	struct can_driver_data common;
	struct k_mutex inst_mutex;
	struct k_mutex tx_mtx;
	struct k_sem tx_sem;
	struct can_gd32_mailbox mb[CAN_GD32_TX_MAILBOX_NUM];
	can_rx_callback_t rx_cb[CONFIG_CAN_GD32_CAN_V2_MAX_ID_FILTER];
	void *cb_arg[CONFIG_CAN_GD32_CAN_V2_MAX_ID_FILTER];
	struct can_filter filters[CONFIG_CAN_GD32_CAN_V2_MAX_ID_FILTER];
	uint8_t tx_mb_used[CAN_GD32_TX_MAILBOX_NUM];
	enum can_state state;
	bool hw_online;
	can_mode_t hw_mode;
};

struct can_gd32_config {
	struct can_driver_config common;
	uint32_t reg; /*!< CAN peripheral base address */
	uint32_t bitrate;
	uint16_t sample_point;
	uint8_t sjw;
	uint8_t prop_seg;
	uint8_t phase_seg1;
	uint8_t phase_seg2;
	struct gd32_pclken pclken;
	void (*config_irq)(uint32_t reg);
	const struct pinctrl_dev_config *pcfg;
};

#endif /* ZEPHYR_DRIVERS_CAN_GD32_V2_H_ */
