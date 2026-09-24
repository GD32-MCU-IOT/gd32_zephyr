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

#if defined(CONFIG_CLOCK_CONTROL_GD32_F5HC_PLL)

/* RCU_CFG0, RCU_PLL and RCU_PLLCFG reset values (user manual 6.7). */
#define GD32_RCU_CFG0_RESET   0x00009400U
#define GD32_RCU_PLL_RESET    0x00003010U
#define GD32_RCU_PLLCFG_RESET 0x00041400U

static void gd32f5hc_soft_delay(uint32_t count)
{
	for (volatile uint32_t i = 0U; i < count; i++) {
	}
}

/*
 * Step AHB down before leaving the PLL, so that Vcore does not fluctuate while
 * the system clock drops. Mirrors the RCU_MODIFY_DE_2() vendor sequence.
 */
static void gd32f5hc_ahb_step_down(void)
{
	uint32_t reg;

	gd32f5hc_soft_delay(0x50U);
	reg = RCU_CFG0;
	reg &= ~RCU_CFG0_AHBPSC;
	reg |= RCU_AHB_CKSYS_DIV2;
	RCU_CFG0 = reg;

	gd32f5hc_soft_delay(0x50U);
	reg = RCU_CFG0;
	reg &= ~RCU_CFG0_AHBPSC;
	reg |= RCU_AHB_CKSYS_DIV4;
	RCU_CFG0 = reg;

	gd32f5hc_soft_delay(0x50U);
}

/*
 * Return RCU to its IRC16M reset state so that the devicetree driven clock tree
 * is always built from a known baseline.
 */
static void gd32f5hc_rcu_reset(void)
{
	RCU_CTL |= RCU_CTL_IRC16MEN;
	while ((RCU_CTL & RCU_CTL_IRC16MSTB) == 0U) {
	}

	if (RCU_SCSS_PLLP == (RCU_CFG0 & (RCU_CFG0_SCSS | RCU_CFG0_SCSS_1))) {
		gd32f5hc_ahb_step_down();
	}

	gd32f5hc_soft_delay(200U);

	RCU_CFG0 &= ~(RCU_CFG0_SCS | RCU_CFG0_SCS_1);
	RCU_CTL &= ~(RCU_CTL_HXTALEN | RCU_CTL_CKMEN | RCU_CTL_PLLEN | RCU_CTL_RCUPRIP);
	RCU_CTL &= ~(RCU_CTL_PLLI2SEN | RCU_CTL_IRC48MEN);

	RCU_PLLCFG = GD32_RCU_PLLCFG_RESET;
	RCU_CFG0 = GD32_RCU_CFG0_RESET;
	RCU_CFG1 = 0x00000000U;
	RCU_ADDCTL = 0x00000000U;
	RCU_PLL = GD32_RCU_PLL_RESET;
	RCU_PLLSSCTL = 0x00000000U;
	RCU_INT = 0x00000000U;
}
#endif /* CONFIG_CLOCK_CONTROL_GD32_F5HC_PLL */

void soc_early_init_hook(void)
{
#if defined(CONFIG_CLOCK_CONTROL_GD32_F5HC_PLL)
	gd32f5hc_rcu_reset();
	SystemCoreClock = IRC16M_VALUE;
	ICACHE_CTL |= ICACHE_CTL_EN;
#else
	SystemInit();
#endif
}
