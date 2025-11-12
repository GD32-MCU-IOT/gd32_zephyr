/*
 * Copyright (c) 2021 BrainCo Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
#include <zephyr/irq.h>
#include <errno.h>

LOG_MODULE_REGISTER(i2c_gd32_common, CONFIG_I2C_LOG_LEVEL);

#include "i2c-priv.h"
#include "i2c_gd32.h"
#ifdef CONFIG_I2C_GD32_DMA

void i2c_gd32_dma_callback(const struct device *dma_dev, void *arg, uint32_t channel, int status)
{
	i2c_gd32_dma_callback_gd(dma_dev, arg, channel, status);
}
#endif /* CONFIG_I2C_GD32_DMA */

/*
 * ADD IP specific TBE handler.
 * Send one byte, disable TIE on last byte, wait for TC or AUTOEND->STOP.
 * Don't give semaphore here unless extreme case (xfer_len==0 without TC).
 */
static void i2c_gd32_event_isr(const struct device *dev)
{
	i2c_gd32_event_isr_gd(dev);
}

static void i2c_gd32_error_isr(const struct device *dev)
{
	i2c_gd32_error_isr_gd(dev);
}

static int i2c_gd32_transfer(const struct device *dev,
			     struct i2c_msg *msgs,
			     uint8_t num_msgs,
			     uint16_t addr)
{
	return i2c_gd32_transfer_gd(dev, msgs, num_msgs, addr);
}

static int i2c_gd32_configure(const struct device *dev,
			      uint32_t dev_config)
{
	return i2c_gd32_configure_gd(dev, dev_config);
}

static DEVICE_API(i2c, i2c_gd32_driver_api) = {
	.configure = i2c_gd32_configure,
	.transfer = i2c_gd32_transfer,
#ifdef CONFIG_I2C_RTIO
	.iodev_submit = i2c_iodev_submit_fallback,
#endif
#ifdef CONFIG_I2C_TARGET
	.target_register = i2c_gd32_target_register,
	.target_unregister = i2c_gd32_target_unregister,
#endif
};

static int i2c_gd32_init(const struct device *dev)
{
	struct i2c_gd32_data *data = dev->data;
	const struct i2c_gd32_config *cfg = dev->config;
	uint32_t bitrate_cfg;
	int err;

	err = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (err < 0) {
		return err;
	}

	/* Mutex semaphore to protect the i2c api in multi-thread env. */
	k_sem_init(&data->bus_mutex, 1, 1);

	/* Sync semaphore to sync i2c state between isr and transfer api. */
	k_sem_init(&data->sync_sem, 0, K_SEM_MAX_LIMIT);

	(void)clock_control_on(GD32_CLOCK_CONTROLLER,
			       (clock_control_subsys_t)&cfg->clkid);

	(void)reset_line_toggle_dt(&cfg->reset);

	cfg->irq_cfg_func();

	bitrate_cfg = i2c_map_dt_bitrate(cfg->bitrate);

	i2c_gd32_configure(dev, I2C_MODE_CONTROLLER | bitrate_cfg);

#ifdef CONFIG_I2C_GD32_DMA
	/* Initialize DMA if configured */
	if ((cfg->dma[RX].dev && !cfg->dma[TX].dev) ||
	    (cfg->dma[TX].dev && !cfg->dma[RX].dev)) {
		LOG_ERR("DMA must be enabled for both TX and RX channels");
		return -ENODEV;
	}

	uint32_t ch_filter;

	for (size_t i = 0; i < i2c_gd32_dma_enabled_num(dev); i++) {
		if (!device_is_ready(cfg->dma[i].dev)) {
			LOG_ERR("DMA %s not ready", cfg->dma[i].dev->name);
			return -ENODEV;
		}

		ch_filter = BIT(cfg->dma[i].channel);
		err = dma_request_channel(cfg->dma[i].dev, &ch_filter);
		if (err < 0) {
			LOG_ERR("dma_request_channel failed %d", err);
			return err;
		}
	}

	/* Enable DMA by default, can be disabled at runtime for debugging */
	data->dma_enabled = true;
#endif

	return 0;
}

#ifdef CONFIG_I2C_GD32_DMA
#define DMA_INITIALIZER(idx, dir)                                              \
	{                                                                      \
		.dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(idx, dir)),     \
		.channel = DT_INST_DMAS_CELL_BY_NAME(idx, dir, channel),       \
		.slot = COND_CODE_1(\
			DT_HAS_COMPAT_STATUS_OKAY(gd_gd32_dma_v1),             \
			(DT_INST_DMAS_CELL_BY_NAME(idx, dir, slot)), (0)),     \
		.config = DT_INST_DMAS_CELL_BY_NAME(idx, dir, config),         \
		.fifo_threshold = COND_CODE_1(\
			DT_HAS_COMPAT_STATUS_OKAY(gd_gd32_dma_v1), \
			(DT_INST_DMAS_CELL_BY_NAME(idx, dir, fifo_threshold)), \
			(0)),                                                 \
	}

#define DMAS_DECL(idx)                                                         \
	{                                                                      \
		COND_CODE_1(DT_INST_DMAS_HAS_NAME(idx, rx),                    \
			    (DMA_INITIALIZER(idx, rx)), ({0})),                \
		COND_CODE_1(DT_INST_DMAS_HAS_NAME(idx, tx),                    \
			    (DMA_INITIALIZER(idx, tx)), ({0})),                \
	}
#else
#define DMAS_DECL(idx)
#endif

#define I2C_GD32_INIT(inst)							\
	PINCTRL_DT_INST_DEFINE(inst);						\
	static void i2c_gd32_irq_cfg_func_##inst(void)				\
	{									\
		IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, event, irq),		\
			    DT_INST_IRQ_BY_NAME(inst, event, priority),		\
			    i2c_gd32_event_isr,					\
			    DEVICE_DT_INST_GET(inst),				\
			    0);							\
		irq_enable(DT_INST_IRQ_BY_NAME(inst, event, irq));		\
										\
		IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, error, irq),		\
			    DT_INST_IRQ_BY_NAME(inst, error, priority),		\
			    i2c_gd32_error_isr,					\
			    DEVICE_DT_INST_GET(inst),				\
			    0);							\
		irq_enable(DT_INST_IRQ_BY_NAME(inst, error, irq));		\
	}									\
	static struct i2c_gd32_data i2c_gd32_data_##inst;			\
	const static struct i2c_gd32_config i2c_gd32_cfg_##inst = {		\
		.reg = DT_INST_REG_ADDR(inst),					\
		.bitrate = DT_INST_PROP(inst, clock_frequency),			\
		.clkid = DT_INST_CLOCKS_CELL(inst, id),				\
		.reset = RESET_DT_SPEC_INST_GET(inst),				\
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),			\
		.irq_cfg_func = i2c_gd32_irq_cfg_func_##inst,			\
		IF_ENABLED(CONFIG_I2C_GD32_DMA, (.dma = DMAS_DECL(inst),))	\
	};									\
	I2C_DEVICE_DT_INST_DEFINE(inst,						\
				  i2c_gd32_init, NULL,				\
				  &i2c_gd32_data_##inst, &i2c_gd32_cfg_##inst,	\
				  POST_KERNEL, CONFIG_I2C_INIT_PRIORITY,	\
				  &i2c_gd32_driver_api);

DT_INST_FOREACH_STATUS_OKAY(I2C_GD32_INIT)
