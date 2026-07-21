/*
 * Copyright (c) 2026, GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/can/transceiver.h>
#include <zephyr/drivers/clock_control/gd32.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/sys/util.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <soc.h>
#include <errno.h>
#include <stdbool.h>
#include <zephyr/drivers/can.h>
#include <zephyr/logging/log.h>
#include <zephyr/irq.h>
#include "can_gd32_v2.h"

LOG_MODULE_REGISTER(can_gd32_v2, CONFIG_CAN_LOG_LEVEL);

#define DT_DRV_COMPAT gd_gd32_can_v2

#define SP_IS_SET(inst) DT_INST_NODE_HAS_PROP(inst, sample_point) ||

/* Macro to exclude the sample point algorithm from compilation if not used
 * Without the macro, the algorithm would always waste ROM
 */
#define USE_SP_ALGO (DT_INST_FOREACH_STATUS_OKAY(SP_IS_SET) 0)

#define SP_AND_TIMING_NOT_SET(inst)                                                                \
	(!DT_INST_NODE_HAS_PROP(inst, sample_point) &&                                             \
	 !(DT_INST_NODE_HAS_PROP(inst, prop_seg) && DT_INST_NODE_HAS_PROP(inst, phase_seg1) &&     \
	   DT_INST_NODE_HAS_PROP(inst, phase_seg2))) ||

#if DT_INST_FOREACH_STATUS_OKAY(SP_AND_TIMING_NOT_SET) 0
#error You must either set a sampling-point or timings (phase-seg* and prop-seg)
#endif

static void can_gd32_signal_tx_complete(const struct device *dev, struct can_gd32_mailbox *mb,
					int status)
{
	can_tx_callback_t callback = mb->tx_callback;

	if (callback != NULL) {
		callback(dev, status, mb->callback_arg);
		mb->tx_callback = NULL;
	}
}

static int can_gd32_get_state(const struct device *dev, enum can_state *state,
			      struct can_bus_err_cnt *err_cnt)
{
	const struct can_gd32_config *cfg = dev->config;
	struct can_gd32_data *data = dev->data;
	uint32_t can = cfg->reg;
	can_error_counter_struct hal_err_cnt;

	if (err_cnt != NULL) {
		can_error_counter_get(can, &hal_err_cnt);
		err_cnt->tx_err_cnt = hal_err_cnt.tx_errcnt;
		err_cnt->rx_err_cnt = hal_err_cnt.rx_errcnt;
	}

	if (state != NULL) {
		if (!data->common.started) {
			*state = CAN_STATE_STOPPED;
		} else {
			switch (can_error_state_get(can)) {
			case CAN_ERROR_STATE_ACTIVE:
				can_error_counter_get(can, &hal_err_cnt);
				if ((hal_err_cnt.tx_errcnt < 96U) &&
				    (hal_err_cnt.rx_errcnt < 96U)) {
					*state = CAN_STATE_ERROR_ACTIVE;
				} else {
					*state = CAN_STATE_ERROR_WARNING;
				}
				break;
			case CAN_ERROR_STATE_PASSIVE:
				*state = CAN_STATE_ERROR_PASSIVE;
				break;
			case CAN_ERROR_STATE_BUS_OFF:
				*state = CAN_STATE_BUS_OFF;
				break;
			default:
				*state = CAN_STATE_ERROR_ACTIVE;
				break;
			}
		}
	}

	return 0;
}

static void can_gd32_state_change(const struct device *dev)
{
	struct can_gd32_data *data = dev->data;
	struct can_bus_err_cnt err_cnt;
	enum can_state state;

	(void)can_gd32_get_state(dev, &state, &err_cnt);

	if (state != data->state) {
		data->state = state;
		if (data->common.state_change_cb != NULL) {
			data->common.state_change_cb(dev, state, err_cnt,
						     data->common.state_change_cb_user_data);
		}
	}
}

static void can_gd32_error_isr(const struct device *dev)
{
	const struct can_gd32_config *cfg = dev->config;
	uint32_t can = cfg->reg;

	if (can_flag_get(can, CAN_FLAG_STUFF_ERR) == SET) {
		CAN_STATS_STUFF_ERROR_INC(dev);
		can_flag_clear(can, CAN_FLAG_STUFF_ERR);
	}
	if (can_flag_get(can, CAN_FLAG_FORM_ERR) == SET) {
		CAN_STATS_FORM_ERROR_INC(dev);
		can_flag_clear(can, CAN_FLAG_FORM_ERR);
	}
	if (can_flag_get(can, CAN_FLAG_CRC_ERR) == SET) {
		CAN_STATS_CRC_ERROR_INC(dev);
		can_flag_clear(can, CAN_FLAG_CRC_ERR);
	}
	if (can_flag_get(can, CAN_FLAG_ACK_ERR) == SET) {
		CAN_STATS_ACK_ERROR_INC(dev);
		can_flag_clear(can, CAN_FLAG_ACK_ERR);
	}
	if (can_flag_get(can, CAN_FLAG_BIT_DOMINANT_ERR) == SET) {
		CAN_STATS_BIT1_ERROR_INC(dev);
		can_flag_clear(can, CAN_FLAG_BIT_DOMINANT_ERR);
	}
	if (can_flag_get(can, CAN_FLAG_BIT_RECESSIVE_ERR) == SET) {
		CAN_STATS_BIT0_ERROR_INC(dev);
		can_flag_clear(can, CAN_FLAG_BIT_RECESSIVE_ERR);
	}

	/* Clear the error-summary interrupt flag (ERR1 bit 1). This bit must
	 * be explicitly cleared after handling all sub-error flags; failing to
	 * do so leaves the interrupt asserted and causes an infinite ISR
	 * re-entry loop that freezes the CPU.
	 */
	can_interrupt_flag_clear(can, CAN_INT_FLAG_ERR_SUMMARY);

	can_gd32_state_change(dev);
}

static void can_gd32_wake_up_isr(const struct device *dev)
{
	ARG_UNUSED(dev);
}

static void can_gd32_fast_error_isr(const struct device *dev)
{
	ARG_UNUSED(dev);
}

static void can_gd32_rx_fifo_isr(const struct device *dev)
{
	struct can_gd32_data *data = dev->data;
	const struct can_gd32_config *cfg = dev->config;
	uint32_t can = cfg->reg;
	struct can_frame frame;
	can_rx_fifo_struct rx_fifo;

	if (can_interrupt_flag_get(can, CAN_INT_FLAG_FIFO_AVAILABLE) == SET) {
		memset(&frame, 0, sizeof(frame));
		can_rx_fifo_read(can, &rx_fifo);

		frame.id = rx_fifo.id;
		frame.dlc = rx_fifo.dlc;
		if (rx_fifo.ide) {
			frame.flags |= CAN_FRAME_IDE;
		}
		if (rx_fifo.rtr) {
			frame.flags |= CAN_FRAME_RTR;
		}
		frame.data_32[0] = rx_fifo.data[0];
		frame.data_32[1] = rx_fifo.data[1];
#ifdef CONFIG_CAN_RX_TIMESTAMP
		frame.timestamp = rx_fifo.timestamp;
#endif

		if ((frame.flags & CAN_FRAME_RTR) != 0 && !IS_ENABLED(CONFIG_CAN_ACCEPT_RTR)) {
			goto overflow_check;
		}

		for (int i = 0; i < CONFIG_CAN_GD32_CAN_V2_MAX_ID_FILTER; i++) {
			if (data->rx_cb[i] != NULL &&
			    can_frame_matches_filter(&frame, &data->filters[i])) {
				data->rx_cb[i](dev, &frame, data->cb_arg[i]);
			}
		}
	}

overflow_check:
	if (can_interrupt_flag_get(can, CAN_INT_FLAG_FIFO_OVERFLOW) == SET) {
		LOG_ERR("RX FIFO overflow");
		can_interrupt_flag_clear(can, CAN_INT_FLAG_FIFO_OVERFLOW);
	}
}

static void can_gd32_tx_isr(const struct device *dev)
{
	struct can_gd32_data *data = dev->data;
	const struct can_gd32_config *cfg = dev->config;
	uint32_t can = cfg->reg;
	bool bus_off = (CAN_ERR1(can) & CAN_ERR1_BOF) != 0U;
	int status;

	for (uint8_t i = 0U; i < CAN_GD32_TX_MAILBOX_NUM; i++) {
		bool done = can_interrupt_flag_get(can, CAN_INT_FLAG_MB14 + i) == SET;

		if (!done && !bus_off) {
			continue;
		}

		status = done ? 0 : (bus_off ? -ENETUNREACH : -EIO);
		can_gd32_signal_tx_complete(dev, &data->mb[i], status);
		can_interrupt_flag_clear(can, CAN_INT_FLAG_MB14 + i);
		data->tx_mb_used[i] = 0U;

		if (done) {
			k_sem_give(&data->tx_sem);
		}
	}
}

static void can_gd32_message_buffer_isr(const struct device *dev)
{
	can_gd32_tx_isr(dev);
	can_gd32_rx_fifo_isr(dev);
}

static void can_gd32_bus_off_isr(const struct device *dev)
{
	const struct can_gd32_config *cfg = dev->config;
	uint32_t can = cfg->reg;

	can_gd32_tx_isr(dev);
	can_flag_clear(can, CAN_FLAG_BUSOFF);
	can_gd32_state_change(dev);
}

#define CAN_GD32_MODE_TIMEOUT 100000U

static void can_gd32_freeze(uint32_t can)
{
	uint32_t tmo = CAN_GD32_MODE_TIMEOUT;

	CAN_CTL0(can) &= ~CAN_CTL0_CANDIS;
	CAN_CTL0(can) |= CAN_CTL0_HALT | CAN_CTL0_INAMOD;

	while (((CAN_CTL0(can) & (CAN_CTL0_NRDY | CAN_CTL0_INAS)) !=
		(CAN_CTL0_NRDY | CAN_CTL0_INAS)) &&
	       (tmo != 0U)) {
		tmo--;
	}
}

/*
 * Bring the controller out of inactive / freeze mode. Same bounded-wait
 * rationale as can_gd32_freeze(): in loopback mode INAS may not clear, so a
 * timeout is not treated as fatal.
 */
static void can_gd32_unfreeze(uint32_t can)
{
	uint32_t tmo = CAN_GD32_MODE_TIMEOUT;

	CAN_CTL0(can) &= ~(CAN_CTL0_HALT | CAN_CTL0_INAMOD);

	while ((CAN_CTL0(can) & CAN_CTL0_INAS) && (tmo != 0U)) {
		tmo--;
	}
}

static int can_gd32_enter_init_mode(uint32_t can)
{
	can_gd32_freeze(can);
	return 0;
}

/*
 * Wake the controller from the reset / disable state. After a hard reset the
 * FlexCAN-style core is in disable mode (CANDIS=1); clearing the disable bit
 * and halting it settles the core before the real enter_init_mode below.
 */
static void can_gd32_leave_sleep_mode(uint32_t can)
{
	can_gd32_freeze(can);
}

static can_operation_modes_enum can_gd32_mode_to_hal(can_mode_t mode)
{
	if ((mode & CAN_MODE_LOOPBACK) != 0) {
		return CAN_LOOPBACK_SILENT_MODE;
	}

	if ((mode & CAN_MODE_LISTENONLY) != 0) {
		return CAN_MONITOR_MODE;
	}

	return CAN_NORMAL_MODE;
}

static int can_gd32_get_capabilities(const struct device *dev, can_mode_t *cap)
{
	ARG_UNUSED(dev);

	*cap = CAN_MODE_NORMAL | CAN_MODE_LOOPBACK | CAN_MODE_LISTENONLY;

	if (IS_ENABLED(CONFIG_CAN_MANUAL_RECOVERY_MODE)) {
		*cap |= CAN_MODE_MANUAL_RECOVERY;
	}

	return 0;
}

static int can_gd32_start(const struct device *dev)
{
	const struct can_gd32_config *cfg = dev->config;
	struct can_gd32_data *data = dev->data;
	uint32_t can = cfg->reg;
	int ret = 0;

	k_mutex_lock(&data->inst_mutex, K_FOREVER);

	if (data->common.started) {
		ret = -EALREADY;
		goto unlock;
	}

	if (cfg->common.phy != NULL) {
		ret = can_transceiver_enable(cfg->common.phy, data->common.mode);
		if (ret != 0) {
			LOG_ERR("failed to enable CAN transceiver (err %d)", ret);
			goto unlock;
		}
	}

	/*
	 * Bring the controller online the first time only. Once it has left
	 * freeze mode and synchronised, we keep it online across stop()/start()
	 * cycles (stop() is tracked purely in software). This avoids the bus-less
	 * loopback re-synchronisation problem, where a halted core can never
	 * clear INAS again.
	 */
	if (!data->hw_online) {
		can_gd32_unfreeze(can);
		data->hw_online = true;
	}

	data->common.started = true;
unlock:
	k_mutex_unlock(&data->inst_mutex);
	return ret;
}

static int can_gd32_stop(const struct device *dev)
{
	const struct can_gd32_config *cfg = dev->config;
	struct can_gd32_data *data = dev->data;
	uint32_t can = cfg->reg;
	int ret = 0;

	k_mutex_lock(&data->inst_mutex, K_FOREVER);

	if (!data->common.started) {
		ret = -EALREADY;
		goto unlock;
	}

	/*
	 * Stop is tracked in software: we keep the controller online (see
	 * can_gd32_start()) and only abort any in-flight transmissions. Sending
	 * is gated by data->common.started, so a stopped controller will not
	 * transmit.
	 */
	for (uint8_t i = 0U; i < CAN_GD32_TX_MAILBOX_NUM; i++) {
		can_mailbox_transmit_abort(can, CAN_GD32_TX_MAILBOX_START + i);
		can_gd32_signal_tx_complete(dev, &data->mb[i], -ENETDOWN);
		data->tx_mb_used[i] = 0U;
	}

	if (cfg->common.phy != NULL) {
		ret = can_transceiver_disable(cfg->common.phy);
		if (ret != 0) {
			LOG_ERR("failed to disable CAN transceiver (err %d)", ret);
			goto unlock;
		}
	}

	data->common.started = false;

unlock:
	k_mutex_unlock(&data->inst_mutex);
	return ret;
}

static void can_gd32_apply_mode_bits(uint32_t can, can_mode_t mode)
{
	switch (can_gd32_mode_to_hal(mode)) {
	case CAN_MONITOR_MODE:
		CAN_CTL1(can) &= ~CAN_CTL1_LSCMOD;
		CAN_CTL1(can) |= CAN_CTL1_MMOD;
		break;
	case CAN_LOOPBACK_SILENT_MODE:
		CAN_CTL1(can) &= ~CAN_CTL1_MMOD;
		CAN_CTL0(can) &= ~CAN_CTL0_SRDIS;
		CAN_FDCTL(can) &= ~CAN_FDCTL_TDCEN;
		CAN_CTL1(can) |= CAN_CTL1_LSCMOD;
		break;
	case CAN_NORMAL_MODE:
	default:
		CAN_CTL1(can) &= ~(CAN_CTL1_LSCMOD | CAN_CTL1_MMOD);
		break;
	}
}

static int can_gd32_set_mode(const struct device *dev, can_mode_t mode)
{
	const struct can_gd32_config *cfg = dev->config;
	struct can_gd32_data *data = dev->data;
	uint32_t can = cfg->reg;
	can_mode_t supported = CAN_MODE_LOOPBACK | CAN_MODE_LISTENONLY;

	LOG_DBG("Set mode %d", mode);

	if (IS_ENABLED(CONFIG_CAN_MANUAL_RECOVERY_MODE)) {
		supported |= CAN_MODE_MANUAL_RECOVERY;
	}

	if ((mode & ~supported) != 0) {
		LOG_ERR("unsupported mode: 0x%08x", mode);
		return -ENOTSUP;
	}

	if (data->common.started) {
		return -EBUSY;
	}

	k_mutex_lock(&data->inst_mutex, K_FOREVER);

	if (!data->hw_online) {
		can_gd32_freeze(can);
		can_gd32_apply_mode_bits(can, mode);
		data->hw_mode = mode;
	}
	data->common.mode = mode;

	k_mutex_unlock(&data->inst_mutex);
	return 0;
}

static int can_gd32_set_timing(const struct device *dev, const struct can_timing *timing)
{
	const struct can_gd32_config *cfg = dev->config;
	struct can_gd32_data *data = dev->data;
	uint32_t can = cfg->reg;

	k_mutex_lock(&data->inst_mutex, K_FOREVER);

	if (data->common.started) {
		k_mutex_unlock(&data->inst_mutex);
		return -EBUSY;
	}

	CAN_BT(can) &= ~(CAN_BT_PBS2 | CAN_BT_PBS1 | CAN_BT_PTS | CAN_BT_SJW | CAN_BT_BAUDPSC);
	CAN_BT(can) |= BT_PBS2((uint32_t)timing->phase_seg2 - 1U) |
		       BT_PBS1((uint32_t)timing->phase_seg1 - 1U) |
		       BT_PTS((uint32_t)timing->prop_seg - 1U) |
		       BT_SJW((uint32_t)timing->sjw - 1U) |
		       BT_BAUDPSC((uint32_t)timing->prescaler - 1U);

	k_mutex_unlock(&data->inst_mutex);
	return 0;
}

/*
 * Map a CAN peripheral base address to its RCU clock-source index.
 */
static can_idx_enum can_gd32_can_idx(uint32_t reg)
{
	if (reg == CAN2) {
		return IDX_CAN2;
	}

	if (reg == CAN1) {
		return IDX_CAN1;
	}

	return IDX_CAN0;
}

static int can_gd32_get_core_clock(const struct device *dev, uint32_t *rate)
{
	ARG_UNUSED(dev);

	/*
	 * The CAN kernel clock is fed from CK_APB2 (selected in can_gd32_init()
	 * via rcu_can_clock_config()). The generic GD32 clock controller cannot
	 * report this rate because the CAN enable bit lives in the additional
	 * APB2 register, so query the HAL directly.
	 */
	*rate = rcu_clock_freq_get(CK_APB2);
	if (*rate == 0U) {
		LOG_ERR("Failed to get CAN core clock rate");
		return -EIO;
	}

	return 0;
}

static int can_gd32_get_max_filters(const struct device *dev, bool ide)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(ide);

	return CONFIG_CAN_GD32_CAN_V2_MAX_ID_FILTER;
}

static int can_gd32_send(const struct device *dev, const struct can_frame *frame,
			 k_timeout_t timeout, can_tx_callback_t callback, void *user_data)
{
	const struct can_gd32_config *cfg = dev->config;
	struct can_gd32_data *data = dev->data;
	uint32_t can = cfg->reg;
	struct can_gd32_mailbox *mb;
	can_mailbox_descriptor_struct tx_msg;
	size_t data_length = can_dlc_to_bytes(frame->dlc);
	uint8_t mb_idx = CAN_GD32_TX_MAILBOX_NUM;
	int ret;

	LOG_DBG("Sending %zu bytes on %s. Id: 0x%x, ID type: %s, Remote Frame: %s", data_length,
		dev->name, frame->id, (frame->flags & CAN_FRAME_IDE) != 0 ? "extended" : "standard",
		(frame->flags & CAN_FRAME_RTR) != 0 ? "yes" : "no");

	__ASSERT(frame->dlc == 0U || frame->data != NULL, "Dataptr is null");

	if ((frame->flags & ~(CAN_FRAME_IDE | CAN_FRAME_RTR)) != 0) {
		LOG_ERR("unsupported CAN frame flags 0x%02x", frame->flags);
		return -ENOTSUP;
	}

	if (frame->dlc > CAN_MAX_DLC) {
		LOG_ERR("DLC of %d for non-FD format frame", frame->dlc);
		return -EINVAL;
	}

	if (!data->common.started) {
		return -ENETDOWN;
	}

	if (CAN_ERR1(can) & CAN_ERR1_BOF) {
		return -ENETUNREACH;
	}

	ret = k_sem_take(&data->tx_sem, timeout);
	if (ret != 0) {
		return -EAGAIN;
	}

	k_mutex_lock(&data->tx_mtx, K_FOREVER);

	for (uint8_t i = 0U; i < CAN_GD32_TX_MAILBOX_NUM; i++) {
		if (data->tx_mb_used[i] == 0U) {
			data->tx_mb_used[i] = 1U;
			mb_idx = i;
			break;
		}
	}

	if (mb_idx == CAN_GD32_TX_MAILBOX_NUM) {
		k_mutex_unlock(&data->tx_mtx);
		k_sem_give(&data->tx_sem);
		LOG_DBG("Transmit buffer full");
		return -EAGAIN;
	}

	mb = &data->mb[mb_idx];
	mb->tx_callback = callback;
	mb->callback_arg = user_data;

	memset(&tx_msg, 0, sizeof(tx_msg));
	tx_msg.code = CAN_MB_TX_STATUS_DATA;
	tx_msg.data_bytes = data_length;
	tx_msg.ide = (frame->flags & CAN_FRAME_IDE) != 0 ? 1U : 0U;
	tx_msg.rtr = (frame->flags & CAN_FRAME_RTR) != 0 ? 1U : 0U;
	tx_msg.id = frame->id;
#if defined(CONFIG_SOC_SERIES_GD32H75E)
	/* GD32H75E HAL: mailbox descriptor `data` is an inline uint8_t[64] array */
	memcpy(tx_msg.data, frame->data, data_length);
#else
	tx_msg.data = (uint32_t *)frame->data_32;
#endif

	can_mailbox_config(can, CAN_GD32_TX_MAILBOX_START + mb_idx, &tx_msg);

	k_mutex_unlock(&data->tx_mtx);
	return 0;
}

static int can_gd32_set_filter(const struct device *dev, const struct can_filter *filter)
{
	const struct can_gd32_config *cfg = dev->config;
	struct can_gd32_data *data = dev->data;
	uint32_t can = cfg->reg;
	uint32_t *filter_ram = (uint32_t *)(CAN_RAM(can) + 0x00000060U);
	bool ide = (filter->flags & CAN_FILTER_IDE) != 0;
	uint32_t val = 0U;
	int filter_id = -ENOSPC;

	for (int i = 0; i < CONFIG_CAN_GD32_CAN_V2_MAX_ID_FILTER; i++) {
		if (data->rx_cb[i] == NULL) {
			filter_id = i;
			break;
		}
	}

	if (filter_id < 0) {
		LOG_WRN("No free filter left");
		return -ENOSPC;
	}

	LOG_DBG("Adding filter_id %d, CAN ID: 0x%x, mask: 0x%x", filter_id, filter->id,
		filter->mask);

	/*
	 * Store the filter for the software acceptance matching performed in the
	 * RX FIFO ISR. The hardware FIFO is configured to accept every frame
	 * because the per-filter mask registers can only be written in freeze
	 * mode, which the bus-less loopback core cannot re-enter once it has
	 * synchronised (see can_gd32_rx_fifo_isr() and can_gd32_init()).
	 */
	data->filters[filter_id] = *filter;

	/*
	 * Populate the FIFO ID filter table element so the slot is active. The
	 * stored ID value is not used for acceptance (the public mask is
	 * all-don't-care); only the slot's presence matters.
	 */
	if (ide) {
		val |= CAN_FDESX_IDE_A;
		val |= (uint32_t)FIFO_FILTER_ID_EXD_A(filter->id);
	} else {
		val |= (uint32_t)FIFO_FILTER_ID_STD_A(filter->id);
	}
	filter_ram[filter_id] = val;

	can_rx_fifo_clear(can);

	return filter_id;
}

static int can_gd32_add_rx_filter(const struct device *dev, can_rx_callback_t cb, void *cb_arg,
				  const struct can_filter *filter)
{
	struct can_gd32_data *data = dev->data;
	int filter_id;

	if ((filter->flags & ~(CAN_FILTER_IDE)) != 0) {
		LOG_ERR("unsupported CAN filter flags 0x%02x", filter->flags);
		return -ENOTSUP;
	}

	k_mutex_lock(&data->inst_mutex, K_FOREVER);

	filter_id = can_gd32_set_filter(dev, filter);
	if (filter_id >= 0) {
		data->rx_cb[filter_id] = cb;
		data->cb_arg[filter_id] = cb_arg;
	}

	k_mutex_unlock(&data->inst_mutex);
	return filter_id;
}

static void can_gd32_remove_rx_filter(const struct device *dev, int filter_id)
{
	const struct can_gd32_config *cfg = dev->config;
	struct can_gd32_data *data = dev->data;
	uint32_t can = cfg->reg;
	uint32_t *filter_ram = (uint32_t *)(CAN_RAM(can) + 0x00000060U);

	__ASSERT_NO_MSG(filter_id >= 0 && filter_id < CONFIG_CAN_GD32_CAN_V2_MAX_ID_FILTER);

	k_mutex_lock(&data->inst_mutex, K_FOREVER);

	if (filter_id < CONFIG_CAN_GD32_CAN_V2_MAX_ID_FILTER) {
		data->rx_cb[filter_id] = NULL;
		data->cb_arg[filter_id] = NULL;
		memset(&data->filters[filter_id], 0, sizeof(data->filters[filter_id]));

		filter_ram[filter_id] = 0x00000000U;
		can_rx_fifo_clear(can);

		LOG_DBG("Removing filter_id %d", filter_id);
	}

	k_mutex_unlock(&data->inst_mutex);
}

static void can_gd32_set_state_change_callback(const struct device *dev,
					       can_state_change_callback_t cb, void *user_data)
{
	struct can_gd32_data *data = dev->data;

	data->common.state_change_cb = cb;
	data->common.state_change_cb_user_data = user_data;
}

#ifdef CONFIG_CAN_MANUAL_RECOVERY_MODE
static int can_gd32_recover(const struct device *dev, k_timeout_t timeout)
{
	const struct can_gd32_config *cfg = dev->config;
	struct can_gd32_data *data = dev->data;
	uint32_t can = cfg->reg;
	int ret = -EAGAIN;
	int64_t start_time;

	if (!data->common.started) {
		return -ENETDOWN;
	}

	if ((data->common.mode & CAN_MODE_MANUAL_RECOVERY) == 0U) {
		return -ENOTSUP;
	}

	if (!(CAN_ERR1(can) & CAN_ERR1_BOF)) {
		return 0;
	}

	if (k_mutex_lock(&data->inst_mutex, K_FOREVER)) {
		return -EAGAIN;
	}

	ret = can_gd32_enter_init_mode(can);
	if (ret) {
		goto done;
	}

	can_gd32_unfreeze(can);

	start_time = k_uptime_ticks();
	while (CAN_ERR1(can) & CAN_ERR1_BOF) {
		if (!K_TIMEOUT_EQ(timeout, K_FOREVER) &&
		    k_uptime_ticks() - start_time >= timeout.ticks) {
			goto done;
		}
	}

	ret = 0;

done:
	k_mutex_unlock(&data->inst_mutex);
	return ret;
}
#endif /* CONFIG_CAN_MANUAL_RECOVERY_MODE */

static void can_gd32_enable_interrupts(uint32_t can)
{
	can_interrupt_enable(can, CAN_INT_FIFO_AVAILABLE);
	for (uint8_t i = 0U; i < CAN_GD32_TX_MAILBOX_NUM; i++) {
		can_interrupt_enable(can, CAN_INT_MB14 + i);
	}
	can_interrupt_enable(can, CAN_INT_BUSOFF);
	can_interrupt_enable(can, CAN_INT_ERR_SUMMARY);
}

static int can_gd32_init(const struct device *dev)
{
	const struct can_gd32_config *cfg = dev->config;
	struct can_gd32_data *data = dev->data;
	uint32_t can = cfg->reg;
	can_fifo_parameter_struct fifo_param;
	struct can_timing timing;
	int ret;

	k_mutex_init(&data->inst_mutex);
	k_mutex_init(&data->tx_mtx);
	k_sem_init(&data->tx_sem, CAN_GD32_TX_MAILBOX_NUM, CAN_GD32_TX_MAILBOX_NUM);

	if (cfg->common.phy != NULL) {
		if (!device_is_ready(cfg->common.phy)) {
			LOG_ERR("CAN transceiver not ready");
			return -ENODEV;
		}
	}

	if (!device_is_ready(GD32_CLOCK_CONTROLLER)) {
		LOG_ERR("clock controller not ready");
		return -ENODEV;
	}

	ret = clock_control_on(GD32_CLOCK_CONTROLLER, (clock_control_subsys_t *)&cfg->pclken);
	if (ret != 0) {
		LOG_ERR("Could not enable CAN clock (%d)", ret);
		return -EIO;
	}

	/*
	 * Select CK_APB2 as the CAN kernel clock source. Without a running
	 * kernel clock the controller cannot leave inactive mode, and
	 * can_gd32_get_core_clock() reports the same source.
	 */
	rcu_can_clock_config(can_gd32_can_idx(cfg->reg), RCU_CANSRC_APB2);

	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("CAN pinctrl setup failed (%d)", ret);
		return ret;
	}

	/*
	 * Wake the controller from reset / disable state before the real
	 * enter_init_mode below. See can_gd32_leave_sleep_mode() for details.
	 */
	can_gd32_leave_sleep_mode(can);

	ret = can_gd32_enter_init_mode(can);
	if (ret) {
		LOG_ERR("Failed to enter init mode");
		return ret;
	}

	timing.sjw = cfg->sjw;
	if (cfg->sample_point && USE_SP_ALGO) {
		ret = can_calc_timing(dev, &timing, cfg->bitrate, cfg->sample_point);
		if (ret < 0) {
			LOG_ERR("Can't find timing for given param");
			return -EIO;
		}
		LOG_DBG("Presc: %d, TS1: %d, TS2: %d", timing.prescaler, timing.phase_seg1,
			timing.phase_seg2);
		LOG_DBG("Sample-point err : %d", ret);
	} else {
		timing.prop_seg = cfg->prop_seg;
		timing.phase_seg1 = cfg->phase_seg1;
		timing.phase_seg2 = cfg->phase_seg2;
		timing.prescaler = 1U;
	}

	ret = can_gd32_set_timing(dev, &timing);
	if (ret) {
		return ret;
	}

	/* IDE and RTR fields take part in the acceptance filtering. */
	CAN_CTL2(can) |= CAN_IDE_RTR_FILTERED;

	/*
	 * Acceptance filtering is performed in software (see
	 * can_gd32_rx_fifo_isr()). Configure the RX FIFO to accept every frame
	 * by programming an all-don't-care public acceptance mask. The per-filter
	 * private mask registers (RFIFOMPF) cannot be used for masked filters
	 * because they are only writable while the controller is in freeze mode,
	 * which the bus-less loopback core cannot re-enter once it has
	 * synchronised. With RPFQEN left cleared, can_rx_fifo_config() copies the
	 * public mask into every private mask register, so all 32 slots accept
	 * any ID.
	 */
	fifo_param.dma_enable = (uint8_t)DISABLE;
	fifo_param.filter_format_and_number = CAN_RXFIFO_FILTER_A_NUM_32;
	fifo_param.fifo_public_filter = 0U;
	CAN_RMPUBF(can) = 0U;
	can_rx_fifo_config(can, &fifo_param);

	ret = can_gd32_set_mode(dev, CAN_MODE_NORMAL);
	if (ret) {
		return ret;
	}

	(void)can_gd32_get_state(dev, &data->state, NULL);

	cfg->config_irq(can);

	return 0;
}

static DEVICE_API(can, can_api_funcs) = {
	.get_capabilities = can_gd32_get_capabilities,
	.start = can_gd32_start,
	.stop = can_gd32_stop,
	.set_mode = can_gd32_set_mode,
	.set_timing = can_gd32_set_timing,
	.send = can_gd32_send,
	.add_rx_filter = can_gd32_add_rx_filter,
	.remove_rx_filter = can_gd32_remove_rx_filter,
	.get_state = can_gd32_get_state,
#ifdef CONFIG_CAN_MANUAL_RECOVERY_MODE
	.recover = can_gd32_recover,
#endif
	.set_state_change_callback = can_gd32_set_state_change_callback,
	.get_core_clock = can_gd32_get_core_clock,
	.get_max_filters = can_gd32_get_max_filters,
	.timing_min = {
			.sjw = 0x01,
			.prop_seg = 0x01,
			.phase_seg1 = 0x01,
			.phase_seg2 = 0x01,
			.prescaler = 0x01,
		},
	.timing_max = {
			.sjw = 0x20,
			.prop_seg = 0x40,
			.phase_seg1 = 0x20,
			.phase_seg2 = 0x20,
			.prescaler = 0x400,
		},
};

#define CAN_GD32_IRQ_INST(inst)                                                                    \
	static void config_can_##inst##_irq(uint32_t can)                                          \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, wake_up, irq),                               \
			    DT_INST_IRQ_BY_NAME(inst, wake_up, priority), can_gd32_wake_up_isr,    \
			    DEVICE_DT_INST_GET(inst), 0);                                          \
		irq_enable(DT_INST_IRQ_BY_NAME(inst, wake_up, irq));                               \
		IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, message_buffer, irq),                        \
			    DT_INST_IRQ_BY_NAME(inst, message_buffer, priority),                   \
			    can_gd32_message_buffer_isr, DEVICE_DT_INST_GET(inst), 0);             \
		irq_enable(DT_INST_IRQ_BY_NAME(inst, message_buffer, irq));                        \
		IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, bus_off, irq),                               \
			    DT_INST_IRQ_BY_NAME(inst, bus_off, priority), can_gd32_bus_off_isr,    \
			    DEVICE_DT_INST_GET(inst), 0);                                          \
		irq_enable(DT_INST_IRQ_BY_NAME(inst, bus_off, irq));                               \
		IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, fast_error, irq),                            \
			    DT_INST_IRQ_BY_NAME(inst, fast_error, priority),                       \
			    can_gd32_fast_error_isr, DEVICE_DT_INST_GET(inst), 0);                 \
		irq_enable(DT_INST_IRQ_BY_NAME(inst, fast_error, irq));                            \
		IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, error, irq),                                 \
			    DT_INST_IRQ_BY_NAME(inst, error, priority), can_gd32_error_isr,        \
			    DEVICE_DT_INST_GET(inst), 0);                                          \
		irq_enable(DT_INST_IRQ_BY_NAME(inst, error, irq));                                 \
		can_gd32_enable_interrupts(can);                                                   \
	}

#define CAN_GD32_CONFIG_INST(inst)                                                                 \
	PINCTRL_DT_INST_DEFINE(inst);                                                              \
	static const struct can_gd32_config can_gd32_cfg_##inst = {                                \
		.common = CAN_DT_DRIVER_CONFIG_INST_GET(inst, 0, 1000000),                         \
		.reg = DT_INST_REG_ADDR(inst),                                                     \
		.bitrate = DT_INST_PROP(inst, bitrate),                                            \
		.sample_point = DT_INST_PROP_OR(inst, sample_point, 0),                            \
		.sjw = DT_INST_PROP_OR(inst, sjw, 1),                                              \
		.prop_seg = DT_INST_PROP_OR(inst, prop_seg, 1),                                    \
		.phase_seg1 = DT_INST_PROP_OR(inst, phase_seg1, 1),                                \
		.phase_seg2 = DT_INST_PROP_OR(inst, phase_seg2, 1),                                \
		.pclken =                                                                          \
			{                                                                          \
				.enr = DT_INST_CLOCKS_CELL(inst, id),                              \
				.bus = DT_INST_CLOCKS_CELL(inst, id),                              \
			},                                                                         \
		.config_irq = config_can_##inst##_irq,                                             \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                                      \
	};

#define CAN_GD32_DATA_INST(inst) static struct can_gd32_data can_gd32_dev_data_##inst;

#define CAN_GD32_DEFINE_INST(inst)                                                                 \
	DEVICE_DT_INST_DEFINE(inst, &can_gd32_init, NULL, &can_gd32_dev_data_##inst,               \
			      &can_gd32_cfg_##inst, POST_KERNEL, CONFIG_CAN_INIT_PRIORITY,         \
			      &can_api_funcs);

#define CAN_GD32_INST(inst)                                                                        \
	CAN_GD32_IRQ_INST(inst)                                                                    \
	CAN_GD32_CONFIG_INST(inst)                                                                 \
	CAN_GD32_DATA_INST(inst)                                                                   \
	CAN_GD32_DEFINE_INST(inst)

DT_INST_FOREACH_STATUS_OKAY(CAN_GD32_INST)
