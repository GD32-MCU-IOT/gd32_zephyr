/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/drivers/pinctrl.h>
#include <soc.h>
#include <gd32_gpio.h>
#include <gd32_rcu.h>

void soc_early_init_hook(void)
{
	SystemInit();
}

void gd32e51x_usart5_pinmux_quirk(uint32_t usart_periph, const struct pinctrl_dev_config *pcfg)
{
	const struct pinctrl_state *state;

	if (usart_periph != USART5) {
		return;
	}

	if (pinctrl_lookup_state(pcfg, PINCTRL_STATE_DEFAULT, &state) < 0) {
		return;
	}

	rcu_periph_clock_enable(RCU_AF);

	for (uint8_t i = 0U; i < state->pin_cnt; i++) {
		uint32_t pin = state->pins[i];
		uint32_t port = GD32_PORT_GET(pin);
		uint32_t pin_num = GD32_PIN_GET(pin);

		if (port == 0U && pin_num == 11U) {
			gpio_afio_port_config(AFIO_PA11_USART5_CFG, ENABLE); /* PA11: TX */
		} else if (port == 0U && pin_num == 12U) {
			gpio_afio_port_config(AFIO_PA12_USART5_CFG, ENABLE); /* PA12: RX */
		} else if (port == 2U && pin_num == 6U) {
			gpio_afio_port_config(AFIO_PC6_USART5_CFG, ENABLE); /* PC6: TX */
		} else if (port == 2U && pin_num == 7U) {
			gpio_afio_port_config(AFIO_PC7_USART5_CFG, ENABLE); /* PC7: RX */
		} else if (port == 6U && pin_num == 14U) {
			gpio_afio_port_config(AFIO_PG14_USART5_CFG, ENABLE); /* PG14: TX */
		} else if (port == 6U && pin_num == 9U) {
			gpio_afio_port_config(AFIO_PG9_USART5_CFG, ENABLE); /* PG9: RX */
		}
	}
}
