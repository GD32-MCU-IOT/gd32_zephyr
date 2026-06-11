/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || \
	!DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

/*
 * The overlay overrides board-default PA1 (ADC1_IN1) to PA0 (ADC1_IN0).
 * idx 0: ADC1 ch0 (PA0), idx 1: ADC0 ch13 (PC3)
 */
static const struct adc_dt_spec adc1_ch0 =
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);
static const struct adc_dt_spec adc0_ch13 =
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1);

static int read_adc(const struct adc_dt_spec *spec, uint16_t *out)
{
	struct adc_sequence seq = {
		.buffer = out,
		.buffer_size = sizeof(*out),
	};

	adc_sequence_init_dt(spec, &seq);

	return adc_read_dt(spec, &seq);
}

int main(void)
{
	int rc;

	if (!adc_is_ready_dt(&adc1_ch0)) {
		printk("ADC1 controller (channel 0) not ready\n");
		return 0;
	}
	if (!adc_is_ready_dt(&adc0_ch13)) {
		printk("ADC0 controller (channel 13) not ready\n");
		return 0;
	}

	if (gpio_is_ready_dt(&led)) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	}

	rc = adc_channel_setup_dt(&adc1_ch0);
	printk("setup adc1 ch0 rc=%d\n", rc);
	rc = adc_channel_setup_dt(&adc0_ch13);
	printk("setup adc0 ch13 rc=%d\n", rc);

	while (1) {
		uint16_t pa0 = 0, pc3 = 0;

		gpio_pin_toggle_dt(&led);

		rc = read_adc(&adc1_ch0, &pa0);
		if (rc < 0) {
			printk("read PA0 failed rc=%d\n", rc);
		}
		rc = read_adc(&adc0_ch13, &pc3);
		if (rc < 0) {
			printk("read PC3 failed rc=%d\n", rc);
		}

		printk("PA0=%u  PC3=%u\n", pa0, pc3);

		k_sleep(K_MSEC(200));
	}

	return 0;
}
