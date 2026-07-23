/*
 * Copyright (c) 2026, GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT gd_gd32_enet_mdio

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/mdio.h>
#include <zephyr/drivers/pinctrl.h>

#include <gd32_enet.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(mdio_gd32_enet, CONFIG_MDIO_LOG_LEVEL);

/*
 * MDIO is embedded in the MAC's own registers, accessed via
 * enet_phy_write_read().
 */
#if defined(CONFIG_SOC_SERIES_GD32H7XX)
#define gd32_enet_phy_write_read(base, direction, addr, reg, pval)                                 \
	enet_phy_write_read((base), (direction), (addr), (reg), (pval))
#else
#define gd32_enet_phy_write_read(base, direction, addr, reg, pval)                                 \
	(ARG_UNUSED(base), enet_phy_write_read((direction), (addr), (reg), (pval)))
#endif

struct mdio_gd32_enet_config {
	const struct pinctrl_dev_config *pcfg;
	uintptr_t base;
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

	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		return ret;
	}

	k_mutex_init(&data->mutex);

	return 0;
}

static DEVICE_API(mdio, mdio_gd32_enet_api) = {
	.read = mdio_gd32_enet_read,
	.write = mdio_gd32_enet_write,
};

#define MDIO_GD32_ENET_DEVICE(inst)                                                                \
	PINCTRL_DT_INST_DEFINE(inst);                                                              \
	static const struct mdio_gd32_enet_config mdio_gd32_enet_config_##inst = {                 \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                                      \
		.base = DT_REG_ADDR(DT_INST_PARENT(inst)),                                         \
	};                                                                                         \
	static struct mdio_gd32_enet_data mdio_gd32_enet_data_##inst;                              \
	DEVICE_DT_INST_DEFINE(inst, mdio_gd32_enet_init, NULL, &mdio_gd32_enet_data_##inst,        \
			      &mdio_gd32_enet_config_##inst, POST_KERNEL,                          \
			      CONFIG_MDIO_INIT_PRIORITY, &mdio_gd32_enet_api);

DT_INST_FOREACH_STATUS_OKAY(MDIO_GD32_ENET_DEVICE)
