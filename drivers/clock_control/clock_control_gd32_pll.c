/*
 * Copyright (c) 2026, GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT gd_gd32f527_rcu

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/kernel.h>

#include <gd32_pmu.h>
#include <gd32_rcu.h>

/* Main PLL node. */
#define GD32_PLL_NODE DT_NODELABEL(pll)

/* PLL input source selection from the pll node clocks phandle. */
#if DT_NODE_HAS_COMPAT(DT_CLOCKS_CTLR(GD32_PLL_NODE), gd_gd32_hxtal_clock)
#define GD32_PLL_SRC RCU_PLLSRC_HXTAL
#else
#define GD32_PLL_SRC RCU_PLLSRC_IRC16M
#endif

/* System clock source selection from the rcu node clocks phandle. */
#define GD32_RCU_NODE DT_NODELABEL(rcu)

#if DT_NODE_HAS_COMPAT(DT_CLOCKS_CTLR(GD32_RCU_NODE), gd_gd32_pll_clock)
#define GD32_CKSYS_SRC  RCU_CKSYSSRC_PLLP
#define GD32_CKSYS_SCSS RCU_SCSS_PLLP
#elif DT_NODE_HAS_COMPAT(DT_CLOCKS_CTLR(GD32_RCU_NODE), gd_gd32_hxtal_clock)
#define GD32_CKSYS_SRC  RCU_CKSYSSRC_HXTAL
#define GD32_CKSYS_SCSS RCU_SCSS_HXTAL
#else
#define GD32_CKSYS_SRC  RCU_CKSYSSRC_IRC16M
#define GD32_CKSYS_SCSS RCU_SCSS_IRC16M
#endif

struct gd32_pll_config {
	uint32_t pll_psc;
	uint32_t pll_n;
	uint32_t pll_p;
	uint32_t pll_q;
	uint16_t ahb_psc;
	uint16_t apb1_psc;
	uint16_t apb2_psc;
};

/* Map divider value to HAL AHB prescaler encoding. */
static uint32_t gd32_ahb_psc_to_reg(uint16_t psc)
{
	switch (psc) {
	case 1U:
		return RCU_AHB_CKSYS_DIV1;
	case 2U:
		return RCU_AHB_CKSYS_DIV2;
	case 4U:
		return RCU_AHB_CKSYS_DIV4;
	case 8U:
		return RCU_AHB_CKSYS_DIV8;
	case 16U:
		return RCU_AHB_CKSYS_DIV16;
	case 64U:
		return RCU_AHB_CKSYS_DIV64;
	case 128U:
		return RCU_AHB_CKSYS_DIV128;
	case 256U:
		return RCU_AHB_CKSYS_DIV256;
	case 512U:
		return RCU_AHB_CKSYS_DIV512;
	default:
		return UINT32_MAX; /* invalid, not a valid encoding */
	}
}

/* Map divider value to HAL APB1 prescaler encoding. */
static uint32_t gd32_apb1_psc_to_reg(uint16_t psc)
{
	switch (psc) {
	case 1U:
		return RCU_APB1_CKAHB_DIV1;
	case 2U:
		return RCU_APB1_CKAHB_DIV2;
	case 4U:
		return RCU_APB1_CKAHB_DIV4;
	case 8U:
		return RCU_APB1_CKAHB_DIV8;
	case 16U:
		return RCU_APB1_CKAHB_DIV16;
	default:
		return UINT32_MAX; /* invalid, not a valid encoding */
	}
}

/* Map divider value to HAL APB2 prescaler encoding. */
static uint32_t gd32_apb2_psc_to_reg(uint16_t psc)
{
	switch (psc) {
	case 1U:
		return RCU_APB2_CKAHB_DIV1;
	case 2U:
		return RCU_APB2_CKAHB_DIV2;
	case 4U:
		return RCU_APB2_CKAHB_DIV4;
	case 8U:
		return RCU_APB2_CKAHB_DIV8;
	case 16U:
		return RCU_APB2_CKAHB_DIV16;
	default:
		return UINT32_MAX; /* invalid, not a valid encoding */
	}
}

static int gd32_pll_init(const struct device *dev)
{
	const struct gd32_pll_config *config = dev->config;
	const uint32_t ahb_psc = gd32_ahb_psc_to_reg(config->ahb_psc);
	const uint32_t apb1_psc = gd32_apb1_psc_to_reg(config->apb1_psc);
	const uint32_t apb2_psc = gd32_apb2_psc_to_reg(config->apb2_psc);
	uint32_t timeout = 0U;
	uint32_t stab_flag = 0U;

	if ((ahb_psc == UINT32_MAX) || (apb1_psc == UINT32_MAX) || (apb2_psc == UINT32_MAX)) {
		return -EINVAL;
	}

	/* Enable HXTAL when the system clock or the PLL input needs it. */
	if ((GD32_CKSYS_SRC == RCU_CKSYSSRC_HXTAL) || (GD32_PLL_SRC == RCU_PLLSRC_HXTAL)) {
		/* Enable HXTAL and wait until it is stable. */
		RCU_CTL |= RCU_CTL_HXTALEN;
		timeout = 0U;
		stab_flag = 0U;
		do {
			timeout++;
			stab_flag = (RCU_CTL & RCU_CTL_HXTALSTB);
		} while ((0U == stab_flag) && (HXTAL_STARTUP_TIMEOUT != timeout));

		if (0U == (RCU_CTL & RCU_CTL_HXTALSTB)) {
			return -EIO;
		}
	}

	/* Enable the PMU clock and configure the LDO output voltage. */
	rcu_periph_clock_enable(RCU_PMU);
	PMU_CTL |= PMU_CTL_LDOVS;

	/* Configure AHB, APB1 and APB2 prescalers. */
	rcu_ahb_clock_config(ahb_psc);
	rcu_apb1_clock_config(apb1_psc);
	rcu_apb2_clock_config(apb2_psc);

	/* Configure and start the PLL only when it drives the system clock. */
	if (GD32_CKSYS_SRC == RCU_CKSYSSRC_PLLP) {
		/* Configure the main PLL. */
		if (rcu_pll_config(GD32_PLL_SRC, config->pll_psc, config->pll_n, config->pll_p,
				   config->pll_q) != SUCCESS) {
			return -EINVAL;
		}

		/* Enable PLL and wait until it is stable. */
		RCU_CTL |= RCU_CTL_PLLEN;
		timeout = 0U;
		while ((RCU_CTL & RCU_CTL_PLLSTB) == 0U) {
			if (++timeout == HXTAL_STARTUP_TIMEOUT) {
				return -EIO;
			}
		}

		/* Enable high-driver mode to extend the clock to 200 MHz. */
		PMU_CTL |= PMU_CTL_HDEN;
		timeout = 0U;
		while ((PMU_CS & PMU_CS_HDRF) == 0U) {
			if (++timeout == HXTAL_STARTUP_TIMEOUT) {
				return -EIO;
			}
		}

		/* Switch to high-driver mode. */
		PMU_CTL |= PMU_CTL_HDS;
		timeout = 0U;
		while ((PMU_CS & PMU_CS_HDSRF) == 0U) {
			if (++timeout == HXTAL_STARTUP_TIMEOUT) {
				return -EIO;
			}
		}
	}

	/* Select the system clock source and wait until it is in effect. */
	rcu_system_clock_source_config(GD32_CKSYS_SRC);
	timeout = 0U;
	while (GD32_CKSYS_SCSS != (RCU_CFG0 & RCU_CFG0_SCSS)) {
		if (++timeout == HXTAL_STARTUP_TIMEOUT) {
			return -EIO;
		}
	}

	/* Update SystemCoreClock from the actual SYSCLK/AHB configuration. */
	SystemCoreClockUpdate();

	return 0;
}

static const struct gd32_pll_config gd32_pll_config = {
	.pll_psc = DT_PROP(GD32_PLL_NODE, pll_psc),
	.pll_n = DT_PROP(GD32_PLL_NODE, pll_n),
	.pll_p = DT_PROP(GD32_PLL_NODE, pll_p),
	.pll_q = DT_PROP(GD32_PLL_NODE, pll_q),
	.ahb_psc = DT_PROP(GD32_RCU_NODE, ahb_prescaler),
	.apb1_psc = DT_PROP(GD32_RCU_NODE, apb1_prescaler),
	.apb2_psc = DT_PROP(GD32_RCU_NODE, apb2_prescaler),
};

DEVICE_DT_DEFINE(DT_NODELABEL(rcu), gd32_pll_init, NULL, NULL, &gd32_pll_config, PRE_KERNEL_1,
		 CONFIG_CLOCK_CONTROL_INIT_PRIORITY, NULL);
