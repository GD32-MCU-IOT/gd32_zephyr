/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <lvgl.h>
#include <stdio.h>

#define SCREEN_WIDTH     240
#define SCREEN_HEIGHT    320
#define ADC_REFERENCE_MV 3300

static const struct gpio_dt_spec leds[] = {
	GPIO_DT_SPEC_GET(DT_NODELABEL(led1), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(led2), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(led3), gpios),
};

static const struct gpio_dt_spec keys[] = {
	GPIO_DT_SPEC_GET(DT_NODELABEL(key_a), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(key_b), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(key_c), gpios),
};

static const char *const key_names[] = {"WAKEUP", "USER", "TAMPER"};
static const struct adc_dt_spec potentiometer = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);
static const struct device *const eeprom = DEVICE_DT_GET(DT_ALIAS(eeprom_0));
static lv_obj_t *led_indicators[ARRAY_SIZE(leds)];
static lv_obj_t *key_labels[ARRAY_SIZE(keys)];
static lv_obj_t *voltage_label;
static lv_obj_t *voltage_bar;
static lv_obj_t *reference_label;
static lv_obj_t *temperature_label;
static lv_obj_t *address_label;
static lv_obj_t *value_label;
static lv_obj_t *eeprom_status;
static lv_obj_t *eeprom_dump;
static uint8_t address;
static uint8_t value;
static bool key_state[ARRAY_SIZE(keys)];
static uint8_t key_samples[ARRAY_SIZE(keys)];
static size_t active_led;
static bool internal_adc_ready;

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, int x, int y)
{
	lv_obj_t *label = lv_label_create(parent);

	lv_label_set_text(label, text);
	lv_obj_set_pos(label, x, y);
	return label;
}

enum eeprom_action {
	ADDR_DOWN,
	ADDR_UP,
	VALUE_DOWN,
	VALUE_UP,
	PROBE,
	READ_BYTE,
	WRITE_BYTE,
	READ_EIGHT,
};

static void eeprom_action_cb(lv_event_t *event)
{
	enum eeprom_action action = (enum eeprom_action)(uintptr_t)lv_event_get_user_data(event);
	uint8_t data[8];
	char text[32];
	int ret = 0;

	switch (action) {
	case ADDR_DOWN:
		address--;
		break;
	case ADDR_UP:
		address++;
		break;
	case VALUE_DOWN:
		value--;
		break;
	case VALUE_UP:
		value++;
		break;
	case PROBE:
	case READ_BYTE:
		ret = device_is_ready(eeprom) ? eeprom_read(eeprom, address, data, 1) : -ENODEV;
		if (ret == 0 && action == READ_BYTE) {
			lv_label_set_text_fmt(eeprom_dump, "Read: 0x%02X", data[0]);
		}
		break;
	case WRITE_BYTE:
		ret = device_is_ready(eeprom) ? eeprom_write(eeprom, address, &value, 1) : -ENODEV;
		break;
	case READ_EIGHT:
		ret = device_is_ready(eeprom) ? eeprom_read(eeprom, address, data, sizeof(data))
					      : -ENODEV;
		if (ret == 0) {
			snprintf(text, sizeof(text), "%02X %02X %02X %02X\n%02X %02X %02X %02X",
				 data[0], data[1], data[2], data[3], data[4], data[5], data[6],
				 data[7]);
			lv_label_set_text(eeprom_dump, text);
		}
		break;
	}
	lv_label_set_text_fmt(address_label, "0x%02X", address);
	lv_label_set_text_fmt(value_label, "0x%02X", value);
	if (action >= PROBE) {
		if (ret == 0) {
			lv_label_set_text(eeprom_status, "Status: OK");
		} else {
			lv_label_set_text_fmt(eeprom_status, "Status: error %d", ret);
		}
	}
}

static void make_button(lv_obj_t *parent, const char *text, int x, int y, int width,
			enum eeprom_action action)
{
	lv_obj_t *button = lv_button_create(parent);
	lv_obj_t *label = lv_label_create(button);

	lv_obj_set_pos(button, x, y);
	lv_obj_set_size(button, width, 30);
	lv_label_set_text(label, text);
	lv_obj_center(label);
	lv_obj_add_event_cb(button, eeprom_action_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)action);
}

static void create_ui(void)
{
	lv_obj_t *tabs = lv_tabview_create(lv_screen_active());
	lv_obj_t *welcome;
	lv_obj_t *gpio_page;
	lv_obj_t *voltage_page;
	lv_obj_t *eeprom_page;

	lv_tabview_set_tab_bar_size(tabs, 36);
	welcome = lv_tabview_add_tab(tabs, "Welcome");
	gpio_page = lv_tabview_add_tab(tabs, "GPIO");
	voltage_page = lv_tabview_add_tab(tabs, "Voltage");
	eeprom_page = lv_tabview_add_tab(tabs, "EEPROM");
	lv_obj_set_style_pad_all(lv_tabview_get_tab_bar(tabs), 0, 0);
	for (int index = 0; index < 4; index++) {
		lv_obj_set_style_pad_all(lv_tabview_get_tab_button(tabs, index), 2, 0);
	}

	make_label(welcome, "GD32F503V-EVAL", 4, 12);
	make_label(welcome, "Zephyr + LVGL", 4, 40);
	make_label(welcome, "Display: ILI9320 240x320", 4, 82);
	make_label(welcome, "Touch: XPT2046", 4, 110);
	make_label(welcome, "MCU: GD32F503VGT6", 4, 138);

	make_label(gpio_page, "LEDs  PC7 / PC8 / PC9", 4, 8);
	for (size_t index = 0; index < ARRAY_SIZE(leds); index++) {
		led_indicators[index] = lv_led_create(gpio_page);
		lv_obj_set_size(led_indicators[index], 24, 24);
		lv_obj_set_pos(led_indicators[index], 20 + index * 64, 34);
		lv_led_set_color(led_indicators[index], lv_color_hex(0x00a050));
		lv_led_off(led_indicators[index]);
	}
	make_label(gpio_page, "Buttons  PA0 / PA1 / PC13", 4, 88);
	for (size_t index = 0; index < ARRAY_SIZE(keys); index++) {
		key_labels[index] = make_label(gpio_page, key_names[index], 4, 116 + index * 28);
	}

	make_label(voltage_page, "Voltage Monitor", 4, 8);
	make_label(voltage_page, "Potentiometer (PC0)", 4, 42);
	voltage_label = make_label(voltage_page, "-- V", 4, 66);
	voltage_bar = lv_bar_create(voltage_page);
	lv_obj_set_pos(voltage_bar, 4, 96);
	lv_obj_set_size(voltage_bar, 180, 12);
	lv_bar_set_range(voltage_bar, 0, 3300);
	reference_label = make_label(voltage_page, "VDD: --", 4, 126);
	temperature_label = make_label(voltage_page, "MCU temp: --", 4, 158);
	make_label(voltage_page, "Vrefint: 1.2 V nominal", 4, 190);

	make_label(eeprom_page, "AT24C02C  I2C0", 4, 2);
	eeprom_status = make_label(eeprom_page, "Status: idle", 4, 24);
	make_button(eeprom_page, "Probe", 4, 48, 80, PROBE);
	make_label(eeprom_page, "Addr", 4, 93);
	make_button(eeprom_page, "-", 54, 86, 32, ADDR_DOWN);
	address_label = make_label(eeprom_page, "0x00", 94, 93);
	make_button(eeprom_page, "+", 150, 86, 32, ADDR_UP);
	make_label(eeprom_page, "Value", 4, 131);
	make_button(eeprom_page, "-", 54, 124, 32, VALUE_DOWN);
	value_label = make_label(eeprom_page, "0x00", 94, 131);
	make_button(eeprom_page, "+", 150, 124, 32, VALUE_UP);
	make_button(eeprom_page, "Read", 4, 164, 58, READ_BYTE);
	make_button(eeprom_page, "Write", 68, 164, 58, WRITE_BYTE);
	make_button(eeprom_page, "Read 8", 132, 164, 68, READ_EIGHT);
	eeprom_dump = make_label(eeprom_page, "--", 4, 208);
}

static void update_inputs(void)
{
	for (size_t index = 0; index < ARRAY_SIZE(keys); index++) {
		int pressed = gpio_pin_get_dt(&keys[index]);

		if (pressed < 0) {
			continue;
		}
		if ((bool)pressed == key_state[index]) {
			key_samples[index] = 0;
		} else if (++key_samples[index] >= 3) {
			key_state[index] = pressed;
			key_samples[index] = 0;
			printk("%s %s\n", key_names[index], pressed ? "pressed" : "released");
			lv_label_set_text_fmt(key_labels[index], "%s: %s", key_names[index],
					      pressed ? "PRESSED" : "released");
		}
	}
}

static void update_voltage(void)
{
	uint16_t raw;
	uint16_t raw_reference = 0;
	uint16_t raw_temperature = 0;
	struct adc_sequence sequence = {
		.buffer = &raw,
		.buffer_size = sizeof(raw),
	};
	int reference_mv = ADC_REFERENCE_MV;
	int ret = adc_sequence_init_dt(&potentiometer, &sequence);

	if (ret == 0) {
		ret = adc_read_dt(&potentiometer, &sequence);
	}
	if (ret < 0) {
		lv_label_set_text_fmt(voltage_label, "ADC error %d", ret);
		return;
	}
	if (internal_adc_ready) {
		sequence.channels = BIT(17);
		sequence.buffer = &raw_reference;
		ret = adc_read(potentiometer.dev, &sequence);
		if (ret == 0 && raw_reference > 0) {
			int measured_mv = 1200 * 4095 / raw_reference;

			if (measured_mv >= 1800 && measured_mv <= 3600) {
				reference_mv = measured_mv;
			}
		}
		sequence.channels = BIT(16);
		sequence.buffer = &raw_temperature;
		if (adc_read(potentiometer.dev, &sequence) == 0 && raw_temperature > 0) {
			int sensor_mv = raw_temperature * reference_mv / 4095;
			int temperature = 25 + (1550 - sensor_mv) * 100 / 447;

			lv_label_set_text_fmt(temperature_label, "MCU temp: %d C (est.)",
					      temperature);
		} else {
			lv_label_set_text(temperature_label, "MCU temp: unavailable");
		}
	}
	int millivolts = raw * reference_mv / 4095;

	lv_label_set_text_fmt(voltage_label, "%d.%03d V", millivolts / 1000, millivolts % 1000);
	lv_label_set_text_fmt(reference_label, "VDD: %d.%03d V (est.)", reference_mv / 1000,
			      reference_mv % 1000);
	lv_bar_set_value(voltage_bar, millivolts, LV_ANIM_OFF);
}

static int show_white(const struct device *display)
{
	uint16_t row[SCREEN_WIDTH];
	struct display_buffer_descriptor descriptor = {
		.buf_size = sizeof(row),
		.width = SCREEN_WIDTH,
		.height = 1,
		.pitch = SCREEN_WIDTH,
	};

	for (size_t index = 0; index < ARRAY_SIZE(row); index++) {
		row[index] = 0xffff;
	}
	for (uint16_t line = 0; line < SCREEN_HEIGHT; line++) {
		int ret = display_write(display, 0, line, &descriptor, row);

		if (ret < 0) {
			return ret;
		}
	}
	return display_blanking_off(display);
}

int main(void)
{
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	struct adc_channel_cfg internal_channel = {
		.gain = ADC_GAIN_1,
		.reference = ADC_REF_INTERNAL,
		.acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_TICKS, 240),
		.channel_id = 16,
	};
	int64_t next_led = 0;
	int64_t next_voltage = 0;

	printk("GD32F503 HMI starting\n");
	for (size_t index = 0; index < ARRAY_SIZE(leds); index++) {
		if (!gpio_is_ready_dt(&leds[index]) ||
		    gpio_pin_configure_dt(&leds[index], GPIO_OUTPUT_INACTIVE) < 0) {
			printk("LED %u initialization failed\n", (unsigned int)index);
			return 0;
		}
	}
	for (size_t index = 0; index < ARRAY_SIZE(keys); index++) {
		if (!gpio_is_ready_dt(&keys[index]) ||
		    gpio_pin_configure_dt(&keys[index], GPIO_INPUT) < 0) {
			printk("Key %u initialization failed\n", (unsigned int)index);
			return 0;
		}
	}
	if (!adc_is_ready_dt(&potentiometer) || adc_channel_setup_dt(&potentiometer) < 0) {
		printk("ADC initialization failed\n");
		return 0;
	}
	internal_adc_ready = adc_channel_setup(potentiometer.dev, &internal_channel) == 0;
	internal_channel.channel_id = 17;
	internal_adc_ready =
		adc_channel_setup(potentiometer.dev, &internal_channel) == 0 && internal_adc_ready;
	if (!device_is_ready(display) || show_white(display) < 0) {
		printk("Display initialization failed\n");
		return 0;
	}
	k_msleep(300);
	create_ui();
	printk("HMI ready\n");
	while (true) {
		int64_t now = k_uptime_get();

		if (now >= next_led) {
			for (size_t index = 0; index < ARRAY_SIZE(leds); index++) {
				bool on = index == active_led;

				gpio_pin_set_dt(&leds[index], on);
				if (on) {
					lv_led_on(led_indicators[index]);
				} else {
					lv_led_off(led_indicators[index]);
				}
			}
			active_led = (active_led + 1) % ARRAY_SIZE(leds);
			next_led = now + 300;
		}
		if (now >= next_voltage) {
			update_voltage();
			next_voltage = now + 500;
		}
		update_inputs();
		lv_timer_handler();
		k_msleep(10);
	}
	return 0;
}
