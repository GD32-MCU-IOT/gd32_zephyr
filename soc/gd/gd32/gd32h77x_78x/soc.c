/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/cache.h>

/*
 * Initialize ECC SRAM. SRAM content and ECC stay valid across warm resets,
 * so only do it after a power-on/brown-out reset; this keeps __noinit data
 * (e.g. watchdog test state) across system/watchdog resets. The reset flags
 * are cleared afterwards so that later warm resets are not mistaken for a
 * power-on reset.
 */
void soc_reset_hook(void)
{
	if ((RCU_RSTSCK & (RCU_RSTSCK_PORRSTF | RCU_RSTSCK_BORRSTF)) == 0U) {
		return;
	}

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

	/* nodelabels sram1/sram2 are the hardware SRAM0/SRAM1 */
	p = (volatile uint32_t *)DT_REG_ADDR(DT_NODELABEL(sram1));
	end = (volatile uint32_t *)(DT_REG_ADDR(DT_NODELABEL(sram1)) +
				    DT_REG_SIZE(DT_NODELABEL(sram1)));
	for (; p < end; p++) {
		*p = 0;
	}

	p = (volatile uint32_t *)DT_REG_ADDR(DT_NODELABEL(sram2));
	end = (volatile uint32_t *)(DT_REG_ADDR(DT_NODELABEL(sram2)) +
				    DT_REG_SIZE(DT_NODELABEL(sram2)));
	for (; p < end; p++) {
		*p = 0;
	}

	RCU_RSTSCK |= RCU_RSTSCK_RSTFC;
}

void soc_early_init_hook(void)
{
	SystemInit();

	sys_cache_instr_enable();
#if defined(CONFIG_CACHE_MANAGEMENT) && defined(CONFIG_DCACHE)
	sys_cache_data_enable();
#endif
}
