/*
 * Copyright (c) 2026, GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT gd_gd32_ethernet

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/gd32.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/phy.h>
#include <ethernet/eth_stats.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/crc.h>

#include <gd32_enet.h>
#include <gd32_syscfg.h>
#include <gd32_rcu.h>

#define SYSCFG_NODE DT_NODELABEL(syscfg)

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(eth_gd32, CONFIG_ETHERNET_LOG_LEVEL);

/*
 * HAL owns the descriptor rings/buffers (gd32<series>_enet.c); these
 * globals are its "current descriptor" cursors, read via
 * enet_desc_information_get() for zero-copy TX/RX.
 */
extern enet_descriptors_struct *dma_current_txdesc;
extern enet_descriptors_struct *dma_current_rxdesc;
extern enet_descriptors_struct rxdesc_tab[ENET_RXBUF_NUM];
extern enet_descriptors_struct txdesc_tab[ENET_TXBUF_NUM];

/*
 * GD32H7 HAL takes a per-instance base address; other series have a
 * single fixed MAC and take none. These wrappers hide the difference.
 * ARG_UNUSED(base) on non-H7 keeps cfg->base evaluated at every call site.
 */
#if defined(CONFIG_SOC_SERIES_GD32H7XX)
#define GD32_ENET_MAC_CFG(base)                     ENET_MAC_CFG(base)
#define gd32_enet_software_reset(base)              enet_software_reset(base)
#define gd32_enet_init(base, mode, chksum, filt)    enet_init((base), (mode), (chksum), (filt))
#define gd32_enet_enable(base)                      enet_enable(base)
#define gd32_enet_disable(base)                     enet_disable(base)
#define gd32_enet_interrupt_enable(base, flag)      enet_interrupt_enable((base), (flag))
#define gd32_enet_interrupt_flag_get(base, flag)    enet_interrupt_flag_get((base), (flag))
#define gd32_enet_interrupt_flag_clear(base, flag)  enet_interrupt_flag_clear((base), (flag))
#define gd32_enet_mac_address_set(base, addr, p)    enet_mac_address_set((base), (addr), (p))
#define gd32_enet_descriptors_chain_init(base, dir) enet_descriptors_chain_init((base), (dir))
#define gd32_enet_desc_info_get(base, desc, info) enet_desc_information_get((base), (desc), (info))
#define gd32_enet_nocopy_rx(base)                 ENET_NOCOPY_FRAME_RECEIVE(base)
#define gd32_enet_nocopy_tx(base, len)            ENET_NOCOPY_FRAME_TRANSMIT((base), (len))
#define gd32_enet_dma_resume(base, dir)           enet_dmaprocess_resume((base), (dir))
#define gd32_syscfg_enet_phy_cfg(base, mode)      syscfg_enet_phy_interface_config((base), (mode))
#else
#define GD32_ENET_MAC_CFG(base)        ENET_MAC_CFG
#define gd32_enet_software_reset(base) (ARG_UNUSED(base), enet_software_reset())
#define gd32_enet_init(base, mode, chksum, filt)                                                   \
	(ARG_UNUSED(base), enet_init((mode), (chksum), (filt)))
#define gd32_enet_enable(base)                   (ARG_UNUSED(base), enet_enable())
#define gd32_enet_disable(base)                  (ARG_UNUSED(base), enet_disable())
#define gd32_enet_interrupt_enable(base, flag)   (ARG_UNUSED(base), enet_interrupt_enable(flag))
#define gd32_enet_interrupt_flag_get(base, flag) (ARG_UNUSED(base), enet_interrupt_flag_get(flag))
#define gd32_enet_interrupt_flag_clear(base, flag)                                                 \
	(ARG_UNUSED(base), enet_interrupt_flag_clear(flag))
#define gd32_enet_mac_address_set(base, addr, p)                                                   \
	(ARG_UNUSED(base), enet_mac_address_set((addr), (p)))
#define gd32_enet_descriptors_chain_init(base, dir)                                                \
	(ARG_UNUSED(base), enet_descriptors_chain_init(dir))
#define gd32_enet_desc_info_get(base, desc, info)                                                  \
	(ARG_UNUSED(base), enet_desc_information_get((desc), (info)))
#define gd32_enet_nocopy_rx(base)       (ARG_UNUSED(base), ENET_NOCOPY_FRAME_RECEIVE())
#define gd32_enet_nocopy_tx(base, len)  (ARG_UNUSED(base), ENET_NOCOPY_FRAME_TRANSMIT(len))
#define gd32_enet_dma_resume(base, dir) (ARG_UNUSED(base), enet_dmaprocess_resume(dir))
#define gd32_syscfg_enet_phy_cfg(base, mode)                                                       \
	(ARG_UNUSED(base), syscfg_enet_phy_interface_config(mode))
#endif

#define ETH_GD32_MTU NET_ETH_MTU

/* Bounded wait (microseconds) for the DMA to release the current TX
 * descriptor.
 */
#define ETH_GD32_TX_DESC_WAIT_US 2000U

struct eth_gd32_config {
	uintptr_t base;
	uint16_t clkid[3];
	struct reset_dt_spec reset;
	const struct pinctrl_dev_config *pcfg;
	const struct device *phy_dev;
	void (*irq_config_func)(void);
	bool rmii;
};

struct eth_gd32_data {
	struct net_if *iface;
	uint8_t mac_addr[6];
	struct k_sem rx_sem;
	struct k_mutex tx_mutex;
	struct k_thread rx_thread;

	K_KERNEL_STACK_MEMBER(rx_thread_stack, CONFIG_ETH_GD32_RX_THREAD_STACK_SIZE);

#if defined(CONFIG_NET_STATISTICS_ETHERNET)
	struct net_stats_eth stats;
#endif
};

/*
 * Derive a stable, per-chip locally administered MAC address from the SoC
 * unique ID when devicetree does not provide local-mac-address, so boards do
 * not all boot with the same hardcoded address.
 */
static void eth_gd32_default_mac(uint8_t mac_addr[6])
{
	uint8_t uid[12];
	uint32_t hash = 0U;
	ssize_t ret;

	ret = hwinfo_get_device_id(uid, sizeof(uid));
	if (ret > 0) {
		hash = crc32_ieee(uid, (size_t)ret);
	}

	/* Locally administered (bit 1 set), unicast (bit 0 clear). */
	mac_addr[0] = 0x02U;
	mac_addr[1] = 0x00U;
	mac_addr[2] = 0x00U;
	mac_addr[3] = (hash >> 16) & 0xffU;
	mac_addr[4] = (hash >> 8) & 0xffU;
	mac_addr[5] = hash & 0xffU;
}

static void eth_gd32_isr(const struct device *dev)
{
	const struct eth_gd32_config *cfg = dev->config;
	struct eth_gd32_data *data = dev->data;

	if (gd32_enet_interrupt_flag_get(cfg->base, ENET_DMA_INT_FLAG_RS) == SET) {
		k_sem_give(&data->rx_sem);
	}

	gd32_enet_interrupt_flag_clear(cfg->base, ENET_DMA_INT_FLAG_RS_CLR);
	gd32_enet_interrupt_flag_clear(cfg->base, ENET_DMA_INT_FLAG_NI_CLR);

	if (gd32_enet_interrupt_flag_get(cfg->base, ENET_DMA_INT_FLAG_AI) == SET) {
#if defined(CONFIG_NET_STATISTICS_ETHERNET)
		eth_stats_update_errors_rx(data->iface);
#endif
		gd32_enet_interrupt_flag_clear(cfg->base, ENET_DMA_INT_FLAG_AI_CLR);
	}
}

static void eth_gd32_rx(const struct device *dev)
{
	const struct eth_gd32_config *cfg = dev->config;
	struct eth_gd32_data *data = dev->data;
	uint32_t len;
	uint8_t *buffer;
	struct net_pkt *pkt = NULL;

	/* Process received frames while descriptor is owned by CPU. */
	while ((dma_current_rxdesc->status & ENET_RDES0_DAV) == 0U) {
		unsigned int key;

		/* Protect descriptor ops from ISR interleaving. */
		key = irq_lock();

		len = gd32_enet_desc_info_get(cfg->base, dma_current_rxdesc, RXDESC_FRAME_LENGTH);

		if (len > 0U) {
			buffer = (uint8_t *)gd32_enet_desc_info_get(cfg->base, dma_current_rxdesc,
								    RXDESC_BUFFER_1_ADDR);

			pkt = net_pkt_rx_alloc_with_buffer(data->iface, len, AF_UNSPEC, 0,
							   K_NO_WAIT);
			if (pkt == NULL) {
				LOG_ERR("Failed to allocate RX packet (len=%u)", len);
			} else if (net_pkt_write(pkt, buffer, len) < 0) {
				LOG_ERR("Failed to write RX packet");
				net_pkt_unref(pkt);
				pkt = NULL;
			}
		} else {
			pkt = NULL;
		}

		/* Always release descriptor to DMA, even for zero-length frames. */
		gd32_enet_nocopy_rx(cfg->base);

		irq_unlock(key);

		if (pkt != NULL) {
			if (net_recv_data(data->iface, pkt) < 0) {
				LOG_ERR("Failed to enqueue RX packet");
#if defined(CONFIG_NET_STATISTICS_ETHERNET)
				eth_stats_update_errors_rx(data->iface);
#endif
				net_pkt_unref(pkt);
			}
		} else if (len > 0U) {
			/* Only count real alloc/write failures, not zero-length skips. */
#if defined(CONFIG_NET_STATISTICS_ETHERNET)
			eth_stats_update_errors_rx(data->iface);
#endif
		}
	}
}

static void eth_gd32_rx_thread(void *arg1, void *arg2, void *arg3)
{
	const struct device *dev = (const struct device *)arg1;
	struct eth_gd32_data *data = dev->data;
	int res;

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (1) {
		res = k_sem_take(&data->rx_sem, K_FOREVER);
		if (res == 0) {
			eth_gd32_rx(dev);
		}
	}
}

static int eth_gd32_send(const struct device *dev, struct net_pkt *pkt)
{
	const struct eth_gd32_config *cfg = dev->config;
	struct eth_gd32_data *data = dev->data;
	size_t len = net_pkt_get_len(pkt);
	unsigned int retries = ETH_GD32_TX_DESC_WAIT_US;
	uint8_t *buffer;
	int ret = 0;

	if (len > ENET_MAX_FRAME_SIZE) {
		LOG_ERR("TX frame too large (%zu bytes)", len);
		return -EIO;
	}

	k_mutex_lock(&data->tx_mutex, K_FOREVER);

	/* Protect TX descriptor ops from ISR interleaving. */
	unsigned int key = irq_lock();

	/* Wait for DMA to release current TX descriptor. */
	while (dma_current_txdesc->status & ENET_TDES0_DAV) {
		if (retries-- == 4U) {
			LOG_ERR("TX descriptor busy (DMA stalled?)");
			ret = -EBUSY;
			goto out;
		}
		k_busy_wait(1);
	}

	buffer = (uint8_t *)gd32_enet_desc_info_get(cfg->base, dma_current_txdesc,
						    TXDESC_BUFFER_1_ADDR);

	net_pkt_cursor_init(pkt);

	if (net_pkt_read(pkt, buffer, len) < 0) {
		ret = -EIO;

		goto out;
	}

	if (gd32_enet_nocopy_tx(cfg->base, len) != SUCCESS) {
		ret = -EIO;
	}

out:
	irq_unlock(key);
	k_mutex_unlock(&data->tx_mutex);
#if defined(CONFIG_NET_STATISTICS_ETHERNET)
	if (ret != 0) {
		eth_stats_update_errors_tx(data->iface);
	}
#else
	ARG_UNUSED(data);
#endif
	return ret;
}

static enum ethernet_hw_caps eth_gd32_get_capabilities(const struct device *dev,
						       struct net_if *iface)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(iface);

	return ETHERNET_LINK_10BASE | ETHERNET_LINK_100BASE
#if defined(CONFIG_ETH_GD32_HW_CHECKSUM)
	       | ETHERNET_HW_RX_CHKSUM_OFFLOAD | ETHERNET_HW_TX_CHKSUM_OFFLOAD
#endif
		;
}

static const struct device *eth_gd32_get_phy(const struct device *dev, struct net_if *iface)
{
	const struct eth_gd32_config *cfg = dev->config;

	ARG_UNUSED(iface);

	return cfg->phy_dev;
}

static void eth_gd32_config_mac_link(uintptr_t base, struct phy_link_state *state)
{
	uint32_t reg_val = GD32_ENET_MAC_CFG(base);

	/* Match the MAC speed/duplex to the PHY-negotiated link. */
	reg_val &= ~(ENET_MAC_CFG_SPD | ENET_MAC_CFG_DPM | ENET_MAC_CFG_LBM);

	if (PHY_LINK_IS_SPEED_100M(state->speed)) {
		reg_val |= ENET_SPEEDMODE_100M;
	}

	if (PHY_LINK_IS_FULL_DUPLEX(state->speed)) {
		reg_val |= ENET_MODE_FULLDUPLEX;
	}

	GD32_ENET_MAC_CFG(base) = reg_val;
}

static void phy_link_state_changed(const struct device *phy_dev, struct phy_link_state *state,
				   void *user_data)
{
	const struct device *dev = (const struct device *)user_data;
	const struct eth_gd32_config *cfg = dev->config;
	struct eth_gd32_data *data = dev->data;

	ARG_UNUSED(phy_dev);

	/*
	 * The MAC must be stopped before its speed/duplex config is changed,
	 * and the speed can change without a link-down callback in between, so
	 * always stop first and reconfigure on link up.
	 */
	gd32_enet_disable(cfg->base);

	if (state->is_up) {
		eth_gd32_config_mac_link(cfg->base, state);
		gd32_enet_enable(cfg->base);
		net_eth_carrier_on(data->iface);
	} else {
		net_eth_carrier_off(data->iface);
	}
}

static void eth_gd32_iface_init(struct net_if *iface)
{
	const struct device *dev = net_if_get_device(iface);
	struct eth_gd32_data *data = dev->data;
	const struct eth_gd32_config *cfg = dev->config;

	data->iface = iface;

	net_if_set_link_addr(iface, data->mac_addr, sizeof(data->mac_addr), NET_LINK_ETHERNET);

	ethernet_init(iface);

	net_if_carrier_off(iface);

	if (device_is_ready(cfg->phy_dev)) {
		phy_link_callback_set(cfg->phy_dev, phy_link_state_changed, (void *)dev);
	} else {
		LOG_ERR("PHY device not ready");
	}

	k_thread_create(&data->rx_thread, data->rx_thread_stack,
			K_KERNEL_STACK_SIZEOF(data->rx_thread_stack), eth_gd32_rx_thread,
			(void *)dev, NULL, NULL, K_PRIO_COOP(CONFIG_ETH_GD32_RX_THREAD_PRIO), 0,
			K_NO_WAIT);
	k_thread_name_set(&data->rx_thread, "eth_gd32_rx");
}

static int eth_gd32_init(const struct device *dev)
{
	const struct eth_gd32_config *cfg = dev->config;
	struct eth_gd32_data *data = dev->data;
	uint16_t syscfg_clkid = DT_CLOCKS_CELL(SYSCFG_NODE, id);
	int ret;
	size_t i;

	for (i = 0U; i < ARRAY_SIZE(cfg->clkid); i++) {
		ret = clock_control_on(GD32_CLOCK_CONTROLLER,
				       (clock_control_subsys_t)&cfg->clkid[i]);
		if (ret < 0) {
			LOG_ERR("Failed to enable ENET clock %u (%d)", i, ret);
			return ret;
		}
	}

	ret = clock_control_on(GD32_CLOCK_CONTROLLER, (clock_control_subsys_t)&syscfg_clkid);
	if (ret < 0) {
		LOG_ERR("Failed to enable SYSCFG clock (%d)", ret);
		return ret;
	}

	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("Failed to apply ENET pinctrl (%d)", ret);
		return ret;
	}

	(void)reset_line_toggle_dt(&cfg->reset);

	if (cfg->rmii) {
		gd32_syscfg_enet_phy_cfg(cfg->base, SYSCFG_ENET_PHY_RMII);
	} else {
		gd32_syscfg_enet_phy_cfg(cfg->base, SYSCFG_ENET_PHY_MII);
	}

	if (gd32_enet_software_reset(cfg->base) == ERROR) {
		LOG_ERR("ENET software reset failed");
		return -EIO;
	}

	/*
	 * Initialize MAC/DMA with a fixed default mode. Link state and speed/duplex
	 * are handled by Zephyr PHY: phy_link_state_changed() updates and enables
	 * the MAC when link is up.
	 */
	if (gd32_enet_init(cfg->base, ENET_100M_FULLDUPLEX,
			   IS_ENABLED(CONFIG_ETH_GD32_HW_CHECKSUM)
				   ? ENET_AUTOCHECKSUM_DROP_FAILFRAMES
				   : ENET_NO_AUTOCHECKSUM,
			   ENET_BROADCAST_FRAMES_PASS) == ERROR) {
		LOG_ERR("ENET init failed (PHY not responding?)");
		return -EIO;
	}

	if ((data->mac_addr[0] | data->mac_addr[1] | data->mac_addr[2] | data->mac_addr[3] |
	     data->mac_addr[4] | data->mac_addr[5]) == 0U) {
		eth_gd32_default_mac(data->mac_addr);
	}

	gd32_enet_mac_address_set(cfg->base, ENET_MAC_ADDRESS0, data->mac_addr);

	gd32_enet_descriptors_chain_init(cfg->base, ENET_DMA_TX);
	gd32_enet_descriptors_chain_init(cfg->base, ENET_DMA_RX);

	for (i = 0U; i < ENET_RXBUF_NUM; i++) {
		enet_rx_desc_immediate_receive_complete_interrupt(&rxdesc_tab[i]);
	}

	/* enable the TCP, UDP and ICMP checksum insertion for the Tx frames */
#if defined(CONFIG_ETH_GD32_HW_CHECKSUM)
	for (i = 0U; i < ENET_TXBUF_NUM; i++) {
		if (enet_transmit_checksum_config(&txdesc_tab[i], ENET_CHECKSUM_TCPUDPICMP_FULL) !=
		    SUCCESS) {
			LOG_ERR("Failed to enable TX checksum offload on descriptor %u", i);
			return -EIO;
		}
	}
#endif

	k_sem_init(&data->rx_sem, 0, K_SEM_MAX_LIMIT);
	k_mutex_init(&data->tx_mutex);

	cfg->irq_config_func();

	gd32_enet_interrupt_enable(cfg->base, ENET_DMA_INT_NIE);
	gd32_enet_interrupt_enable(cfg->base, ENET_DMA_INT_RIE);

	/* The MAC/DMA are enabled by phy_link_state_changed() on link up. */

	return 0;
}

static const struct ethernet_api eth_gd32_api = {
	.iface_api.init = eth_gd32_iface_init,
	.get_capabilities = eth_gd32_get_capabilities,
	.get_phy = eth_gd32_get_phy,
	.send = eth_gd32_send,
};

#define ETH_GD32_IRQ_CONFIG_FUNC(n)                                                                \
	static void eth_gd32_irq_config_func_##n(void)                                             \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), eth_gd32_isr,               \
			    DEVICE_DT_INST_GET(n), 0);                                             \
		irq_enable(DT_INST_IRQN(n));                                                       \
	}

#define ETH_GD32_INIT(n)                                                                           \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	ETH_GD32_IRQ_CONFIG_FUNC(n)                                                                \
	BUILD_ASSERT(DT_INST_ENUM_HAS_VALUE(n, phy_connection_type, mii) ||                        \
			     DT_INST_ENUM_HAS_VALUE(n, phy_connection_type, rmii),                 \
		     "Unsupported PHY connection type");                                           \
	static const struct eth_gd32_config eth_gd32_config_##n = {                                \
		.base = DT_INST_REG_ADDR(n),                                                       \
		.clkid = {DT_INST_CLOCKS_CELL_BY_IDX(n, 0, id),                                    \
			  DT_INST_CLOCKS_CELL_BY_IDX(n, 1, id),                                    \
			  DT_INST_CLOCKS_CELL_BY_IDX(n, 2, id)},                                   \
		.reset = RESET_DT_SPEC_INST_GET(n),                                                \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                         \
		.phy_dev = DEVICE_DT_GET(DT_INST_PHANDLE(n, phy_handle)),                          \
		.irq_config_func = eth_gd32_irq_config_func_##n,                                   \
		.rmii = DT_INST_ENUM_HAS_VALUE(n, phy_connection_type, rmii),                      \
	};                                                                                         \
	static struct eth_gd32_data eth_gd32_data_##n = {                                          \
		.mac_addr = DT_INST_PROP_OR(n, local_mac_address, {0}),                            \
	};                                                                                         \
	ETH_NET_DEVICE_DT_INST_DEFINE(n, eth_gd32_init, NULL, &eth_gd32_data_##n,                  \
				      &eth_gd32_config_##n, CONFIG_ETH_INIT_PRIORITY,              \
				      &eth_gd32_api, ETH_GD32_MTU);

DT_INST_FOREACH_STATUS_OKAY(ETH_GD32_INIT)
