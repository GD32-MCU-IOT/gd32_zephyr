/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT gd_gd32_enet_mdio

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/mdio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/gd32.h>

#include <gd32_enet.h>
#include <gd32_syscfg.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(mdio_gd32_enet, CONFIG_MDIO_LOG_LEVEL);

#define SYSCFG_NODE DT_NODELABEL(syscfg)

/*
 * MDIO is embedded in the MAC's own registers, accessed via
 * enet_phy_write_read().
 */
#if defined(CONFIG_SOC_SERIES_GD32H73X_75X)
#define gd32_enet_phy_write_read(base, direction, addr, reg, pval)                                 \
	enet_phy_write_read((base), (direction), (addr), (reg), (pval))
#define gd32_syscfg_enet_phy_cfg(base, mode) syscfg_enet_phy_interface_config((base), (mode))
#define gd32_enet_deinit(base)               enet_deinit(base)
#define gd32_enet_software_reset(base)       enet_software_reset(base)
#define gd32_enet_mdc_config(base)           enet_phy_config(base)
#else
#define gd32_enet_phy_write_read(base, direction, addr, reg, pval)                                 \
	(ARG_UNUSED(base), enet_phy_write_read((direction), (addr), (reg), (pval)))
#define gd32_syscfg_enet_phy_cfg(base, mode)                                                       \
	(ARG_UNUSED(base), syscfg_enet_phy_interface_config(mode))
#define gd32_enet_deinit(base)         (ARG_UNUSED(base), enet_deinit())
#define gd32_enet_software_reset(base) (ARG_UNUSED(base), enet_software_reset())
/* enet_init() of these HALs configures MDC itself */
#define gd32_enet_mdc_config(base)     (ARG_UNUSED(base), SUCCESS)
#endif

struct mdio_gd32_enet_config {
	const struct pinctrl_dev_config *pcfg;
	const struct pinctrl_dev_config *mac_pcfg;
	uintptr_t base;
	uint16_t clkid[3];
	bool rmii;
};

struct mdio_gd32_enet_data {
	struct k_mutex mutex;
};

static int mdio_gd32_enet_read(const struct device *dev, uint8_t prtad, uint8_t regad,
			       uint16_t *data)
{
	const struct mdio_gd32_enet_config *cfg = dev->config;
	struct mdio_gd32_enet_data *dev_data = dev->data;
	uint16_t value = 0U;
	ErrStatus ret;

	k_mutex_lock(&dev_data->mutex, K_FOREVER);
	ret = gd32_enet_phy_write_read(cfg->base, ENET_PHY_READ, prtad, regad, &value);
	k_mutex_unlock(&dev_data->mutex);

	if (ret != SUCCESS) {
		return -EIO;
	}

	*data = value;

	return 0;
}

static int mdio_gd32_enet_write(const struct device *dev, uint8_t prtad, uint8_t regad,
				uint16_t data)
{
	const struct mdio_gd32_enet_config *cfg = dev->config;
	struct mdio_gd32_enet_data *dev_data = dev->data;
	uint16_t value = data;
	ErrStatus ret;

	k_mutex_lock(&dev_data->mutex, K_FOREVER);
	ret = gd32_enet_phy_write_read(cfg->base, ENET_PHY_WRITE, prtad, regad, &value);
	k_mutex_unlock(&dev_data->mutex);

	return (ret == SUCCESS) ? 0 : -EIO;
}

static int mdio_gd32_enet_init(const struct device *dev)
{
	const struct mdio_gd32_enet_config *cfg = dev->config;
	struct mdio_gd32_enet_data *data = dev->data;
	int ret;
	uint16_t syscfg_clkid = DT_CLOCKS_CELL(SYSCFG_NODE, id);

	/*
	 * The PHY is probed before the MAC driver is initialized. SMI access
	 * only works after the vendor demo bring-up: pins, PHY interface
	 * select, ENET clocks, ENET reset, then the MDC divider.
	 */
	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		return ret;
	}

	/*
	 * The MAC reset needs the RMII reference clock on REF_CLK, so mux the
	 * MAC pins too. CKOUT0 (PHY clock) is already up (lower priority).
	 */
	ret = pinctrl_apply_state(cfg->mac_pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		return ret;
	}

	ret = clock_control_on(GD32_CLOCK_CONTROLLER, (clock_control_subsys_t)&syscfg_clkid);
	if (ret < 0) {
		return ret;
	}

	gd32_syscfg_enet_phy_cfg(cfg->base,
				 cfg->rmii ? SYSCFG_ENET_PHY_RMII : SYSCFG_ENET_PHY_MII);

	/* The MAC clocks must be running before any reset or SMI access */
	for (size_t i = 0U; i < ARRAY_SIZE(cfg->clkid); i++) {
		ret = clock_control_on(GD32_CLOCK_CONTROLLER,
				       (clock_control_subsys_t)&cfg->clkid[i]);
		if (ret < 0) {
			return ret;
		}
	}

	gd32_enet_deinit(cfg->base);

	if (gd32_enet_software_reset(cfg->base) == ERROR) {
		LOG_ERR("ENET software reset timeout");
		return -EIO;
	}

	/* The reset leaves MDC at its default divider, set it from HCLK */
	if (gd32_enet_mdc_config(cfg->base) == ERROR) {
		LOG_ERR("Failed to configure MDC clock");
		return -EIO;
	}

	k_mutex_init(&data->mutex);

	return 0;
}

static DEVICE_API(mdio, mdio_gd32_enet_api) = {
	.read = mdio_gd32_enet_read,
	.write = mdio_gd32_enet_write,
};

#define MDIO_GD32_ENET_MAC(inst) DT_CHILD(DT_INST_PARENT(inst), ethernet)

#define MDIO_GD32_ENET_DEVICE(inst)                                                                \
	PINCTRL_DT_INST_DEFINE(inst);                                                              \
	PINCTRL_DT_DEFINE(MDIO_GD32_ENET_MAC(inst));                                               \
	static const struct mdio_gd32_enet_config mdio_gd32_enet_config_##inst = {                 \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                                      \
		.mac_pcfg = PINCTRL_DT_DEV_CONFIG_GET(MDIO_GD32_ENET_MAC(inst)),                   \
		.base = DT_REG_ADDR(DT_INST_PARENT(inst)),                                         \
		.clkid = {DT_CLOCKS_CELL_BY_IDX(DT_INST_PARENT(inst), 0, id),                      \
			  DT_CLOCKS_CELL_BY_IDX(DT_INST_PARENT(inst), 1, id),                      \
			  DT_CLOCKS_CELL_BY_IDX(DT_INST_PARENT(inst), 2, id)},                     \
		.rmii = DT_ENUM_HAS_VALUE(DT_CHILD(DT_INST_PARENT(inst), ethernet),                       \
					  phy_connection_type, rmii),                              \
	};                                                                                         \
	static struct mdio_gd32_enet_data mdio_gd32_enet_data_##inst;                              \
	DEVICE_DT_INST_DEFINE(inst, mdio_gd32_enet_init, NULL, &mdio_gd32_enet_data_##inst,        \
			      &mdio_gd32_enet_config_##inst, POST_KERNEL,                          \
			      CONFIG_MDIO_INIT_PRIORITY, &mdio_gd32_enet_api);

DT_INST_FOREACH_STATUS_OKAY(MDIO_GD32_ENET_DEVICE)
