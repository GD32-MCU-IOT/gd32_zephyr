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

#define SCREEN_WIDTH     480
#define SCREEN_HEIGHT    272
#define ADC_REFERENCE_MV 3300

/* LED1 (PF10) is the TLI DE signal, only LED2 (PA6) is available */
static const struct gpio_dt_spec leds[] = {
	GPIO_DT_SPEC_GET(DT_NODELABEL(led1), gpios),
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
static lv_obj_t *raw_label;
static lv_obj_t *address_label;
static lv_obj_t *value_label;
static lv_obj_t *eeprom_status;
static lv_obj_t *eeprom_dump;
static uint8_t address;
static uint8_t value;
static bool key_state[ARRAY_SIZE(keys)];
static uint8_t key_samples[ARRAY_SIZE(keys)];
static bool led_on;

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
	case READ_EIGHT: {
		size_t size = device_is_ready(eeprom) ? eeprom_get_size(eeprom) : 0;
		size_t len = address < size ? MIN(sizeof(data), size - address) : 0;
		int pos = 0;

		ret = len > 0 ? eeprom_read(eeprom, address, data, len) : -ENODEV;
		if (ret == 0) {
			for (size_t index = 0; index < len; index++) {
				pos += snprintf(&text[pos], sizeof(text) - pos, "%02X ",
						data[index]);
			}
			lv_label_set_text(eeprom_dump, text);
		}
		break;
	}
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
	lv_obj_set_size(button, width, 36);
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

	lv_tabview_set_tab_bar_size(tabs, 40);
	welcome = lv_tabview_add_tab(tabs, "Welcome");
	gpio_page = lv_tabview_add_tab(tabs, "GPIO");
	voltage_page = lv_tabview_add_tab(tabs, "Voltage");
	eeprom_page = lv_tabview_add_tab(tabs, "EEPROM");

	make_label(welcome, "GD32H759I-EVAL", 4, 8);
	make_label(welcome, "Zephyr + LVGL", 4, 36);
	make_label(welcome, "Display: TLI RGB 480x272", 4, 78);
	make_label(welcome, "Touch: GT911 capacitive", 4, 106);
	make_label(welcome, "MCU: GD32H759IMK6 (Cortex-M7)", 4, 134);

	make_label(gpio_page, "LED2  PA6", 4, 8);
	for (size_t index = 0; index < ARRAY_SIZE(leds); index++) {
		led_indicators[index] = lv_led_create(gpio_page);
		lv_obj_set_size(led_indicators[index], 28, 28);
		lv_obj_set_pos(led_indicators[index], 20 + index * 64, 40);
		lv_led_set_color(led_indicators[index], lv_color_hex(0x00a050));
		lv_led_off(led_indicators[index]);
	}
	make_label(gpio_page, "Keys  PA0 / PF8 / PC13", 220, 8);
	for (size_t index = 0; index < ARRAY_SIZE(keys); index++) {
		key_labels[index] = make_label(gpio_page, key_names[index], 220, 40 + index * 32);
	}

	make_label(voltage_page, "Voltage Monitor", 4, 8);
	make_label(voltage_page, "Potentiometer VR1 (PC2_C, ADC2_IN0)", 4, 44);
	voltage_label = make_label(voltage_page, "-- V", 4, 76);
	voltage_bar = lv_bar_create(voltage_page);
	lv_obj_set_pos(voltage_bar, 4, 108);
	lv_obj_set_size(voltage_bar, 400, 16);
	lv_bar_set_range(voltage_bar, 0, ADC_REFERENCE_MV);
	raw_label = make_label(voltage_page, "Raw: --", 4, 140);

	make_label(eeprom_page, "AT24C02  I2C1  0x50", 4, 2);
	eeprom_status = make_label(eeprom_page, "Status: idle", 240, 2);
	make_button(eeprom_page, "Probe", 4, 30, 90, PROBE);
	make_label(eeprom_page, "Addr", 4, 86);
	make_button(eeprom_page, "-", 64, 78, 40, ADDR_DOWN);
	address_label = make_label(eeprom_page, "0x00", 118, 86);
	make_button(eeprom_page, "+", 174, 78, 40, ADDR_UP);
	make_label(eeprom_page, "Value", 240, 86);
	make_button(eeprom_page, "-", 300, 78, 40, VALUE_DOWN);
	value_label = make_label(eeprom_page, "0x00", 354, 86);
	make_button(eeprom_page, "+", 410, 78, 40, VALUE_UP);
	make_button(eeprom_page, "Read", 4, 126, 90, READ_BYTE);
	make_button(eeprom_page, "Write", 104, 126, 90, WRITE_BYTE);
	make_button(eeprom_page, "Read 8", 204, 126, 100, READ_EIGHT);
	eeprom_dump = make_label(eeprom_page, "--", 4, 176);
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
	struct adc_sequence sequence = {
		.buffer = &raw,
		.buffer_size = sizeof(raw),
	};
	int ret = adc_sequence_init_dt(&potentiometer, &sequence);

	if (ret == 0) {
		ret = adc_read_dt(&potentiometer, &sequence);
	}
	if (ret < 0) {
		lv_label_set_text_fmt(voltage_label, "ADC error %d", ret);
		return;
	}

	int millivolts = raw * ADC_REFERENCE_MV / 4095;

	lv_label_set_text_fmt(voltage_label, "%d.%03d V", millivolts / 1000, millivolts % 1000);
	lv_label_set_text_fmt(raw_label, "Raw: %u / 4095", raw);
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
	int64_t next_led = 0;
	int64_t next_voltage = 0;

	printk("GD32H759 HMI starting\n");
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
			led_on = !led_on;
			for (size_t index = 0; index < ARRAY_SIZE(leds); index++) {
				gpio_pin_set_dt(&leds[index], led_on);
				if (led_on) {
					lv_led_on(led_indicators[index]);
				} else {
					lv_led_off(led_indicators[index]);
				}
			}
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
