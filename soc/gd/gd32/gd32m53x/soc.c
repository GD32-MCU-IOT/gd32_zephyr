/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <soc.h>

/* initial ecc memory */
void soc_reset_hook(void)
{
#if defined(__ICCARM__)
	register uint32_t r0 = DT_REG_ADDR(DT_CHOSEN(zephyr_sram));
	register uint32_t r1 =
		DT_REG_ADDR(DT_CHOSEN(zephyr_sram)) + DT_REG_SIZE(DT_CHOSEN(zephyr_sram));
#else
	register uint32_t r0 __asm__("r0") = DT_REG_ADDR(DT_CHOSEN(zephyr_sram));
	register uint32_t r1 __asm__("r1") =
		DT_REG_ADDR(DT_CHOSEN(zephyr_sram)) + DT_REG_SIZE(DT_CHOSEN(zephyr_sram));
#endif

	for (; r0 < r1; r0 += 4) {
		*(volatile uint32_t *)r0 = 0;
	}
}

void soc_early_init_hook(void)
{
	SystemInit();
}

#ifdef CONFIG_GPIO_GD32

/*
 * GD32M53x pin->EXTI mapping is non-linear. Table [compact_port][pin] encodes
 * ((exti_line << 4) | extiss_value); 0xFF = no mapping. compact_port: A..G->0..6, N->7.
 * Source: gd32m53x_syscfg.h exti_gpio_enum.
 */
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

static int m53x_port_compact(uint8_t port_index)
{
	if (port_index <= 6U) {
		return (int)port_index;
	}
	if (port_index == 16U) {
		return 7; /* GPION */
	}
	return -1;
}

uint8_t gd32m53x_exti_encode_get(uint32_t gpio_base, uint8_t pin)
{
	uint8_t port_index = (gpio_base - GPIOA) / (GPIOB - GPIOA);
	int cp = m53x_port_compact(port_index);

	if (cp < 0 || pin >= 16U) {
		return GD32M53X_EXTI_INVALID;
	}

	return m53x_exti_map[cp][pin];
}

#endif /* CONFIG_GPIO_GD32 */
