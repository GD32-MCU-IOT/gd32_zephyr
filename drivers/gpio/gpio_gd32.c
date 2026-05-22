/*
 * Copyright (c) 2021 Teslabs Engineering S.L.
 * Copyright (c) 2025 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT gd_gd32_gpio

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/gd32.h>
#include <zephyr/drivers/interrupt_controller/gd32_exti.h>
#include <zephyr/drivers/reset.h>

#include <gd32_gpio.h>

#include <zephyr/drivers/gpio/gpio_utils.h>

#if defined(CONFIG_GD32_HAS_AF_PINMUX)
/** SYSCFG DT node */
#define SYSCFG_NODE DT_NODELABEL(syscfg)
#else
/** AFIO DT node */
#define AFIO_NODE DT_NODELABEL(afio)

/** GPIO mode: analog (CTL bits) */
#define CTL_MODE_ANALOG 0x0U
/** GPIO mode: input floating (CTL bits) */
#define CTL_MODE_INP_FLOAT 0x4U
/** GPIO mode: input with pull-up/down (CTL bits) */
#define CTL_MODE_INP_PUPD 0x8U
/** GPIO mode: output push-pull @ 2MHz (CTL bits) */
#define CTL_MODE_OUT_PP 0x2U
/** GPIO mode: output open-drain @ 2MHz (CTL bits) */
#define CTL_MODE_OUT_OD 0x6U
#endif /* CONFIG_GD32_HAS_AF_PINMUX */

/** EXTISS mask */
#define EXTISS_MSK 0xFU
/** EXTISS line step size */
#define EXTISS_STEP 4U
/** EXTISS line shift */
#define EXTISS_LINE_SHIFT(pin) (EXTISS_STEP * ((pin) % EXTISS_STEP))

struct gpio_gd32_config {
	struct gpio_driver_config common;
	uint32_t reg;
	uint16_t clkid;
	uint16_t clkid_exti;
	struct reset_dt_spec reset;
};

struct gpio_gd32_data {
	struct gpio_driver_data common;
	sys_slist_t callbacks;
#ifdef CONFIG_GPIO_GET_DIRECTION
	uint32_t input_pins;
	uint32_t output_pins;
#endif /* CONFIG_GPIO_GET_DIRECTION */
#if defined(CONFIG_SOC_SERIES_GD32M53X)
	/* Reverse map: EXTI line -> GPIO pin, populated on interrupt configure. */
	uint8_t line_to_pin[16];
#endif /* CONFIG_SOC_SERIES_GD32M53X */
};

#if defined(CONFIG_SOC_SERIES_GD32M53X)
/*
 * GD32M53x pin->EXTI mapping is non-linear. Table [compact_port][pin] encodes
 * ((exti_line << 4) | extiss_value); 0xFF = no mapping. compact_port: A..G->0..6, N->7.
 * Source: gd32m53x_syscfg.h exti_gpio_enum.
 */
#define M53X_EXTI_INVALID 0xFFU
#define M53X_EXTI_LINE(v) ((uint8_t)((v) >> 4))
#define M53X_EXTI_VAL(v)  ((uint8_t)((v) & 0x0FU))
#define M53X_EXTI_NUM_PORTS 8U

static const uint8_t m53x_exti_map[M53X_EXTI_NUM_PORTS][16] = {
	/* GPIOA: PA0->E2, PA1->E4, PA8->E0, PA9->E1 */
	{0x20, 0x40, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	 0x00, 0x10, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
	/* GPIOB: PB0->E7, PB1->E6, PB2->E10, PB14->E14, PB15->E15 */
	{0x70, 0x60, 0xA0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0, 0xF0},
	/* GPIOC: PC0->E10..PC5->E15, PC6->E6..PC12->E2 */
	{0xA1, 0xB0, 0xC0, 0xD0, 0xE1, 0xF1, 0x61, 0x71,
	 0x03, 0x13, 0x01, 0x11, 0x21, 0xFF, 0xFF, 0xFF},
	/* GPIOD: PD2->E6, PD4->E8, PD5->E9, PD8->E5..PD14->E14 */
	{0xFF, 0xFF, 0x62, 0xFF, 0x80, 0x90, 0xFF, 0xFF,
	 0x50, 0x91, 0xA2, 0xB1, 0xC1, 0xD1, 0xE2, 0xFF},
	/* GPIOE: PE8->E8 PE9->E9 PE10->E10 PE11->E14 PE12->E12 PE13->E1 PE14->E4 */
	{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	 0x81, 0x92, 0xA3, 0xE3, 0xC2, 0x12, 0x41, 0xFF},
	/* GPIOF: PF8->E8 PF9->E4 PF10->E10 PF11->E9 PF12->E3 PF13->E4 PF14->E2 */
	{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	 0x82, 0x42, 0xA4, 0x93, 0x30, 0x43, 0x22, 0xFF},
	/* GPIOG: PG11->E11 PG12->E2 PG13->E6 PG14->E5 PG15->E8 */
	{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xB2, 0x23, 0x63, 0x51, 0x83},
	/* GPION: PN2->E0 PN5->E7 PN7->E5 */
	{0xFF, 0xFF, 0x02, 0xFF, 0xFF, 0x72, 0xFF, 0x52,
	 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
};

static inline int m53x_port_compact(uint8_t port_index)
{
	if (port_index <= 6U) {
		return (int)port_index;
	}
	if (port_index == 16U) {
		return 7; /* GPION */
	}
	return -1;
}

static uint8_t m53x_exti_lookup(const struct device *port, gpio_pin_t pin)
{
	const struct gpio_gd32_config *config = port->config;
	uint8_t port_index = (config->reg - GPIOA) / (GPIOB - GPIOA);
	int cp = m53x_port_compact(port_index);

	if (cp < 0 || pin >= 16U) {
		return M53X_EXTI_INVALID;
	}

	return m53x_exti_map[cp][pin];
}
#endif /* CONFIG_SOC_SERIES_GD32M53X */

/**
 * @brief EXTI ISR callback.
 *
 * @param line EXTI line number.
 * @param arg GPIO port instance.
 */
static void gpio_gd32_isr(uint8_t line, void *arg)
{
	const struct device *dev = arg;
	struct gpio_gd32_data *data = dev->data;
	uint8_t pin = line;

#if defined(CONFIG_SOC_SERIES_GD32M53X)
	/* On GD32M53x the EXTI line != pin; map it back to the configured pin. */
	pin = data->line_to_pin[line];
#endif /* CONFIG_SOC_SERIES_GD32M53X */

	gpio_fire_callbacks(&data->callbacks, dev, BIT(pin));
}

/**
 * @brief Configure EXTI source selection register.
 *
 * @param port GPIO port instance.
 * @param pin GPIO pin number.
 *
 * @retval 0 on success.
 * @retval -EINVAL if pin is not valid.
 */
static int gpio_gd32_configure_extiss(const struct device *port,
				      gpio_pin_t pin)
{
	const struct gpio_gd32_config *config = port->config;
	uint8_t port_index, shift;
	uint8_t line = pin;
	volatile uint32_t *extiss;

#if defined(CONFIG_SOC_SERIES_GD32M53X)
	uint8_t enc = m53x_exti_lookup(port, pin);

	if (enc == M53X_EXTI_INVALID) {
		return -EINVAL;
	}
	line = M53X_EXTI_LINE(enc);
#endif /* CONFIG_SOC_SERIES_GD32M53X */

	switch (line / EXTISS_STEP) {
#if defined(CONFIG_GD32_HAS_AF_PINMUX) && !defined(CONFIG_SOC_SERIES_GD32F50X)
	case 0U:
		extiss = &SYSCFG_EXTISS0;
		break;
	case 1U:
		extiss = &SYSCFG_EXTISS1;
		break;
	case 2U:
		extiss = &SYSCFG_EXTISS2;
		break;
	case 3U:
		extiss = &SYSCFG_EXTISS3;
		break;
#else
	case 0U:
		extiss = &AFIO_EXTISS0;
		break;
	case 1U:
		extiss = &AFIO_EXTISS1;
		break;
	case 2U:
		extiss = &AFIO_EXTISS2;
		break;
	case 3U:
		extiss = &AFIO_EXTISS3;
		break;
#endif /* CONFIG_GD32_HAS_AF_PINMUX */
	default:
		return -EINVAL;
	}

	port_index = (config->reg - GPIOA) / (GPIOB - GPIOA);
	shift = EXTISS_LINE_SHIFT(line);

#if defined(CONFIG_SOC_SERIES_GD32M53X)
	port_index = M53X_EXTI_VAL(enc);
#endif /* CONFIG_SOC_SERIES_GD32M53X */

	*extiss &= ~(EXTISS_MSK << shift);
	*extiss |= port_index << shift;

	return 0;
}

static inline int gpio_gd32_configure(const struct device *port, gpio_pin_t pin,
				      gpio_flags_t flags)
{
	const struct gpio_gd32_config *config = port->config;

#ifdef CONFIG_GPIO_GET_DIRECTION
	struct gpio_gd32_data *data = port->data;
	gpio_pin_t orig_pin = pin;
#endif /* CONFIG_GPIO_GET_DIRECTION */

#ifdef CONFIG_GD32_HAS_AF_PINMUX
	uint32_t ctl, pupd;

	ctl = GPIO_CTL(config->reg);
	ctl &= ~GPIO_MODE_MASK(pin);

	pupd = GPIO_PUD(config->reg);
	pupd &= ~GPIO_PUPD_MASK(pin);

	if ((flags & GPIO_OUTPUT) != 0U) {
		ctl |= GPIO_MODE_SET(pin, GPIO_MODE_OUTPUT);

		if ((flags & GPIO_SINGLE_ENDED) != 0U) {
			if ((flags & GPIO_LINE_OPEN_DRAIN) != 0U) {
				GPIO_OMODE(config->reg) |= BIT(pin);
			} else {
				return -ENOTSUP;
			}
		} else {
			GPIO_OMODE(config->reg) &= ~BIT(pin);
		}

		if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0U) {
			GPIO_BOP(config->reg) = BIT(pin);
		} else if ((flags & GPIO_OUTPUT_INIT_LOW) != 0U) {
			GPIO_BC(config->reg) = BIT(pin);
		}
	} else if ((flags & GPIO_INPUT) != 0U) {
		ctl |= GPIO_MODE_SET(pin, GPIO_MODE_INPUT);
	} else {
		ctl |= GPIO_MODE_SET(pin, GPIO_MODE_ANALOG);
	}

	if ((flags & GPIO_PULL_UP) != 0U) {
		pupd |= GPIO_PUPD_SET(pin, GPIO_PUPD_PULLUP);
	} else if ((flags & GPIO_PULL_DOWN) != 0U) {
		pupd |= GPIO_PUPD_SET(pin, GPIO_PUPD_PULLDOWN);
	} else {
		pupd |= GPIO_PUPD_SET(pin, GPIO_PUPD_NONE);
	}

	GPIO_PUD(config->reg) = pupd;
	GPIO_CTL(config->reg) = ctl;
#else
	volatile uint32_t *ctl_reg;
	uint32_t ctl, pin_bit;

	pin_bit = BIT(pin);

	if (pin < 8U) {
		ctl_reg = &GPIO_CTL0(config->reg);
	} else {
		ctl_reg = &GPIO_CTL1(config->reg);
		pin -= 8U;
	}

	ctl = *ctl_reg;
	ctl &= ~GPIO_MODE_MASK(pin);

	if ((flags & GPIO_OUTPUT) != 0U) {
		if ((flags & GPIO_SINGLE_ENDED) != 0U) {
			if ((flags & GPIO_LINE_OPEN_DRAIN) != 0U) {
				ctl |= GPIO_MODE_SET(pin, CTL_MODE_OUT_OD);
			} else {
				return -ENOTSUP;
			}
		} else {
			ctl |= GPIO_MODE_SET(pin, CTL_MODE_OUT_PP);
		}

		if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0U) {
			GPIO_BOP(config->reg) = pin_bit;
		} else if ((flags & GPIO_OUTPUT_INIT_LOW) != 0U) {
			GPIO_BC(config->reg) = pin_bit;
		}
	} else if ((flags & GPIO_INPUT) != 0U) {
		if ((flags & GPIO_PULL_UP) != 0U) {
			ctl |= GPIO_MODE_SET(pin, CTL_MODE_INP_PUPD);
			GPIO_BOP(config->reg) = pin_bit;
		} else if ((flags & GPIO_PULL_DOWN) != 0U) {
			ctl |= GPIO_MODE_SET(pin, CTL_MODE_INP_PUPD);
			GPIO_BC(config->reg) = pin_bit;
		} else {
			ctl |= GPIO_MODE_SET(pin, CTL_MODE_INP_FLOAT);
		}
	} else {
		ctl |= GPIO_MODE_SET(pin, CTL_MODE_ANALOG);
	}

	*ctl_reg = ctl;
#endif /* CONFIG_GD32_HAS_AF_PINMUX */

#ifdef CONFIG_GPIO_GET_DIRECTION
	if ((flags & GPIO_INPUT) != 0U) {
		data->input_pins |= BIT(orig_pin);
	} else {
		data->input_pins &= ~BIT(orig_pin);
	}

	if ((flags & GPIO_OUTPUT) != 0U) {
		data->output_pins |= BIT(orig_pin);
	} else {
		data->output_pins &= ~BIT(orig_pin);
	}
#endif /* CONFIG_GPIO_GET_DIRECTION */

	return 0;
}

static int gpio_gd32_port_get_raw(const struct device *port, uint32_t *value)
{
	const struct gpio_gd32_config *config = port->config;

	*value = GPIO_ISTAT(config->reg);

	return 0;
}

static int gpio_gd32_port_set_masked_raw(const struct device *port,
					 gpio_port_pins_t mask,
					 gpio_port_value_t value)
{
	const struct gpio_gd32_config *config = port->config;

	GPIO_OCTL(config->reg) =
		(GPIO_OCTL(config->reg) & ~mask) | (value & mask);

	return 0;
}

static int gpio_gd32_port_set_bits_raw(const struct device *port,
				       gpio_port_pins_t pins)
{
	const struct gpio_gd32_config *config = port->config;

	GPIO_BOP(config->reg) = pins;

	return 0;
}

static int gpio_gd32_port_clear_bits_raw(const struct device *port,
					 gpio_port_pins_t pins)
{
	const struct gpio_gd32_config *config = port->config;

	GPIO_BC(config->reg) = pins;

	return 0;
}

static int gpio_gd32_port_toggle_bits(const struct device *port,
				      gpio_port_pins_t pins)
{
	const struct gpio_gd32_config *config = port->config;

#if defined(CONFIG_GD32_HAS_AF_PINMUX) && !defined(CONFIG_SOC_SERIES_GD32F50X)
	GPIO_TG(config->reg) = pins;
#else
	GPIO_OCTL(config->reg) ^= pins;
#endif /* CONFIG_GD32_HAS_AF_PINMUX */

	return 0;
}

static int gpio_gd32_pin_interrupt_configure(const struct device *port,
					     gpio_pin_t pin,
					     enum gpio_int_mode mode,
					     enum gpio_int_trig trig)
{
	uint8_t line = pin;
#if defined(CONFIG_SOC_SERIES_GD32M53X)
	struct gpio_gd32_data *data = port->data;
	uint8_t enc = m53x_exti_lookup(port, pin);

	if (enc == M53X_EXTI_INVALID) {
		return -EINVAL;
	}
	line = M53X_EXTI_LINE(enc);
#endif /* CONFIG_SOC_SERIES_GD32M53X */

	if (mode == GPIO_INT_MODE_DISABLED) {
		gd32_exti_disable(line);
		(void)gd32_exti_configure(line, NULL, NULL);
		gd32_exti_trigger(line, GD32_EXTI_TRIG_NONE);
	} else if (mode == GPIO_INT_MODE_EDGE) {
		int ret;

		ret = gd32_exti_configure(line, gpio_gd32_isr, (void *)port);
		if (ret < 0) {
			return ret;
		}

		ret = gpio_gd32_configure_extiss(port, pin);
		if (ret < 0) {
			return ret;
		}

#if defined(CONFIG_SOC_SERIES_GD32M53X)
		data->line_to_pin[line] = pin;
#endif /* CONFIG_SOC_SERIES_GD32M53X */

		switch (trig) {
		case GPIO_INT_TRIG_LOW:
			gd32_exti_trigger(line, GD32_EXTI_TRIG_FALLING);
			break;
		case GPIO_INT_TRIG_HIGH:
			gd32_exti_trigger(line, GD32_EXTI_TRIG_RISING);
			break;
		case GPIO_INT_TRIG_BOTH:
			gd32_exti_trigger(line, GD32_EXTI_TRIG_BOTH);
			break;
		default:
			return -ENOTSUP;
		}

		gd32_exti_enable(line);
	} else {
		return -ENOTSUP;
	}

	return 0;
}

#ifdef CONFIG_GPIO_GET_DIRECTION
static int gpio_gd32_port_get_direction(const struct device *port,
					gpio_port_pins_t map,
					gpio_port_pins_t *inputs,
					gpio_port_pins_t *outputs)
{
	const struct gpio_gd32_config *config = port->config;
	struct gpio_gd32_data *data = port->data;

	map &= config->common.port_pin_mask;

	if (inputs != NULL) {
		*inputs = map & data->input_pins;
	}

	if (outputs != NULL) {
		*outputs = map & data->output_pins;
	}

	return 0;
}
#endif /* CONFIG_GPIO_GET_DIRECTION */

static int gpio_gd32_manage_callback(const struct device *dev,
				     struct gpio_callback *callback, bool set)
{
	struct gpio_gd32_data *data = dev->data;

	return gpio_manage_callback(&data->callbacks, callback, set);
}

static DEVICE_API(gpio, gpio_gd32_api) = {
	.pin_configure = gpio_gd32_configure,
	.port_get_raw = gpio_gd32_port_get_raw,
	.port_set_masked_raw = gpio_gd32_port_set_masked_raw,
	.port_set_bits_raw = gpio_gd32_port_set_bits_raw,
	.port_clear_bits_raw = gpio_gd32_port_clear_bits_raw,
	.port_toggle_bits = gpio_gd32_port_toggle_bits,
	.pin_interrupt_configure = gpio_gd32_pin_interrupt_configure,
	.manage_callback = gpio_gd32_manage_callback,
#ifdef CONFIG_GPIO_GET_DIRECTION
	.port_get_direction = gpio_gd32_port_get_direction,
#endif
};

static int gpio_gd32_init(const struct device *port)
{
	const struct gpio_gd32_config *config = port->config;

	(void)clock_control_on(GD32_CLOCK_CONTROLLER,
			       (clock_control_subsys_t)&config->clkid);
	(void)clock_control_on(GD32_CLOCK_CONTROLLER,
			       (clock_control_subsys_t)&config->clkid_exti);

	(void)reset_line_toggle_dt(&config->reset);

	return 0;
}

#define GPIO_GD32_DEFINE(n)						       \
	static const struct gpio_gd32_config gpio_gd32_config##n = {	       \
		.common = GPIO_COMMON_CONFIG_FROM_DT_INST(n),		       \
		.reg = DT_INST_REG_ADDR(n),				       \
		.clkid = DT_INST_CLOCKS_CELL(n, id),			       \
		COND_CODE_1(DT_NODE_HAS_STATUS_OKAY(SYSCFG_NODE),	       \
			    (.clkid_exti = DT_CLOCKS_CELL(SYSCFG_NODE, id),),  \
			    (.clkid_exti = DT_CLOCKS_CELL(AFIO_NODE, id),))    \
		.reset = RESET_DT_SPEC_INST_GET(n),			       \
	};								       \
									       \
	static struct gpio_gd32_data gpio_gd32_data##n;			       \
									       \
	DEVICE_DT_INST_DEFINE(n, gpio_gd32_init, NULL, &gpio_gd32_data##n,     \
			      &gpio_gd32_config##n, PRE_KERNEL_1,	       \
			      CONFIG_GPIO_INIT_PRIORITY, &gpio_gd32_api);

DT_INST_FOREACH_STATUS_OKAY(GPIO_GD32_DEFINE)
