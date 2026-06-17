/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/cache.h>


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

	/* DTCM initialization */
	volatile uint32_t *p   = (volatile uint32_t *)DT_REG_ADDR(DT_CHOSEN(zephyr_dtcm));
	volatile uint32_t *end = (volatile uint32_t *)(
		DT_REG_ADDR(DT_CHOSEN(zephyr_dtcm)) + DT_REG_SIZE(DT_CHOSEN(zephyr_dtcm)));

	for (; p < end; p++) {
		*p = 0;
	}
}

void soc_early_init_hook(void)
{
	SystemInit();

	sys_cache_instr_enable();
#if defined(CONFIG_CACHE_MANAGEMENT) && defined(CONFIG_DCACHE)
	sys_cache_data_enable();
#endif
}
