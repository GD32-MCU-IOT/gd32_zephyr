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
#include "can_gd32.h"

LOG_MODULE_REGISTER(can_gd32, CONFIG_CAN_LOG_LEVEL);

#define CAN_INIT_TIMEOUT (10 * sys_clock_hw_cycles_per_sec() / MSEC_PER_SEC)

#define DT_DRV_COMPAT gd_gd32_can

#ifdef CAN_BT_BS1_6_4
#define CAN_GD32_BT_TIMING_MASK \
	(CAN_BT_BS1_3_0 | CAN_BT_BS1_6_4 | CAN_BT_BS2_2_0 | CAN_BT_BS2_4_3 | \
	 CAN_BT_BAUDPSC)
#elif defined(CAN_BT_BS1_3_0)
#define CAN_GD32_BT_TIMING_MASK \
	(CAN_BT_BS1_3_0 | CAN_BT_BS2_2_0 | CAN_BT_BAUDPSC)
#endif

#ifdef CAN_TMP_FDF
#define CAN_GD32_TMP_FRAME_MASK (CAN_TMP_DLENC | CAN_TMP_ESI | CAN_TMP_FDF)
#elif defined(CAN_TMP_DLENC)
#define CAN_GD32_TMP_FRAME_MASK CAN_TMP_DLENC
#endif

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

#if (CONFIG_CAN_MAX_STD_ID_FILTER + CONFIG_CAN_MAX_EXT_ID_FILTER * 2) >                            \
	(CAN_GD32_NUM_FILTER_BANKS * 2)
#error Number of configured filters exceeds available filter bank slots.
#endif

/* Mutex to prevent simultaneous access to filter registers shared between CAN0 and CAN1. */
static struct k_mutex filter_mutex;

static void can_gd32_signal_tx_complete(const struct device *dev, struct can_gd32_mailbox *mb,
					int status)
{
	can_tx_callback_t callback = mb->tx_callback;

	if (callback != NULL) {
		callback(dev, status, mb->callback_arg);
		mb->tx_callback = NULL;
	}
}

static void can_gd32_rx_fifo_pop(CAN_RxFIFOMail_TypeDef *Rx_FIFO_Mail, struct can_frame *frame)
{
#ifdef CONFIG_CAN_FD_MODE
	uint8_t i = 0U;
	size_t data_length = 0U;
#endif

	memset(frame, 0, sizeof(*frame));

	if (Rx_FIFO_Mail->CANX_RFIFOMI & CAN_RFIFOMI_FF) {
		frame->id = Rx_FIFO_Mail->CANX_RFIFOMI >> CAN_RFIFOMI_EXID_OFFSET;
		frame->flags |= CAN_FRAME_IDE;
	} else {
		frame->id = Rx_FIFO_Mail->CANX_RFIFOMI >> CAN_RFIFOMI_STID_POFFSET;
	}
#ifdef CONFIG_CAN_FD_MODE
	if ((Rx_FIFO_Mail->CANX_RFIFOMP & CAN_RFIFOMP_FDF)) {
		frame->flags |= CAN_FRAME_FDF;
		if ((Rx_FIFO_Mail->CANX_RFIFOMP & CAN_RFIFOMP_BRS)) {
			frame->flags |= CAN_FRAME_BRS;
		}
		frame->dlc = Rx_FIFO_Mail->CANX_RFIFOMP & CAN_RFIFOMP_DLENC;
		data_length = can_dlc_to_bytes(frame->dlc);
		/* get the data length for word */
		data_length = data_length / 4U;
		/* data length is 5-7 need receive 2 word */
		if ((1U == data_length) && (4U != frame->dlc)) {
			data_length++;
		}
		if (0U == data_length) {
			/* if data length less than 4 bytes */
			frame->data_32[0] = Rx_FIFO_Mail->CANX_RFIFODATA0;
		} else {
			/* get the data by reading from CAN_RFIFOMDATA0 register*/
			for (i = 0U; i < data_length; i++) {
				frame->data_32[i] = Rx_FIFO_Mail->CANX_RFIFODATA0;
			}
		}
	} else { /* non-FD frame */
		if ((Rx_FIFO_Mail->CANX_RFIFOMI & CAN_RFIFOMI_RTR) != 0) {
			frame->flags |= CAN_FRAME_RTR;
		}
		frame->dlc = Rx_FIFO_Mail->CANX_RFIFOMP & CAN_RFIFOMP_DLENC;
		frame->data_32[0] = Rx_FIFO_Mail->CANX_RFIFODATA0;
		frame->data_32[1] = Rx_FIFO_Mail->CANX_RFIFODATA1;
	}

#else
	if ((Rx_FIFO_Mail->CANX_RFIFOMI & CAN_RFIFOMI_RTR) != 0) {
		frame->flags |= CAN_FRAME_RTR;
	}
	frame->dlc = Rx_FIFO_Mail->CANX_RFIFOMP & CAN_RFIFOMP_DLENC;
	frame->data_32[0] = Rx_FIFO_Mail->CANX_RFIFODATA0;
	frame->data_32[1] = Rx_FIFO_Mail->CANX_RFIFODATA1;
#endif

#ifdef CONFIG_CAN_RX_TIMESTAMP
	frame->timestamp = ((Rx_FIFO_Mail->CANX_RFIFOMP & CAN_RFIFOMP_TS) >> CAN_RFIFOMP_TS_OFFSET);
#endif
}

static inline void can_gd32_rx_isr_handler(const struct device *dev)
{
	struct can_gd32_data *data = dev->data;
	const struct can_gd32_config *cfg = dev->config;
	GD_CAN_TypeDef *can = cfg->can;
	int filter_id, index;
	struct can_frame frame;
	can_rx_callback_t callback = NULL;
	CAN_RxFIFOMail_TypeDef *Rx_FIFO_Mail;
	void *cb_arg;

	while (can->CANX_RFIFO0 & CAN_RFIFO0_RFL0) {
		Rx_FIFO_Mail = &can->RXFIFOMAIL[0];
		filter_id =
			((Rx_FIFO_Mail->CANX_RFIFOMP & CAN_RFIFOMP_FI) >> CAN_RFIFOMP_FI_OFFSET);

		LOG_DBG("Message on filter_id %d", filter_id);

		can_gd32_rx_fifo_pop(Rx_FIFO_Mail, &frame);

		if (filter_id < CONFIG_CAN_MAX_EXT_ID_FILTER) {
			callback = data->rx_cb_ext[filter_id];
			cb_arg = data->cb_arg_ext[filter_id];
		} else if (filter_id < CAN_GD32_MAX_FILTER_ID) {
			index = filter_id - CONFIG_CAN_MAX_EXT_ID_FILTER;
			callback = data->rx_cb_std[index];
			cb_arg = data->cb_arg_std[index];
		}

		if (callback) {
			callback(dev, &frame, cb_arg);
		}

		/* Release message */
		can->CANX_RFIFO0 |= CAN_RFIFO0_RFD0;
	}

	if (can->CANX_RFIFO0 & CAN_RFIFO0_RFO0) {
		LOG_ERR("RX FIFO Overflow");
	}
}

static int can_gd32_get_state(const struct device *dev, enum can_state *state,
			      struct can_bus_err_cnt *err_cnt)
{
	const struct can_gd32_config *cfg = dev->config;
	struct can_gd32_data *data = dev->data;
	GD_CAN_TypeDef *can = cfg->can;

	if (state != NULL) {
		if (!data->common.started) {
			*state = CAN_STATE_STOPPED;
		} else if (can->CANX_ERR & CAN_ERR_BOERR) {
			*state = CAN_STATE_BUS_OFF;
		} else if (can->CANX_ERR & CAN_ERR_PERR) {
			*state = CAN_STATE_ERROR_PASSIVE;
		} else if (can->CANX_ERR & CAN_ERR_WERR) {
			*state = CAN_STATE_ERROR_WARNING;
		} else {
			*state = CAN_STATE_ERROR_ACTIVE;
		}
	}

	if (err_cnt != NULL) {
		err_cnt->tx_err_cnt = ((can->CANX_ERR & CAN_ERR_TECNT) >> CAN_ERR_TECNT_OFFSET);
		err_cnt->rx_err_cnt = ((can->CANX_ERR & CAN_ERR_RECNT) >> CAN_ERR_RECNT_OFFSET);
	}

	return 0;
}

static inline void can_gd32_bus_state_change_isr(const struct device *dev)
{
	struct can_gd32_data *data = dev->data;
	struct can_bus_err_cnt err_cnt;
	enum can_state state;
	const can_state_change_callback_t cb = data->common.state_change_cb;
	void *state_change_cb_data = data->common.state_change_cb_user_data;

#ifdef CONFIG_CAN_STATS
	const struct can_gd32_config *cfg = dev->config;
	GD_CAN_TypeDef *can = cfg->can;

	switch (can->CANX_ERR & CAN_ERR_ERRN) {
	case (CAN_ERRN_1):
		CAN_STATS_STUFF_ERROR_INC(dev);
		break;
	case (CAN_ERRN_2):
		CAN_STATS_FORM_ERROR_INC(dev);
		break;
	case (CAN_ERRN_3):
		CAN_STATS_ACK_ERROR_INC(dev);
		break;
	case (CAN_ERRN_4):
		CAN_STATS_BIT1_ERROR_INC(dev);
		break;
	case (CAN_ERRN_5):
		CAN_STATS_BIT0_ERROR_INC(dev);
		break;
	case (CAN_ERRN_6):
		CAN_STATS_CRC_ERROR_INC(dev);
		break;
	default:
		break;
	}

	/* Clear the last error code flag */
	can->CANX_ERR |= CAN_ERR_ERRN;
#endif /* CONFIG_CAN_STATS */

	(void)can_gd32_get_state(dev, &state, &err_cnt);

	if (state != data->state) {
		data->state = state;

		if (cb != NULL) {
			cb(dev, state, err_cnt, state_change_cb_data);
		}
	}
}

static inline void can_gd32_tx_isr_handler(const struct device *dev)
{
	struct can_gd32_data *data = dev->data;
	const struct can_gd32_config *cfg = dev->config;
	GD_CAN_TypeDef *can = cfg->can;
	uint32_t bus_off;
	int status;

	bus_off = can->CANX_ERR & CAN_ERR_BOERR;

	if ((can->CANX_TSTAT & CAN_TSTAT_MTF0) | bus_off) {
		status = can->CANX_TSTAT & CAN_TSTAT_MTFNERR0 ? 0
			 : can->CANX_TSTAT & CAN_TSTAT_MTE0   ? -EIO
			 : can->CANX_TSTAT & CAN_TSTAT_MAL0   ? -EBUSY
			 : bus_off                            ? -ENETUNREACH
							      : -EIO;
		/* clear the TX mailbox0 request */
		can->CANX_TSTAT |= CAN_TSTAT_MTF0;
		can_gd32_signal_tx_complete(dev, &data->mb0, status);
	}

	if ((can->CANX_TSTAT & CAN_TSTAT_MTF1) | bus_off) {
		status = can->CANX_TSTAT & CAN_TSTAT_MTFNERR1 ? 0
			 : can->CANX_TSTAT & CAN_TSTAT_MTE1   ? -EIO
			 : can->CANX_TSTAT & CAN_TSTAT_MAL1   ? -EBUSY
			 : bus_off                            ? -ENETUNREACH
							      : -EIO;
		/* clear the TX mailbox1 request */
		can->CANX_TSTAT |= CAN_TSTAT_MTF1;
		can_gd32_signal_tx_complete(dev, &data->mb1, status);
	}

	if ((can->CANX_TSTAT & CAN_TSTAT_MTF2) | bus_off) {
		status = can->CANX_TSTAT & CAN_TSTAT_MTFNERR2 ? 0
			 : can->CANX_TSTAT & CAN_TSTAT_MTE2   ? -EIO
			 : can->CANX_TSTAT & CAN_TSTAT_MAL2   ? -EBUSY
			 : bus_off                            ? -ENETUNREACH
							      : -EIO;
		/* clear the TX mailbox2 request  */
		can->CANX_TSTAT |= CAN_TSTAT_MTF2;
		can_gd32_signal_tx_complete(dev, &data->mb2, status);
	}

	if (can->CANX_TSTAT & (CAN_TSTAT_TME0 | CAN_TSTAT_TME1 | CAN_TSTAT_TME2)) {
		k_sem_give(&data->tx_int_sem);
	}
}

static void can_gd32_rx_isr(const struct device *dev)
{
	can_gd32_rx_isr_handler(dev);
}

static void can_gd32_tx_isr(const struct device *dev)
{
	can_gd32_tx_isr_handler(dev);
}

static void can_gd32_state_change_isr(const struct device *dev)
{
	const struct can_gd32_config *cfg = dev->config;
	GD_CAN_TypeDef *can = cfg->can;

	/* error interrupt flag */
	if (can->CANX_STAT & CAN_STAT_ERRIF) {
		can_gd32_tx_isr_handler(dev);
		can_gd32_bus_state_change_isr(dev);
		can->CANX_STAT |= CAN_STAT_ERRIF;
	}
}

static int can_gd32_enter_init_mode(GD_CAN_TypeDef *can)
{
	uint32_t start_time;

	/* enter initialization mode */
	can->CANX_CTL |= CAN_CTL_IWMOD;
	start_time = k_cycle_get_32();
	/* Wait for the initialization mode to enter successfully */
	while ((can->CANX_STAT & CAN_STAT_IWS) == 0U) {
		if (k_cycle_get_32() - start_time > CAN_INIT_TIMEOUT) {
			can->CANX_CTL &= ~CAN_CTL_IWMOD;
			return -EAGAIN;
		}
	}

	return 0;
}

static int can_gd32_exit_init_mode(GD_CAN_TypeDef *can)
{
	uint32_t start_time;
	/* exit initialization mode */
	can->CANX_CTL &= ~CAN_CTL_IWMOD;
	start_time = k_cycle_get_32();
	/*  Wait for the initialization mode to exit successfully */
	while ((can->CANX_STAT & CAN_STAT_IWS) != 0U) {
		if (k_cycle_get_32() - start_time > CAN_INIT_TIMEOUT) {
			return -EAGAIN;
		}
	}

	return 0;
}

static int can_gd32_exit_sleep_mode(GD_CAN_TypeDef *can)
{
	uint32_t start_time;
	/* exit sleep mode */
	can->CANX_CTL &= ~CAN_CTL_SLPWMOD;
	start_time = k_cycle_get_32();
	/*  Wait for the sleep mode to exit successfully */
	while ((can->CANX_STAT & CAN_STAT_SLPWS) != 0) {
		if (k_cycle_get_32() - start_time > CAN_INIT_TIMEOUT) {
			return -EAGAIN;
		}
	}

	return 0;
}

static int can_gd32_get_capabilities(const struct device *dev, can_mode_t *cap)
{
	ARG_UNUSED(dev);

	*cap = CAN_MODE_NORMAL | CAN_MODE_LOOPBACK | CAN_MODE_LISTENONLY | CAN_MODE_ONE_SHOT;

	if (IS_ENABLED(CONFIG_CAN_MANUAL_RECOVERY_MODE)) {
		*cap |= CAN_MODE_MANUAL_RECOVERY;
	}

	if (IS_ENABLED(CONFIG_CAN_FD_MODE)) {
		*cap |= CAN_MODE_FD;
	}

	return 0;
}

static int can_gd32_start(const struct device *dev)
{
	const struct can_gd32_config *cfg = dev->config;
	struct can_gd32_data *data = dev->data;
	GD_CAN_TypeDef *can = cfg->can;
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

	ret = can_gd32_exit_init_mode(can);
	if (ret < 0) {
		LOG_ERR("Failed to leave init mode");

		if (cfg->common.phy != NULL) {
			/* attempt to disable the CAN transceiver in case of error */
			(void)can_transceiver_disable(cfg->common.phy);
		}

		ret = -EIO;
		goto unlock;
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
	GD_CAN_TypeDef *can = cfg->can;
	int ret = 0;

	k_mutex_lock(&data->inst_mutex, K_FOREVER);

	if (!data->common.started) {
		ret = -EALREADY;
		goto unlock;
	}

	ret = can_gd32_enter_init_mode(can);
	if (ret < 0) {
		LOG_ERR("Failed to enter init mode");
		ret = -EIO;
		goto unlock;
	}

	/* Abort any pending transmissions */
	can_gd32_signal_tx_complete(dev, &data->mb0, -ENETDOWN);
	can_gd32_signal_tx_complete(dev, &data->mb1, -ENETDOWN);
	can_gd32_signal_tx_complete(dev, &data->mb2, -ENETDOWN);
	can->CANX_TSTAT |= CAN_TSTAT_MST0 | CAN_TSTAT_MST1 | CAN_TSTAT_MST2;

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

static int can_gd32_set_mode(const struct device *dev, can_mode_t mode)
{
	can_mode_t supported = CAN_MODE_LOOPBACK | CAN_MODE_LISTENONLY | CAN_MODE_ONE_SHOT;
	const struct can_gd32_config *cfg = dev->config;
	GD_CAN_TypeDef *can = cfg->can;
	struct can_gd32_data *data = dev->data;
#ifdef CONFIG_CAN_FD_MODE
	CAN_TxMail_TypeDef *TxMail = NULL;
#endif

	LOG_DBG("Set mode %d", mode);

	if (IS_ENABLED(CONFIG_CAN_MANUAL_RECOVERY_MODE)) {
		supported |= CAN_MODE_MANUAL_RECOVERY;
	}

	if (IS_ENABLED(CONFIG_CAN_FD_MODE)) {
		supported |= CAN_MODE_FD;
	}

	if ((mode & ~supported) != 0) {
		LOG_ERR("unsupported mode: 0x%08x", mode);
		return -ENOTSUP;
	}

	if (data->common.started) {
		return -EBUSY;
	}

	k_mutex_lock(&data->inst_mutex, K_FOREVER);

	if ((mode & CAN_MODE_LOOPBACK) != 0) {
		/* Loopback mode */
		can->CANX_BT |= CAN_BT_LCMOD;
	} else {
		can->CANX_BT &= ~CAN_BT_LCMOD;
	}

	if ((mode & CAN_MODE_LISTENONLY) != 0) {
		/* Silent mode */
		can->CANX_BT |= CAN_BT_SCMOD;
	} else {
		can->CANX_BT &= ~CAN_BT_SCMOD;
	}

	if ((mode & CAN_MODE_ONE_SHOT) != 0) {
		/* No automatic retransmission */
		can->CANX_CTL |= CAN_CTL_ARD;
	} else {
		can->CANX_CTL &= ~CAN_CTL_ARD;
	}

	if (IS_ENABLED(CONFIG_CAN_MANUAL_RECOVERY_MODE)) {
		if ((mode & CAN_MODE_MANUAL_RECOVERY) != 0) {
			can->CANX_CTL &= ~CAN_CTL_ABOR;
		} else {
			can->CANX_CTL |= CAN_CTL_ABOR;
		}
	} else {
		can->CANX_CTL |= CAN_CTL_ABOR;
	}

#ifdef CONFIG_CAN_FD_MODE
	/* CANFD mode, open the baud rate switch*/
	if ((mode & CAN_MODE_FD) != 0) {
		can->CANX_FDCTL |= CAN_FDCTL_FDEN;
		TxMail = &can->TXMAIL[0];
		TxMail->CANX_TMP |= CAN_TMP_BRS;
		TxMail = &can->TXMAIL[1];
		TxMail->CANX_TMP |= CAN_TMP_BRS;
		TxMail = &can->TXMAIL[2];
		TxMail->CANX_TMP |= CAN_TMP_BRS;
	} else {
		can->CANX_FDCTL &= ~CAN_FDCTL_FDEN;
		TxMail = &can->TXMAIL[0];
		TxMail->CANX_TMP &= ~CAN_TMP_BRS;
		TxMail = &can->TXMAIL[1];
		TxMail->CANX_TMP &= ~CAN_TMP_BRS;
		TxMail = &can->TXMAIL[2];
		TxMail->CANX_TMP &= ~CAN_TMP_BRS;
	}
#endif /* CONFIG_CAN_FD_MODE */
	data->common.mode = mode;
	k_mutex_unlock(&data->inst_mutex);

	return 0;
}

static int can_gd32_timing_configure(const struct device *dev, const struct can_timing *timing,
				     const struct can_timing *fd_timing)
{
	const struct can_gd32_config *cfg = dev->config;
	GD_CAN_TypeDef *can = cfg->can;
	struct can_gd32_data *data = dev->data;

#ifdef CONFIG_CAN_FD_MODE
	uint32_t fdctl_status;
#endif

	k_mutex_lock(&data->inst_mutex, K_FOREVER);

	if (data->common.started) {
		k_mutex_unlock(&data->inst_mutex);
		return -EBUSY;
	}
	if (timing) {
		can->CANX_BT = (can->CANX_BT & ~(CAN_GD32_BT_TIMING_MASK));
		can->CANX_BT |= BT_BS1((uint32_t)timing->phase_seg1 - 1) |
				BT_BS2((uint32_t)timing->phase_seg2 - 1) |
				BT_BAUDPSC((uint32_t)timing->prescaler - 1);
		can->CANX_BT = (can->CANX_BT & ~CAN_BT_SJW);
		can->CANX_BT |= BT_SJW((uint32_t)(timing->sjw - 1));
	}
#ifdef CONFIG_CAN_FD_MODE
	if (fd_timing) {
		/* get the current mode whether CANFD mode */
		fdctl_status = can->CANX_FDCTL;
		if (timing) {
			if (CAN_FDCTL_FDEN != (fdctl_status & CAN_FDCTL_FDEN)) {
				/* CAN FD disable, should first enable, then write */
				fdctl_status = fdctl_status | CAN_FDCTL_FDEN;
				can->CANX_FDCTL = fdctl_status;
				can->CANX_BT = (can->CANX_BT &
						~(CAN_GD32_BT_TIMING_MASK));
				can->CANX_BT |= BT_BS1((uint32_t)timing->phase_seg1 - 1) |
						BT_BS2((uint32_t)timing->phase_seg2 - 1) |
						BT_BAUDPSC((uint32_t)timing->prescaler - 1);

				can->CANX_BT = (can->CANX_BT & ~CAN_BT_SJW);
				can->CANX_BT |= BT_SJW((uint32_t)timing->sjw - 1);
			} else {
				can->CANX_BT = (can->CANX_BT &
						~(CAN_GD32_BT_TIMING_MASK));
				can->CANX_BT |= BT_BS1((uint32_t)timing->phase_seg1 - 1) |
						BT_BS2((uint32_t)timing->phase_seg2 - 1) |
						BT_BAUDPSC((uint32_t)timing->prescaler - 1);
				can->CANX_BT = (can->CANX_BT & ~CAN_BT_SJW);
				can->CANX_BT |= BT_SJW((uint32_t)timing->sjw - 1);
			}
		}
		can->CANX_DBT = (can->CANX_DBT & ~(CAN_DBT_DBAUDPSC | CAN_DBT_DBS1 | CAN_DBT_DBS2));
		can->CANX_DBT |= BT_DBS1((uint32_t)fd_timing->phase_seg1 - 1) |
				 BT_DBS2((uint32_t)fd_timing->phase_seg2 - 1) |
				 BT_BAUDPSC((uint32_t)fd_timing->prescaler - 1);
		can->CANX_DBT = (can->CANX_DBT & ~CAN_DBT_DSJW);
		can->CANX_DBT |= BT_DSJW((uint32_t)fd_timing->sjw - 1);
	}
#endif
	k_mutex_unlock(&data->inst_mutex);

	return 0;
}

static int can_gd32_set_timing(const struct device *dev, const struct can_timing *timing)
{
	struct can_gd32_data *data = dev->data;

	if (data->common.started) {
		return -EBUSY;
	}

	return can_gd32_timing_configure(dev, timing, NULL);
}

#ifdef CONFIG_CAN_FD_MODE
static int can_gd32_set_timing_data(const struct device *dev, const struct can_timing *timing_data)
{
	struct can_gd32_data *data = dev->data;
	int ret;

	if (data->common.started) {
		return -EBUSY;
	}

	ret = can_gd32_timing_configure(dev, NULL, timing_data);

	return ret;
}
#endif /* CONFIG_CAN_FD_MODE */

static int can_gd32_get_core_clock(const struct device *dev, uint32_t *rate)
{
	const struct can_gd32_config *cfg = dev->config;
	const struct device *clock_dev;
	int ret;

	clock_dev = DEVICE_DT_GET(GD32_CLOCK_CONTROLLER_NODE);

	ret = clock_control_get_rate(clock_dev, (clock_control_subsys_t *)&cfg->pclken, rate);
	if (ret != 0) {
		LOG_ERR("Failed call clock_control_get_rate: return [%d]", ret);
		return -EIO;
	}

	return 0;
}

static int can_gd32_get_max_filters(const struct device *dev, bool ide)
{
	ARG_UNUSED(dev);

	if (ide) {
		return CONFIG_CAN_MAX_EXT_ID_FILTER;
	} else {
		return CONFIG_CAN_MAX_STD_ID_FILTER;
	}
}

static int can_gd32_init(const struct device *dev)
{
	const struct can_gd32_config *cfg = dev->config;
	struct can_gd32_data *data = dev->data;
	GD_CAN_TypeDef *can = cfg->can;
	struct can_timing timing;
	const struct device *clock_dev;
	uint32_t bank_offset;
	int ret;
#ifdef CONFIG_CAN_FD_MODE
	CAN_TxMail_TypeDef *TxMail = NULL;
	struct can_timing fd_timing;
#endif

	k_mutex_init(&filter_mutex);
	k_mutex_init(&data->inst_mutex);
	k_sem_init(&data->tx_int_sem, 0, 1);

	if (cfg->common.phy != NULL) {
		if (!device_is_ready(cfg->common.phy)) {
			LOG_ERR("CAN transceiver not ready");
			return -ENODEV;
		}
	}

	clock_dev = DEVICE_DT_GET(GD32_CLOCK_CONTROLLER_NODE);
	if (!device_is_ready(clock_dev)) {
		LOG_ERR("clock control device not ready");
		return -ENODEV;
	}

	ret = clock_control_on(clock_dev, (clock_control_subsys_t *)&cfg->pclken);
	if (ret != 0) {
		LOG_ERR("HAL_CAN_Init clock control on failed: %d", ret);
		return -EIO;
	}

	/* Configure DT device pins when available */
	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("CAN pinctrl setup failed (%d)", ret);
		return ret;
	}

	/* if CAN1 used, the CAN0 must be enable for configuring filter */
	if ((uint32_t)can == CAN1) {
		rcu_periph_clock_enable(RCU_CAN0);
	}

	ret = can_gd32_exit_sleep_mode(can);
	if (ret) {
		LOG_ERR("Failed to exit sleep mode");
		return ret;
	}

	ret = can_gd32_enter_init_mode(can);
	if (ret) {
		LOG_ERR("Failed to enter init mode");
		return ret;
	}

	/* configure scale of filter banks < CONFIG_CAN_MAX_EXT_ID_FILTER for ext ids */
	bank_offset = (cfg->can == cfg->master_can) ? 0 : CAN_GD32_NUM_FILTER_BANKS;
	cfg->master_can->CANX_FCTL |= CAN_FCTL_FLD;
	cfg->master_can->CANX_FSCFG |= ((1U << CONFIG_CAN_MAX_EXT_ID_FILTER) - 1) << bank_offset;
	cfg->master_can->CANX_FCTL &= ~CAN_FCTL_FLD;

	can->CANX_CTL &= (~CAN_CTL_TFO) & (~CAN_CTL_RFOD) & (~CAN_CTL_ARD) & (~CAN_CTL_AWU) &
			 (~CAN_CTL_ABOR) & (~CAN_CTL_TTC);
#ifdef CONFIG_CAN_RX_TIMESTAMP
	can->CANX_CTL |= CAN_CTL_TTC;
#endif
#ifdef CONFIG_CAN_FD_MODE
	/* clear the CANFD mode */
	can->CANX_FDCTL &= ~CAN_FDCTL_FDEN;
	TxMail = &can->TXMAIL[0];
	TxMail->CANX_TMP &= ~CAN_TMP_BRS;
	TxMail = &can->TXMAIL[1];
	TxMail->CANX_TMP &= ~CAN_TMP_BRS;
	TxMail = &can->TXMAIL[2];
	TxMail->CANX_TMP &= ~CAN_TMP_BRS;
	if (cfg->tx_delay_comp_offset != 0U) {
		can->CANX_FDCTL |= CAN_FDCTL_TDCEN;
		can->CANX_FDTDC = ((uint32_t)cfg->tx_delay_comp_offset << 8U);
	} else {
		can->CANX_FDCTL &= ~CAN_FDCTL_TDCEN;
		can->CANX_FDTDC = 0;
	}
#endif /* CONFIG_CAN_FD_MODE */
	/* calculate the default sample point */
	timing.sjw = cfg->sjw;
	if (cfg->sample_point && USE_SP_ALGO) {
		ret = can_calc_timing(dev, &timing, cfg->bus_speed, cfg->sample_point);
		if (ret == -EINVAL) {
			LOG_ERR("Can't find timing for given param");
			return -EIO;
		}
		LOG_DBG("Presc: %d, TS1: %d, TS2: %d", timing.prescaler, timing.phase_seg1,
			timing.phase_seg2);
		LOG_DBG("Sample-point err : %d", ret);
	} else {
		timing.prop_seg = 0;
		timing.phase_seg1 = cfg->prop_ts1;
		timing.phase_seg2 = cfg->ts2;
	}
#ifdef CONFIG_CAN_FD_MODE
	/* calculate the default data sample point */
	if (cfg->sample_point_data) {
		ret = can_calc_timing_data(dev, &fd_timing, cfg->bus_speed_data,
					   cfg->sample_point_data);
		if (ret == -EINVAL) {
			LOG_ERR("Can't find timing for given dataphase param");
			return -EIO;
		}

		LOG_DBG("Sample-point err data phase: %d", ret);
	} else if (cfg->prop_ts1_data) {
		fd_timing.prop_seg = 0;
		fd_timing.phase_seg1 = cfg->prop_ts1_data;
		fd_timing.phase_seg2 = cfg->ts2_data;
	}
#endif
#ifdef CONFIG_CAN_FD_MODE
	fd_timing.sjw = cfg->sjw_data;
	ret = can_gd32_timing_configure(dev, &timing, &fd_timing);
#else
	ret = can_gd32_set_timing(dev, &timing);
#endif
	if (ret) {
		return ret;
	}

	ret = can_gd32_set_mode(dev, CAN_MODE_NORMAL);
	if (ret) {
		return ret;
	}

	(void)can_gd32_get_state(dev, &data->state, NULL);

	cfg->config_irq(can);
	can->CANX_INTEN |= CAN_INTEN_TMEIE;

	return 0;
}

static void can_gd32_set_state_change_callback(const struct device *dev,
					       can_state_change_callback_t cb, void *user_data)
{
	struct can_gd32_data *data = dev->data;
	const struct can_gd32_config *cfg = dev->config;
	GD_CAN_TypeDef *can = cfg->can;

	data->common.state_change_cb = cb;
	data->common.state_change_cb_user_data = user_data;

	if (cb == NULL) {
		can->CANX_INTEN &= ~(CAN_INTEN_BOIE | CAN_INTEN_PERRIE | CAN_INTEN_WERRIE);
	} else {
		can->CANX_INTEN |= CAN_INTEN_BOIE | CAN_INTEN_PERRIE | CAN_INTEN_WERRIE;
	}
}

#ifdef CONFIG_CAN_MANUAL_RECOVERY_MODE
static int can_gd32_recover(const struct device *dev, k_timeout_t timeout)
{
	struct can_gd32_data *data = dev->data;
	const struct can_gd32_config *cfg = dev->config;
	GD_CAN_TypeDef *can = cfg->can;
	int ret = -EAGAIN;
	int64_t start_time;

	if (!data->common.started) {
		return -ENETDOWN;
	}

	if ((data->common.mode & CAN_MODE_MANUAL_RECOVERY) == 0U) {
		return -ENOTSUP;
	}

	if (!(can->CANX_ERR & CAN_ERR_BOERR)) {
		return 0;
	}

	if (k_mutex_lock(&data->inst_mutex, K_FOREVER)) {
		return -EAGAIN;
	}
	ret = can_gd32_enter_init_mode(can);
	if (ret) {
		goto done;
	}

	can_gd32_exit_init_mode(can);

	start_time = k_uptime_ticks();

	while (can->CANX_ERR & CAN_ERR_BOERR) {
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

static int can_gd32_send(const struct device *dev, const struct can_frame *frame,
			 k_timeout_t timeout, can_tx_callback_t callback, void *user_data)
{
	const struct can_gd32_config *cfg = dev->config;
	struct can_gd32_data *data = dev->data;
	GD_CAN_TypeDef *can = cfg->can;
	uint32_t tx_status_reg = can->CANX_TSTAT;
	struct can_gd32_mailbox *mb = NULL;
	CAN_TxMail_TypeDef *TxMail = NULL;
	size_t data_length = can_dlc_to_bytes(frame->dlc);
#ifdef CONFIG_CAN_FD_MODE
	volatile uint32_t p_temp;
	uint8_t i = 0U;
#endif /* CONFIG_CAN_FD_MODE */

	LOG_DBG("Sending %d bytes on %s. "
		"Id: 0x%x, "
		"ID type: %s, "
		"Remote Frame: %s",
		data_length, dev->name, frame->id,
		(frame->flags & CAN_FRAME_IDE) != 0 ? "extended" : "standard",
		(frame->flags & CAN_FRAME_RTR) != 0 ? "yes" : "no");

	__ASSERT_NO_MSG(callback != NULL);
	__ASSERT(frame->dlc == 0U || frame->data != NULL, "Dataptr is null");
#ifdef CONFIG_CAN_FD_MODE
	if ((frame->flags & ~(CAN_FRAME_IDE | CAN_FRAME_RTR | CAN_FRAME_FDF | CAN_FRAME_BRS)) !=
	    0) {
		LOG_ERR("unsupported CAN frame flags 0x%02x", frame->flags);
		return -ENOTSUP;
	}

	if ((frame->flags & CAN_FRAME_FDF) != 0 && (can->CANX_FDCTL & CAN_FDCTL_FDEN) == 0) {
		LOG_ERR("CAN-FD format not supported in non-FD mode");
		return -ENOTSUP;
	}

	if ((frame->flags & CAN_FRAME_BRS) != 0 && (can->CANX_FDCTL & CAN_FDCTL_FDEN) == 0) {
		LOG_ERR("CAN-FD BRS not supported in non-FD mode");
		return -ENOTSUP;
	}
#else  /* CONFIG_CAN_FD_MODE */
	if ((frame->flags & ~(CAN_FRAME_IDE | CAN_FRAME_RTR)) != 0) {
		LOG_ERR("unsupported CAN frame flags 0x%02x", frame->flags);
		return -ENOTSUP;
	}
#endif /* !CONFIG_CAN_FD_MODE */

	if (data_length > sizeof(frame->data)) {
		LOG_ERR("data length (%zu) > max frame data length (%zu)", data_length,
			sizeof(frame->data));
		return -EINVAL;
	}

	if ((frame->flags & CAN_FRAME_FDF) != 0) {
		if (frame->dlc > CANFD_MAX_DLC) {
			LOG_ERR("DLC of %d for CAN-FD format frame", frame->dlc);
			return -EINVAL;
		}
	} else {
		if (frame->dlc > CAN_MAX_DLC) {
			LOG_ERR("DLC of %d for non-FD format frame", frame->dlc);
			return -EINVAL;
		}
	}

	if (!data->common.started) {
		return -ENETDOWN;
	}

	if (can->CANX_ERR & CAN_ERR_BOERR) {
		return -ENETUNREACH;
	}

	k_mutex_lock(&data->inst_mutex, K_FOREVER);
	while (!(tx_status_reg & (CAN_TSTAT_TME0 | CAN_TSTAT_TME1 | CAN_TSTAT_TME2))) {
		k_mutex_unlock(&data->inst_mutex);
		LOG_DBG("Transmit buffer full");
		if (k_sem_take(&data->tx_int_sem, timeout)) {
			return -EAGAIN;
		}

		k_mutex_lock(&data->inst_mutex, K_FOREVER);
		tx_status_reg = can->CANX_TSTAT;
	}

	if (tx_status_reg & CAN_TSTAT_TME0) {
		LOG_DBG("Using TX mailbox 0");
		TxMail = &can->TXMAIL[0];
		mb = &(data->mb0);
	} else if (tx_status_reg & CAN_TSTAT_TME1) {
		LOG_DBG("Using TX mailbox 1");
		TxMail = &can->TXMAIL[1];
		mb = &data->mb1;
	} else if (tx_status_reg & CAN_TSTAT_TME2) {
		LOG_DBG("Using TX mailbox 2");
		TxMail = &can->TXMAIL[2];
		mb = &data->mb2;
	}

	mb->tx_callback = callback;
	mb->callback_arg = user_data;

	/* stop the TX mailbox */
	TxMail->CANX_TMI &= CAN_TMI_TEN;

	if ((frame->flags & CAN_FRAME_IDE) != 0) {
		TxMail->CANX_TMI |= (frame->id << 3U) | CAN_TMI_FF;
	} else {
		TxMail->CANX_TMI |= (frame->id << 21U);
	}

	if ((frame->flags & CAN_FRAME_RTR) != 0) {
		TxMail->CANX_TMI |= CAN_TMI_FT;
	}
	TxMail->CANX_TMP &= ~(CAN_TMP_DLENC | CAN_TMP_ESI | CAN_TMP_FDF | CAN_TMP_TSEN);
	TxMail->CANX_TMP |= (frame->dlc & 0xF);

#ifdef CONFIG_CAN_FD_MODE
	if (frame->flags & CAN_FRAME_FDF) {
		TxMail->CANX_TMP |= CAN_TMP_FDF;
		TxMail->CANX_TMP |= CAN_TMP_ESI;

		i = data_length / 4U;
		/* data length is 5-7 need send 2 word */
		if ((i == 1U) && (4U != data_length)) {
			i++;
		}
		p_temp = (uint32_t)frame->data_32;
		if ((i == 0U)) {
			TxMail->CANX_TMDATA0 = *(uint32_t *)p_temp;
		} else {
			for (; i > 0U; i--) {
				TxMail->CANX_TMDATA0 = *(uint32_t *)p_temp;
				p_temp = ((uint32_t)((uint32_t)p_temp + 4U));
			}
		}
	} else {
		TxMail->CANX_TMDATA0 = frame->data_32[0];
		TxMail->CANX_TMDATA1 = frame->data_32[1];
	}
#else
	TxMail->CANX_TMDATA0 = frame->data_32[0];
	TxMail->CANX_TMDATA1 = frame->data_32[1];
#endif
	TxMail->CANX_TMI |= CAN_TMI_TEN;
	k_mutex_unlock(&data->inst_mutex);

	return 0;
}

static void can_gd32_set_filter_bank(int filter_id, CAN_FilterReg_TypeDef *filter_reg, bool ide,
				     uint32_t id, uint32_t mask)
{
	if (ide) {
		filter_reg->CANX_FxDATA0 = id;
		filter_reg->CANX_FxDATA1 = mask;
	} else {
		if ((filter_id - CONFIG_CAN_MAX_EXT_ID_FILTER) % 2 == 0) {
			/* even std filter id: first 1/2 bank */
			filter_reg->CANX_FxDATA0 = id | (mask << 16);
		} else {
			/* uneven std filter id: first 1/2 bank */
			filter_reg->CANX_FxDATA1 = id | (mask << 16);
		}
	}
}

static inline uint32_t can_gd32_filter_to_std_mask(const struct can_filter *filter)
{
	uint32_t rtr_mask =
		(filter->flags & (CAN_FT_DATA | CAN_FT_REMOTE)) != (CAN_FT_DATA | CAN_FT_REMOTE)
			? 1U
			: 0U;

	return (filter->mask << CAN_GD32_FIRX_STD_ID_POS) |
	       (rtr_mask << CAN_GD32_FIRX_STD_RTR_POS) | (1U << CAN_GD32_FIRX_STD_IDE_POS);
}

static inline uint32_t can_gd32_filter_to_ext_mask(const struct can_filter *filter)
{
	uint32_t rtr_mask =
		(filter->flags & (CAN_FT_DATA | CAN_FT_REMOTE)) != (CAN_FT_DATA | CAN_FT_REMOTE)
			? 1U
			: 0U;

	return (filter->mask << CAN_GD32_FIRX_EXT_EXT_ID_POS) |
	       (rtr_mask << CAN_GD32_FIRX_EXT_RTR_POS) | (1U << CAN_GD32_FIRX_EXT_IDE_POS);
}

static inline uint32_t can_gd32_filter_to_std_id(const struct can_filter *filter)
{
	return (filter->id << CAN_GD32_FIRX_STD_ID_POS) |
	       (((filter->flags & CAN_FT_REMOTE) != 0) ? (1U << CAN_GD32_FIRX_STD_RTR_POS) : 0U);
}

static inline uint32_t can_gd32_filter_to_ext_id(const struct can_filter *filter)
{
	return (filter->id << CAN_GD32_FIRX_EXT_EXT_ID_POS) |
	       (((filter->flags & CAN_FT_REMOTE) != 0) ? (1U << CAN_GD32_FIRX_EXT_RTR_POS) : 0U) |
	       (1U << CAN_GD32_FIRX_EXT_IDE_POS);
}

static inline int can_gd32_set_filter(const struct device *dev, const struct can_filter *filter)
{
	const struct can_gd32_config *cfg = dev->config;
	struct can_gd32_data *data = dev->data;
	GD_CAN_TypeDef *can = cfg->master_can;
	uint32_t mask = 0U;
	uint32_t id = 0U;
	int filter_id = -ENOSPC;
	int bank_offset = 0;
	int bank_num;

	if (cfg->can != cfg->master_can) {
		/* CAN0 and CAN1 shares 28 filter banks */
		/* CAN1 has no filter control register */
		bank_offset = CAN_GD32_NUM_FILTER_BANKS;
	}

	if ((filter->flags & CAN_FILTER_IDE) != 0) {
		for (int i = 0; i < CONFIG_CAN_MAX_EXT_ID_FILTER; i++) {
			if (data->rx_cb_ext[i] == NULL) {
				id = can_gd32_filter_to_ext_id(filter);
				mask = can_gd32_filter_to_ext_mask(filter);
				filter_id = i;
				bank_num = bank_offset + i;
				break;
			}
		}
	} else {
		for (int i = 0; i < CONFIG_CAN_MAX_STD_ID_FILTER; i++) {
			if (data->rx_cb_std[i] == NULL) {
				id = can_gd32_filter_to_std_id(filter);
				mask = can_gd32_filter_to_std_mask(filter);
				filter_id = CONFIG_CAN_MAX_EXT_ID_FILTER + i;
				bank_num = bank_offset + CONFIG_CAN_MAX_EXT_ID_FILTER + i / 2;
				break;
			}
		}
	}

	if (filter_id != -ENOSPC) {
		LOG_DBG("Adding filter_id %d, CAN ID: 0x%x, mask: 0x%x", filter_id, filter->id,
			filter->mask);

		/* set the filter init mode */
		can->CANX_FCTL |= CAN_FCTL_FLD;

		can_gd32_set_filter_bank(filter_id, &can->CANX_FilterReg[bank_num],
					 (filter->flags & CAN_FILTER_IDE) != 0, id, mask);

		can->CANX_FW |= 1U << bank_num;
		can->CANX_FCTL &= ~(CAN_FCTL_FLD);
	} else {
		LOG_WRN("No free filter left");
	}

	return filter_id;
}

/*
 * This driver uses masked mode for all filters (CAN_FMMCFG left at reset value
 * 0x00) in order to simplify mapping between filter match index from the FIFOs
 * and array index for the callbacks. All ext ID filters are stored in the
 * banks below CONFIG_CAN_MAX_EXT_ID_FILTER, followed by the std ID filters,
 * which consume only 1/2 bank per filter.
 *
 * The more complicated list mode must be implemented if someone requires more
 * than 28 std ID or 14 ext ID filters.
 *
 * Currently, all filter banks are assigned to FIFO0, and FIFO1 is not used.
 */
static int can_gd32_add_rx_filter(const struct device *dev, can_rx_callback_t cb, void *cb_arg,
				  const struct can_filter *filter)
{
	struct can_gd32_data *data = dev->data;
	int filter_id;

#ifdef CONFIG_CAN_FD_MODE
	if ((filter->flags & ~(CAN_FILTER_IDE | CAN_FT_DATA | CAN_FT_REMOTE | CAN_FDF_FDFRAME)) !=
	    0) {
#else
	if ((filter->flags & ~(CAN_FILTER_IDE | CAN_FT_DATA | CAN_FT_REMOTE)) != 0) {
#endif
		LOG_ERR("unsupported CAN filter flags 0x%02x", filter->flags);
		return -ENOTSUP;
	}

	k_mutex_lock(&filter_mutex, K_FOREVER);
	k_mutex_lock(&data->inst_mutex, K_FOREVER);

	filter_id = can_gd32_set_filter(dev, filter);
	if (filter_id >= 0) {
		if ((filter->flags & CAN_FILTER_IDE) != 0) {
			data->rx_cb_ext[filter_id] = cb;
			data->cb_arg_ext[filter_id] = cb_arg;
		} else {
			data->rx_cb_std[filter_id - CONFIG_CAN_MAX_EXT_ID_FILTER] = cb;
			data->cb_arg_std[filter_id - CONFIG_CAN_MAX_EXT_ID_FILTER] = cb_arg;
		}
	}

	k_mutex_unlock(&data->inst_mutex);
	k_mutex_unlock(&filter_mutex);

	return filter_id;
}

static void can_gd32_remove_rx_filter(const struct device *dev, int filter_id)
{
	const struct can_gd32_config *cfg = dev->config;
	struct can_gd32_data *data = dev->data;
	GD_CAN_TypeDef *can = cfg->master_can;
	bool ide;
	int bank_offset = 0;
	int bank_num;
	bool bank_unused;

	__ASSERT_NO_MSG(filter_id >= 0 && filter_id < CAN_GD32_MAX_FILTER_ID);

	k_mutex_lock(&filter_mutex, K_FOREVER);
	k_mutex_lock(&data->inst_mutex, K_FOREVER);

	if (cfg->can != cfg->master_can) {
		bank_offset = CAN_GD32_NUM_FILTER_BANKS;
	}

	if (filter_id < CONFIG_CAN_MAX_EXT_ID_FILTER) {
		ide = true;
		bank_num = bank_offset + filter_id;

		data->rx_cb_ext[filter_id] = NULL;
		data->cb_arg_ext[filter_id] = NULL;

		bank_unused = true;
	} else {
		int filter_index = filter_id - CONFIG_CAN_MAX_EXT_ID_FILTER;

		ide = false;
		bank_num = bank_offset + CONFIG_CAN_MAX_EXT_ID_FILTER +
			   (filter_id - CONFIG_CAN_MAX_EXT_ID_FILTER) / 2;

		data->rx_cb_std[filter_index] = NULL;
		data->cb_arg_std[filter_index] = NULL;

		if (filter_index % 2 == 1) {
			bank_unused = data->rx_cb_std[filter_index - 1] == NULL;
		} else if (filter_index + 1 < CONFIG_CAN_MAX_STD_ID_FILTER) {
			bank_unused = data->rx_cb_std[filter_index + 1] == NULL;
		} else {
			bank_unused = true;
		}
	}

	LOG_DBG("Removing filter_id %d, ide %d", filter_id, ide);

	can->CANX_FCTL |= CAN_FCTL_FLD;

	can_gd32_set_filter_bank(filter_id, &can->CANX_FilterReg[bank_num], ide, 0, 0xFFFFFFFF);

	if (bank_unused) {
		can->CANX_FW &= ~(1U << bank_num);
		LOG_DBG("Filter bank %d is unused -> deactivate", bank_num);
	}

	can->CANX_FCTL &= ~(CAN_FCTL_FLD);

	k_mutex_unlock(&data->inst_mutex);
	k_mutex_unlock(&filter_mutex);
}

static DEVICE_API(can, can_api_funcs) = {
	/* Get CAN work mode. */
	.get_capabilities = can_gd32_get_capabilities,
	/* Leave initialization mode. */
	.start = can_gd32_start,
	/* Stop CAN transmission. */
	.stop = can_gd32_stop,
	/* Set loopback, silent, and retransmission related modes. */
	.set_mode = can_gd32_set_mode,
	/* Configure CAN/FDCAN baud rate. */
	.set_timing = can_gd32_set_timing,
	/* Send CAN/FDCAN data. */
	.send = can_gd32_send,
	.add_rx_filter = can_gd32_add_rx_filter,
	.remove_rx_filter = can_gd32_remove_rx_filter,
	/* Get CAN error state. */
	.get_state = can_gd32_get_state,
#ifdef CONFIG_CAN_MANUAL_RECOVERY_MODE
	/* Recover CAN bus manually. */
	.recover = can_gd32_recover,
#endif
	.set_state_change_callback = can_gd32_set_state_change_callback,
	.get_core_clock = can_gd32_get_core_clock,
	.get_max_filters = can_gd32_get_max_filters,
	.timing_min = {.sjw = 0x1,
		       .prop_seg = 0x00,
		       .phase_seg1 = 0x01,
		       .phase_seg2 = 0x01,
		       .prescaler = 0x01},
	.timing_max = {.sjw = 0x20,
		       .prop_seg = 0x00,
		       .phase_seg1 = 0x80,
		       .phase_seg2 = 0x20,
		       .prescaler = 0x400},
#ifdef CONFIG_CAN_FD_MODE
	.set_timing_data = can_gd32_set_timing_data,
	/* Data timing limits for GD32C11X */
	.timing_data_min = {.sjw = 0x01,
			    .prop_seg = 0x00,
			    .phase_seg1 = 0x01,
			    .phase_seg2 = 0x01,
			    .prescaler = 0x01},
	.timing_data_max = {.sjw = 0x08,
			    .prop_seg = 0x00,
			    .phase_seg1 = 0x10,
			    .phase_seg2 = 0x08,
			    .prescaler = 0x400}
#endif
};

#define CAN_GD32_IRQ_INST(inst)                                                                    \
	static void config_can_##inst##_irq(GD_CAN_TypeDef *can)                                   \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, rx0, irq),                                   \
			    DT_INST_IRQ_BY_NAME(inst, rx0, priority), can_gd32_rx_isr,             \
			    DEVICE_DT_INST_GET(inst), 0);                                          \
		irq_enable(DT_INST_IRQ_BY_NAME(inst, rx0, irq));                                   \
		IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, tx, irq),                                    \
			    DT_INST_IRQ_BY_NAME(inst, tx, priority), can_gd32_tx_isr,              \
			    DEVICE_DT_INST_GET(inst), 0);                                          \
		irq_enable(DT_INST_IRQ_BY_NAME(inst, tx, irq));                                    \
		IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, sce, irq),                                   \
			    DT_INST_IRQ_BY_NAME(inst, sce, priority), can_gd32_state_change_isr,   \
			    DEVICE_DT_INST_GET(inst), 0);                                          \
		irq_enable(DT_INST_IRQ_BY_NAME(inst, sce, irq));                                   \
		can->CANX_INTEN |= CAN_INTEN_TMEIE | CAN_INTEN_RFNEIE0 | CAN_INTEN_ERRIE |         \
				   CAN_INTEN_RFNEIE1 | CAN_INTEN_BOIE;                             \
		if (IS_ENABLED(CONFIG_CAN_STATS)) {                                                \
			can->CANX_INTEN |= CAN_INTEN_ERRNIE;                                       \
		}                                                                                  \
	}

#define CAN_GD32_DT_INST_BITRATE(inst) \
	DT_INST_PROP_OR(inst, bitrate, DT_INST_PROP_OR(inst, bus_speed, CONFIG_CAN_DEFAULT_BITRATE))

#define CAN_GD32_DT_INST_BITRATE_DATA(inst) \
	DT_INST_PROP_OR(inst, bitrate_data, DT_INST_PROP_OR(inst, bus_speed_data, 0))

#define CAN_GD32_CONFIG_INST(inst)                                                                 \
	PINCTRL_DT_INST_DEFINE(inst);                                                              \
	static const struct can_gd32_config can_gd32_cfg_##inst = {                                \
		.common = CAN_DT_DRIVER_CONFIG_INST_GET(inst, 0, 6000000),                         \
		.can = (GD_CAN_TypeDef *)DT_INST_REG_ADDR(inst),                                   \
		.master_can = (GD_CAN_TypeDef *)DT_INST_PROP_OR(inst, master_can_reg,              \
								DT_INST_REG_ADDR(inst)),           \
		.bus_speed = CAN_GD32_DT_INST_BITRATE(inst),                             \
		.sample_point = DT_INST_PROP_OR(inst, sample_point, 0),                            \
		.sjw = DT_INST_PROP_OR(inst, sjw, 1),                                              \
		.prop_ts1 =                                                                        \
			DT_INST_PROP_OR(inst, prop_seg, 0) + DT_INST_PROP_OR(inst, phase_seg1, 0), \
		.ts2 = DT_INST_PROP_OR(inst, phase_seg2, 0),                                       \
		.bus_speed_data = CAN_GD32_DT_INST_BITRATE_DATA(inst),                   \
		.sample_point_data = DT_INST_PROP_OR(inst, sample_point_data, 0),                  \
		.sjw_data = DT_INST_PROP_OR(inst, sjw_data, 1),                                    \
		.prop_ts1_data = DT_INST_PROP_OR(inst, prop_seg_data, 0) +                         \
				 DT_INST_PROP_OR(inst, phase_seg1_data, 0),                        \
		.ts2_data = DT_INST_PROP_OR(inst, phase_seg2_data, 0),                             \
		.pclken =                                                                          \
			{                                                                          \
				.enr = DT_INST_CLOCKS_CELL(inst, id),                              \
				.bus = DT_INST_CLOCKS_CELL(inst, id),                              \
			},                                                                         \
		.config_irq = config_can_##inst##_irq,                                             \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                                      \
		.tx_delay_comp_offset = DT_INST_PROP_OR(inst, tx_delay_comp_offset, 0),            \
	};

#define CAN_GD32_DATA_INST(inst) static struct can_gd32_data can_gd32_dev_data_##inst;

#define CAN_GD32_DEFINE_INST(inst)                                                                 \
	CAN_DEVICE_DT_INST_DEFINE(inst, can_gd32_init, NULL,                                      \
				  &can_gd32_dev_data_##inst, &can_gd32_cfg_##inst,                 \
				  POST_KERNEL, CONFIG_CAN_INIT_PRIORITY,                           \
				  &can_api_funcs);

#define CAN_GD32_INST(inst)                                                                        \
	CAN_GD32_IRQ_INST(inst)                                                                    \
	CAN_GD32_CONFIG_INST(inst)                                                                 \
	CAN_GD32_DATA_INST(inst)                                                                   \
	CAN_GD32_DEFINE_INST(inst)

DT_INST_FOREACH_STATUS_OKAY(CAN_GD32_INST)
