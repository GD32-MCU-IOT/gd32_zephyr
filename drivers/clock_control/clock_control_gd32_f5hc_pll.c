/*
 * Copyright (c) 2026, GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT gd_gd32f5hc_rcu

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <gd32_pmu.h>
#include <gd32_rcu.h>

#define GD32_PLL_NODE    DT_NODELABEL(pll)
#define GD32_PLLI2S_NODE DT_NODELABEL(plli2s)
#define GD32_RCU_NODE    DT_NODELABEL(rcu)

/* PLL input source selection from the pll node clocks phandle. */
#if DT_NODE_HAS_COMPAT(DT_CLOCKS_CTLR(GD32_PLL_NODE), gd_gd32_hxtal_clock)
#define GD32_PLL_SRC (RCU_PLLSRC_HXTAL)
#elif DT_NODE_HAS_COMPAT(DT_CLOCKS_CTLR(GD32_PLL_NODE), gd_gd32f5hc_irc48m_clock)
#define GD32_PLL_SRC (RCU_PLLSRC_IRC48M)
#else
#define GD32_PLL_SRC (RCU_PLLSRC_IRC16M)
#endif

/* System clock source selection from the rcu node clocks phandle. */
#if DT_NODE_HAS_COMPAT(DT_CLOCKS_CTLR(GD32_RCU_NODE), gd_gd32f5hc_pll_clock)
#define GD32_SYSCLK_FROM_PLL 1
#define GD32_CKSYS_SRC       (RCU_CKSYSSRC_PLLP)
#define GD32_CKSYS_SCSS      (RCU_SCSS_PLLP)
#elif DT_NODE_HAS_COMPAT(DT_CLOCKS_CTLR(GD32_RCU_NODE), gd_gd32_hxtal_clock)
#define GD32_SYSCLK_FROM_PLL 0
#define GD32_CKSYS_SRC       (RCU_CKSYSSRC_HXTAL)
#define GD32_CKSYS_SCSS      (RCU_SCSS_HXTAL)
#elif DT_NODE_HAS_COMPAT(DT_CLOCKS_CTLR(GD32_RCU_NODE), gd_gd32f5hc_irc48m_clock)
#define GD32_SYSCLK_FROM_PLL 0
#define GD32_CKSYS_SRC       (RCU_CKSYSSRC_IRC48M)
#define GD32_CKSYS_SCSS      (RCU_SCSS_IRC48M)
#else
#define GD32_SYSCLK_FROM_PLL 0
#define GD32_CKSYS_SRC       (RCU_CKSYSSRC_IRC16M)
#define GD32_CKSYS_SCSS      (RCU_SCSS_IRC16M)
#endif

/* HXTAL is needed when it feeds the system clock directly or through the PLL. */
#define GD32_NEEDS_HXTAL                                                                           \
	(DT_NODE_HAS_COMPAT(DT_CLOCKS_CTLR(GD32_RCU_NODE), gd_gd32_hxtal_clock) ||                 \
	 (GD32_SYSCLK_FROM_PLL &&                                                                  \
	  DT_NODE_HAS_COMPAT(DT_CLOCKS_CTLR(GD32_PLL_NODE), gd_gd32_hxtal_clock)))

/* IRC48M is needed when it feeds the system clock directly or through the PLL. */
#define GD32_NEEDS_IRC48M                                                                          \
	(DT_NODE_HAS_COMPAT(DT_CLOCKS_CTLR(GD32_RCU_NODE), gd_gd32f5hc_irc48m_clock) ||            \
	 (GD32_SYSCLK_FROM_PLL &&                                                                  \
	  DT_NODE_HAS_COMPAT(DT_CLOCKS_CTLR(GD32_PLL_NODE), gd_gd32f5hc_irc48m_clock)))

#define GD32_IRC16M_FREQ 16000000U

/* Clock tree frequencies derived at build time for the sanity checks below. */
#define GD32_PLL_IN_FREQ                                                                           \
	DT_PROP_OR(DT_CLOCKS_CTLR(GD32_PLL_NODE), clock_frequency, GD32_IRC16M_FREQ)
#define GD32_PLL_PSC      DT_PROP_OR(GD32_PLL_NODE, pll_psc, 1)
#define GD32_PLL_N        DT_PROP_OR(GD32_PLL_NODE, pll_n, 1)
#define GD32_PLL_P        DT_PROP_OR(GD32_PLL_NODE, pll_p, 1)
#define GD32_VCO_SRC_FREQ (GD32_PLL_IN_FREQ / GD32_PLL_PSC)
#define GD32_VCO_FREQ     (GD32_VCO_SRC_FREQ * GD32_PLL_N)
#define GD32_PLLP_FREQ    (GD32_VCO_FREQ / GD32_PLL_P)

#if GD32_SYSCLK_FROM_PLL
#define GD32_SYSCLK_FREQ GD32_PLLP_FREQ
#else
#define GD32_SYSCLK_FREQ                                                                           \
	DT_PROP_OR(DT_CLOCKS_CTLR(GD32_RCU_NODE), clock_frequency, GD32_IRC16M_FREQ)
#endif

#define GD32_HCLK_FREQ DT_PROP(GD32_RCU_NODE, clock_frequency)
#define GD32_AHB_PSC   DT_PROP(GD32_RCU_NODE, ahb_prescaler)
#define GD32_APB1_PSC  DT_PROP(GD32_RCU_NODE, apb1_prescaler)
#define GD32_APB2_PSC  DT_PROP(GD32_RCU_NODE, apb2_prescaler)

BUILD_ASSERT((GD32_HCLK_FREQ * GD32_AHB_PSC) == GD32_SYSCLK_FREQ,
	     "rcu clock-frequency does not match the selected clock source divided by "
	     "ahb-prescaler");
BUILD_ASSERT(GD32_HCLK_FREQ <= 200000000, "GD32F5HC HCLK must not exceed 200 MHz");
BUILD_ASSERT(GD32_HCLK_FREQ / GD32_APB1_PSC <= 50000000, "GD32F5HC PCLK1 must not exceed 50 MHz");
BUILD_ASSERT(GD32_HCLK_FREQ / GD32_APB2_PSC <= 100000000, "GD32F5HC PCLK2 must not exceed 100 MHz");

#if GD32_SYSCLK_FROM_PLL
BUILD_ASSERT(GD32_VCO_SRC_FREQ >= 1000000 && GD32_VCO_SRC_FREQ <= 2000000,
	     "GD32F5HC PLL VCO source clock must be within 1 MHz .. 2 MHz");
BUILD_ASSERT(GD32_VCO_FREQ >= 64000000 && GD32_VCO_FREQ <= 500000000,
	     "GD32F5HC PLL VCO clock must be within 64 MHz .. 500 MHz");
#endif

#if DT_PROP(GD32_PLL_NODE, spread_spectrum)
#define GD32_SS_FREQ   DT_PROP(GD32_PLL_NODE, ss_modulation_freq)
#define GD32_SS_DEPTH  DT_PROP(GD32_PLL_NODE, ss_modulation_depth)
/* MODCNT = round(f(VCO source) / 4 / f(modulation)) */
#define GD32_SS_MODCNT ((GD32_VCO_SRC_FREQ + 2 * GD32_SS_FREQ) / (4 * GD32_SS_FREQ))
/* MODSTEP = round(depth[%] * PLLN * 2^14 / (MODCNT * 100)), depth given in 0.1 % */
#define GD32_SS_MODSTEP                                                                            \
	(((GD32_SS_DEPTH * GD32_PLL_N * 16384) + (500 * GD32_SS_MODCNT)) / (1000 * GD32_SS_MODCNT))

#if DT_ENUM_HAS_VALUE(GD32_PLL_NODE, ss_type, down)
#define GD32_SS_TYPE (RCU_SS_TYPE_DOWN)
#else
#define GD32_SS_TYPE (RCU_SS_TYPE_CENTER)
#endif

BUILD_ASSERT((GD32_SS_MODSTEP * GD32_SS_MODCNT) <= 65535,
	     "GD32F5HC spread spectrum requires MODSTEP * MODCNT <= 65535");
#endif /* spread-spectrum */

#if DT_NODE_HAS_STATUS(GD32_PLLI2S_NODE, okay)
#define GD32_PLLI2S_PSC_VAL  DT_PROP(GD32_PLLI2S_NODE, plli2s_psc)
#define GD32_PLLI2S_N_VAL    DT_PROP(GD32_PLLI2S_NODE, plli2s_n)
#define GD32_PLLI2S_DIV_VAL  DT_PROP(GD32_PLLI2S_NODE, plli2s_div)
#define GD32_PLLI2S_SRC_FREQ (GD32_PLL_IN_FREQ / GD32_PLLI2S_PSC_VAL)
#define GD32_PLLI2S_VCO_FREQ (GD32_PLLI2S_SRC_FREQ * GD32_PLLI2S_N_VAL)

/* PLLSEL is programmed by rcu_pll_config(), which only runs when SYSCLK uses the main PLL. */
BUILD_ASSERT(GD32_SYSCLK_FROM_PLL, "GD32F5HC plli2s requires the main pll as SYSCLK source");
BUILD_ASSERT(GD32_PLLI2S_PSC_VAL >= 1 && GD32_PLLI2S_PSC_VAL <= 8,
	     "GD32F5HC plli2s-psc must be within 1 .. 8");
BUILD_ASSERT(GD32_PLLI2S_N_VAL >= 8 && GD32_PLLI2S_N_VAL <= 127,
	     "GD32F5HC plli2s-n must be within 8 .. 127");
BUILD_ASSERT(GD32_PLLI2S_DIV_VAL == 0 || (GD32_PLLI2S_DIV_VAL >= 3 && GD32_PLLI2S_DIV_VAL <= 63),
	     "GD32F5HC plli2s-div must be 0 or within 3 .. 63");
BUILD_ASSERT(GD32_PLLI2S_SRC_FREQ >= 2000000 && GD32_PLLI2S_SRC_FREQ <= 16000000,
	     "GD32F5HC PLLI2S VCO source clock must be within 2 MHz .. 16 MHz");
BUILD_ASSERT(GD32_PLLI2S_VCO_FREQ <= 550000000,
	     "GD32F5HC PLLI2S VCO clock must not exceed 550 MHz");
#endif /* plli2s */

struct gd32_pll_config {
	uint32_t pll_psc;
	uint32_t pll_n;
	uint32_t pll_p;
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

/* Map divider value to HAL APB1 prescaler encoding. Dividers below 4 are reserved. */
static uint32_t gd32_apb1_psc_to_reg(uint16_t psc)
{
	switch (psc) {
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

/* Software delay matching the one the vendor reset sequence inserts around AHB changes. */
static void gd32_soft_delay(void)
{
	for (volatile uint32_t i = 0U; i < 0x50U; i++) {
	}
}

/* Vendor sequence keeps AHB divided while SYSCLK switches, to limit Vcore fluctuations. */
static void gd32_ahb_psc_set(uint32_t ahb_psc)
{
	uint32_t reg = RCU_CFG0;

	reg &= ~RCU_CFG0_AHBPSC;
	reg |= ahb_psc;
	RCU_CFG0 = reg;
}

static int gd32_pll_init(const struct device *dev)
{
	const struct gd32_pll_config *config = dev->config;
	const uint32_t ahb_psc = gd32_ahb_psc_to_reg(config->ahb_psc);
	const uint32_t apb1_psc = gd32_apb1_psc_to_reg(config->apb1_psc);
	const uint32_t apb2_psc = gd32_apb2_psc_to_reg(config->apb2_psc);
	uint32_t timeout;
	uint32_t reg;

	if ((ahb_psc == UINT32_MAX) || (apb1_psc == UINT32_MAX) || (apb2_psc == UINT32_MAX)) {
		return -EINVAL;
	}

#if GD32_NEEDS_HXTAL
	/* Enable HXTAL and wait until it is stable. */
	RCU_CTL |= RCU_CTL_HXTALEN;
	timeout = 0U;
	while ((RCU_CTL & RCU_CTL_HXTALSTB) == 0U) {
		if (++timeout == HXTAL_STARTUP_TIMEOUT) {
			return -EIO;
		}
	}
#endif

#if GD32_NEEDS_IRC48M
	/* Enable IRC48M and wait until it is stable. */
	RCU_CTL |= RCU_CTL_IRC48MEN;
	timeout = 0U;
	while ((RCU_CTL & RCU_CTL_IRC48MSTB) == 0U) {
		if (++timeout == HXTAL_STARTUP_TIMEOUT) {
			return -EIO;
		}
	}
#endif

	/* Enable the PMU clock and select the LDO output voltage. */
	rcu_periph_clock_enable(RCU_PMU);
	PMU_CTL0 |= PMU_CTL0_LDOVS;

	/* Run AHB divided by 2 until the system clock switch is complete. */
	gd32_ahb_psc_set(RCU_AHB_CKSYS_DIV2);
	rcu_apb1_clock_config(apb1_psc);
	rcu_apb2_clock_config(apb2_psc);

#if GD32_SYSCLK_FROM_PLL
#if DT_PROP(GD32_PLL_NODE, spread_spectrum)
	/* Must be programmed while the PLL is off, and before rcu_pll_config() which
	 * validates PLLN against the active modulation type.
	 */
	rcu_spread_spectrum_config(GD32_SS_TYPE, GD32_SS_MODSTEP, GD32_SS_MODCNT);
	rcu_spread_spectrum_enable();
#else
	rcu_spread_spectrum_disable();
#endif

	if (rcu_pll_config(GD32_PLL_SRC, config->pll_psc, config->pll_n, config->pll_p) !=
	    SUCCESS) {
		return -EINVAL;
	}

	RCU_CTL |= RCU_CTL_PLLEN;
	timeout = 0U;
	while ((RCU_CTL & RCU_CTL_PLLSTB) == 0U) {
		if (++timeout == HXTAL_STARTUP_TIMEOUT) {
			return -EIO;
		}
	}
#endif /* GD32_SYSCLK_FROM_PLL */

	/* Select the system clock source. SCS spans RCU_CFG0 bits 1:0 and bit 8. */
	reg = RCU_CFG0;
	reg &= ~(RCU_CFG0_SCS | RCU_CFG0_SCS_1);
	reg |= GD32_CKSYS_SRC;
	RCU_CFG0 = reg;

	timeout = 0U;
	while (GD32_CKSYS_SCSS != (RCU_CFG0 & (RCU_CFG0_SCSS | RCU_CFG0_SCSS_1))) {
		if (++timeout == HXTAL_STARTUP_TIMEOUT) {
			return -EIO;
		}
	}

	/* Ramp AHB up to the requested prescaler now that SYSCLK is settled. */
	gd32_soft_delay();
	gd32_ahb_psc_set(ahb_psc);
	gd32_soft_delay();

#if DT_NODE_HAS_STATUS(GD32_PLLI2S_NODE, okay)
	if (rcu_plli2s_config(DT_PROP(GD32_PLLI2S_NODE, plli2s_n),
			      PLLCFG_PLLI2SPSC(DT_PROP(GD32_PLLI2S_NODE, plli2s_psc) - 1U),
			      PLLCFG_PLLI2SDIV(DT_PROP(GD32_PLLI2S_NODE, plli2s_div))) != SUCCESS) {
		return -EINVAL;
	}

	RCU_CTL |= RCU_CTL_PLLI2SEN;
	timeout = 0U;
	while ((RCU_CTL & RCU_CTL_PLLI2SSTB) == 0U) {
		if (++timeout == HXTAL_STARTUP_TIMEOUT) {
			return -EIO;
		}
	}
#endif

	SystemCoreClockUpdate();

	return 0;
}

static const struct gd32_pll_config gd32_pll_config = {
	.pll_psc = GD32_PLL_PSC,
	.pll_n = GD32_PLL_N,
	.pll_p = GD32_PLL_P,
	.ahb_psc = GD32_AHB_PSC,
	.apb1_psc = GD32_APB1_PSC,
	.apb2_psc = GD32_APB2_PSC,
};

DEVICE_DT_DEFINE(GD32_RCU_NODE, gd32_pll_init, NULL, NULL, &gd32_pll_config, PRE_KERNEL_1,
		 CONFIG_CLOCK_CONTROL_INIT_PRIORITY, NULL);
