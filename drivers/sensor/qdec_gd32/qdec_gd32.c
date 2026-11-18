/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT gd_gd32_qdec

#include <errno.h>
#include <limits.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/gd32.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/qdec_gd32.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>

#include <gd32_timer.h>

struct qdec_gd32_config {
	uint32_t reg;
	uint16_t clkid;
	struct reset_dt_spec reset;
	const struct pinctrl_dev_config *pcfg;
	uint32_t encoder_mode;
	uint8_t input_filter;
	bool invert_direction;
	void (*irq_config)(const struct device *dev);
};

struct qdec_gd32_data {
	int32_t overflow_count;
	int32_t sample;
};

/* Keep enough headroom so that later +/-0x10000 overflow adjustments and the
 * addition of the 16-bit hardware counter stay within the int32_t range.
 * The accumulated count saturates here instead of wrapping around.
 */
#define QDEC_GD32_COUNT_MIN (INT32_MIN + 0x10000)
#define QDEC_GD32_COUNT_MAX (INT32_MAX - 0x10000)

static int32_t qdec_gd32_saturate(int64_t value)
{
	if (value > QDEC_GD32_COUNT_MAX) {
		return QDEC_GD32_COUNT_MAX;
	}
	if (value < QDEC_GD32_COUNT_MIN) {
		return QDEC_GD32_COUNT_MIN;
	}
	return (int32_t)value;
}

static void qdec_gd32_update_overflow(const struct device *dev)
{
	const struct qdec_gd32_config *config = dev->config;
	struct qdec_gd32_data *data = dev->data;
	uint32_t reg = config->reg;
	uint32_t mid = ((uint32_t)TIMER_CAR(reg) + 1U) / 2U;

	/* Sample TIMER_CNT while the pending update flag is still set: it is the
	 * direction evidence for that crossing (near 0 after an overflow, near
	 * CAR after an underflow). Clearing the flag first would let a later,
	 * opposite crossing's counter value be attributed to this wrap.
	 * TIMER_CTL0_DIR cannot be used instead, it only reflects the current
	 * direction and can flip before the ISR runs.
	 *
	 * A crossing arriving after the clear re-asserts the sticky flag and is
	 * handled by the next iteration. One arriving in the short window between
	 * the sample and the clear coalesces with the pending flag; see the
	 * QDEC_GD32 Kconfig help for this documented limitation.
	 */
	for (unsigned int retry = 0U; retry < 3U; retry++) {
		uint16_t cnt;
		int32_t delta;

		if ((TIMER_INTF(reg) & TIMER_INT_FLAG_UP) == 0U) {
			break;
		}

		cnt = (uint16_t)TIMER_CNT(reg);
		timer_interrupt_flag_clear(reg, TIMER_INT_FLAG_UP);

		delta = (cnt < mid) ? (int32_t)0x10000 : -(int32_t)0x10000;
		if (config->invert_direction) {
			delta = -delta;
		}
		data->overflow_count = qdec_gd32_saturate((int64_t)data->overflow_count + delta);
	}
}

static void qdec_gd32_isr(const struct device *dev)
{
	qdec_gd32_update_overflow(dev);
}

static int qdec_gd32_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	const struct qdec_gd32_config *config = dev->config;
	struct qdec_gd32_data *data = dev->data;
	int32_t overflow_count;
	uint16_t counter;
	unsigned int key;

	if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_ENCODER_COUNT) {
		return -ENOTSUP;
	}

	key = irq_lock();
	/* Retry until the counter snapshot and the overflow base belong to the
	 * same wrap epoch: a single reread can still straddle another crossing
	 * when the encoder reverses.
	 */
	for (unsigned int retry = 0; retry < 3U; retry++) {
		qdec_gd32_update_overflow(dev);
		overflow_count = data->overflow_count;
		counter = (uint16_t)TIMER_CNT(config->reg);
		qdec_gd32_update_overflow(dev);
		if (overflow_count == data->overflow_count) {
			break;
		}
		counter = (uint16_t)TIMER_CNT(config->reg);
	}
	data->sample = qdec_gd32_saturate(
		(int64_t)data->overflow_count +
		(config->invert_direction ? -(int32_t)counter : (int32_t)counter));
	irq_unlock(key);

	return 0;
}

static int qdec_gd32_channel_get(const struct device *dev, enum sensor_channel chan,
				 struct sensor_value *value)
{
	struct qdec_gd32_data *data = dev->data;

	if (chan != SENSOR_CHAN_ENCODER_COUNT) {
		return -ENOTSUP;
	}

	value->val1 = data->sample;
	value->val2 = 0;
	return 0;
}

int qdec_gd32_set_count(const struct device *dev, int32_t count)
{
	const struct qdec_gd32_config *config = dev->config;
	struct qdec_gd32_data *data = dev->data;
	unsigned int key = irq_lock();

	if (count > QDEC_GD32_COUNT_MAX) {
		count = QDEC_GD32_COUNT_MAX;
	} else if (count < QDEC_GD32_COUNT_MIN) {
		count = QDEC_GD32_COUNT_MIN;
	}

	/* irq_lock() only masks CPU interrupts; encoder edges keep arriving. Stop
	 * the counter while resetting so that no new underflow can be raised and
	 * then discarded by the flag clear below. Edges arriving while the counter
	 * is stopped are lost, so callers must preset only while the encoder is
	 * stationary.
	 */
	TIMER_CTL0(config->reg) &= ~TIMER_CTL0_CEN;
	data->overflow_count = count;
	data->sample = count;
	TIMER_CNT(config->reg) = 0U;
	timer_interrupt_flag_clear(config->reg, TIMER_INT_FLAG_UP);
	TIMER_CTL0(config->reg) |= TIMER_CTL0_CEN;
	irq_unlock(key);

	return 0;
}

static int qdec_gd32_init(const struct device *dev)
{
	const struct qdec_gd32_config *config = dev->config;
	int result;

	result = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (result != 0) {
		return result;
	}

	result = clock_control_on(GD32_CLOCK_CONTROLLER, (clock_control_subsys_t)&config->clkid);
	if (result != 0) {
		return result;
	}

	result = reset_line_toggle_dt(&config->reset);
	if (result != 0) {
		return result;
	}

	TIMER_CTL0(config->reg) = TIMER_COUNTER_EDGE | TIMER_COUNTER_UP;
	TIMER_PSC(config->reg) = 0U;
	TIMER_CAR(config->reg) = UINT16_MAX;
	TIMER_CNT(config->reg) = 0U;
	timer_quadrature_decoder_mode_config(config->reg, config->encoder_mode,
					     TIMER_IC_POLARITY_RISING, TIMER_IC_POLARITY_RISING);
	TIMER_CHCTL0(config->reg) &= ~(TIMER_CHCTL0_CH0CAPFLT | TIMER_CHCTL0_CH1CAPFLT);
	TIMER_CHCTL0(config->reg) |=
		((uint32_t)config->input_filter << 4U) | ((uint32_t)config->input_filter << 12U);
	TIMER_CHCTL2(config->reg) |= TIMER_CHCTL2_CH0EN | TIMER_CHCTL2_CH1EN;
	timer_interrupt_flag_clear(config->reg, TIMER_INT_FLAG_UP);
	timer_interrupt_enable(config->reg, TIMER_INT_UP);
	config->irq_config(dev);
	TIMER_CTL0(config->reg) |= TIMER_CTL0_CEN;

	return 0;
}

static DEVICE_API(sensor, qdec_gd32_api) = {
	.sample_fetch = qdec_gd32_sample_fetch,
	.channel_get = qdec_gd32_channel_get,
};

/* On advanced timers the first IRQ is the break vector, so prefer the named
 * update interrupt when the devicetree provides one. General-purpose timers
 * only expose a single vector and keep using the default IRQ.
 */
#define QDEC_GD32_IRQ_CONNECT_UP(n)                                                                \
	IRQ_CONNECT(DT_INST_IRQ_BY_NAME(n, up, irq), DT_INST_IRQ_BY_NAME(n, up, priority),         \
		    qdec_gd32_isr, DEVICE_DT_INST_GET(n), 0);                                      \
	irq_enable(DT_INST_IRQ_BY_NAME(n, up, irq))

#define QDEC_GD32_IRQ_CONNECT_DEFAULT(n)                                                           \
	IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), qdec_gd32_isr,                      \
		    DEVICE_DT_INST_GET(n), 0);                                                     \
	irq_enable(DT_INST_IRQN(n))

#define QDEC_GD32_IRQ_CFG(n)                                                                       \
	COND_CODE_1(DT_INST_IRQ_HAS_NAME(n, up), (QDEC_GD32_IRQ_CONNECT_UP(n)),                   \
		    (QDEC_GD32_IRQ_CONNECT_DEFAULT(n)))

#define QDEC_GD32_DEFINE(n)                                                                        \
	static void qdec_gd32_irq_config_##n(const struct device *dev)                             \
	{                                                                                          \
		QDEC_GD32_IRQ_CFG(n);                                                              \
	}                                                                                          \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	static struct qdec_gd32_data qdec_gd32_data_##n;                                           \
	static const struct qdec_gd32_config qdec_gd32_config_##n = {                              \
		.reg = DT_INST_REG_ADDR(n),                                                        \
		.clkid = DT_INST_CLOCKS_CELL(n, id),                                               \
		.reset = RESET_DT_SPEC_INST_GET(n),                                                \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                         \
		.encoder_mode = DT_INST_PROP(n, gd_encoder_mode),                                  \
		.input_filter = DT_INST_PROP(n, gd_input_filter),                                  \
		.invert_direction = DT_INST_PROP(n, gd_invert_direction),                          \
		.irq_config = qdec_gd32_irq_config_##n,                                            \
	};                                                                                         \
	SENSOR_DEVICE_DT_INST_DEFINE(n, qdec_gd32_init, NULL, &qdec_gd32_data_##n,                 \
				     &qdec_gd32_config_##n, POST_KERNEL,                           \
				     CONFIG_SENSOR_INIT_PRIORITY, &qdec_gd32_api);

DT_INST_FOREACH_STATUS_OKAY(QDEC_GD32_DEFINE)
