/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define PWM_NODE    DT_ALIAS(pwm0)
#define PWM_CHANNEL 0U

/* period/pulse are expressed in PWM controller ticks (prescaler applied) */
#define PWM_PERIOD 1000U
#define PWM_PULSE  500U /* 50% duty */

static const struct device *const pwm_dev = DEVICE_DT_GET(PWM_NODE);
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

int main(void)
{
	int ret;

	if (!device_is_ready(pwm_dev)) {
		printk("PWM device %s not ready\n", pwm_dev->name);
		return -1;
	}

	ret = pwm_set_cycles(pwm_dev, PWM_CHANNEL, PWM_PERIOD, PWM_PULSE,
			     PWM_POLARITY_NORMAL);
	if (ret < 0) {
		printk("pwm_set_cycles failed (%d)\n", ret);
		return ret;
	}

	printk("PWM running on %s ch%u\n", pwm_dev->name, PWM_CHANNEL);

	if (gpio_is_ready_dt(&led)) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	}

	while (1) {
		gpio_pin_toggle_dt(&led);
		k_sleep(K_MSEC(500));
	}

	return 0;
}
