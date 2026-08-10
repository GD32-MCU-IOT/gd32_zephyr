/*
 * Copyright (c) 2021 Teslabs Engineering S.L.
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT gd_gd32_pwm

#include <errno.h>

#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/gd32.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/sys/util_macro.h>

#include <gd32_timer.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pwm_gd32, CONFIG_PWM_LOG_LEVEL);

#if defined(CONFIG_SOC_SERIES_GD32F50X)
/* GD32F50x HAL renamed TIMER_CCHP to TIMER_CCHP0 */
#define TIMER_CCHP      TIMER_CCHP0
#define TIMER_CCHP_POEN TIMER_CCHP0_POEN
#endif

/** PWM data. */
struct pwm_gd32_data {
	/** Timer clock (Hz). */
	uint32_t tim_clk;
};

/** PWM configuration. */
struct pwm_gd32_config {
	/** Timer register. */
	uint32_t reg;
	/** Number of channels */
	uint8_t channels;
	/** Flag to indicate if timer has 32-bit counter */
	bool is_32bit;
	/** Flag to indicate if timer is advanced */
	bool is_advanced;
	/** Prescaler. */
	uint16_t prescaler;
#if defined(CONFIG_SOC_SERIES_GD32H7XX) || defined(CONFIG_SOC_SERIES_GD32H75E)
	/** aligned mode selection. */
	uint16_t alignedmode;
	/** counter direction selection. */
	uint16_t counterdirection;
	/** set deadtime of channels. */
	uint8_t deadtime;
#endif
	/** Clock id. */
	uint16_t clkid;
	/** Reset. */
	struct reset_dt_spec reset;
	/** pinctrl configurations. */
	const struct pinctrl_dev_config *pcfg;
};

#if defined(CONFIG_SOC_SERIES_GD32H7XX) || defined(CONFIG_SOC_SERIES_GD32H75E)
#define TIMER_MAX_CH 4u
#endif

/** Obtain channel enable bit for the given channel */
#define TIMER_CHCTL2_CHXEN(ch) BIT(4U * (ch))
/** Obtain polarity bit for the given channel */
#define TIMER_CHCTL2_CHXP(ch)  BIT(1U + (4U * (ch)))
#if defined(CONFIG_SOC_SERIES_GD32H7XX) || defined(CONFIG_SOC_SERIES_GD32H75E)
/** Obtain channel enable bit for the given channel */
#define TIMER_CHCTL2_MCHXEN(ch) BIT(2U + (4U * (ch)))
/** Obtain polarity bit for complementary output of the given channel */
#define TIMER_CHCTL2_MCHXP(ch)  BIT(3U + (4U * (ch)))
#endif
/** Obtain CHCTL0/1 mask for the given channel (0 or 1) */
#define TIMER_CHCTLX_MSK(ch) (0xFU << (8U * (ch)))

/** Obtain RCU register offset from RCU clock value */
#define RCU_CLOCK_OFFSET(rcu_clock) ((rcu_clock) >> 6U)

static int pwm_gd32_set_cycles(const struct device *dev, uint32_t channel, uint32_t period_cycles,
			       uint32_t pulse_cycles, pwm_flags_t flags)
{
	const struct pwm_gd32_config *config = dev->config;
#if defined(CONFIG_SOC_SERIES_GD32H7XX) || defined(CONFIG_SOC_SERIES_GD32H75E)
	if (channel >= TIMER_MAX_CH) {
		return -EINVAL;
	}
#else
	if (channel >= config->channels) {
		return -EINVAL;
	}
#endif
	/* 16-bit timers can count up to UINT16_MAX */
	if (!config->is_32bit && period_cycles > UINT16_MAX) {
		return -ENOTSUP;
	}

	/* disable channel output if period is zero */
	if (period_cycles == 0U) {
		TIMER_CHCTL2(config->reg) &= ~TIMER_CHCTL2_CHXEN(channel);
		return 0;
	}

	/* update polarity */
	if ((flags & PWM_POLARITY_INVERTED) != 0U) {
		TIMER_CHCTL2(config->reg) |= TIMER_CHCTL2_CHXP(channel);
#if defined(TIMER_MCH_0)
		TIMER_CHCTL2(config->reg) |= TIMER_CHCTL2_MCHXP(channel);
#endif /* TIMER_MCH_0 */
	} else {
		TIMER_CHCTL2(config->reg) &= ~TIMER_CHCTL2_CHXP(channel);
#if defined(TIMER_MCH_0)
		TIMER_CHCTL2(config->reg) &= ~TIMER_CHCTL2_MCHXP(channel);
#endif /* TIMER_MCH_0 */
	}
#if defined(CONFIG_SOC_SERIES_GD32H7XX) || defined(CONFIG_SOC_SERIES_GD32H75E)
	if (config->alignedmode == TIMER_COUNTER_EDGE) {
		/* remove 1 period cycle */
		period_cycles -= 1U;
	} else {
		pulse_cycles /= 2U;
		period_cycles /= 2U;
	}
#endif
	/* update pulse */
	switch (channel) {
	case 0U:
		TIMER_CH0CV(config->reg) = pulse_cycles;
		break;
	case 1U:
		TIMER_CH1CV(config->reg) = pulse_cycles;
		break;
	case 2U:
		TIMER_CH2CV(config->reg) = pulse_cycles;
		break;
	case 3U:
		TIMER_CH3CV(config->reg) = pulse_cycles;
		break;
	default:
		__ASSERT_NO_MSG(NULL);
		break;
	}

	/* update period */
	TIMER_CAR(config->reg) = period_cycles;

	/* channel not enabled: configure it */
	if ((TIMER_CHCTL2(config->reg) & TIMER_CHCTL2_CHXEN(channel)) == 0U) {
		volatile uint32_t *chctl;

		/* select PWM0 mode, enable OC shadowing */
		if (channel < 2U) {
			chctl = &TIMER_CHCTL0(config->reg);
		} else {
			chctl = &TIMER_CHCTL1(config->reg);
		}

		*chctl &= ~TIMER_CHCTLX_MSK(channel);
		*chctl |= (TIMER_OC_MODE_PWM0 | TIMER_OC_SHADOW_ENABLE) << (8U * (channel % 2U));

		/* enable channel output */
		TIMER_CHCTL2(config->reg) |= TIMER_CHCTL2_CHXEN(channel);
#if defined(TIMER_MCH_0)
		TIMER_CHCTL2(config->reg) |= TIMER_CHCTL2_MCHXEN(channel);
		/* enable the auto reload shadow function */
		TIMER_CTL0(config->reg) |= TIMER_CTL0_ARSE;
		/* set deadtime */
		TIMER_CCHP(config->reg) |= (uint32_t)(config->deadtime);
#endif /* TIMER_MCH_0 */
		/* generate update event (to load shadow values) */
		TIMER_SWEVG(config->reg) |= TIMER_SWEVG_UPG;
	}

	return 0;
}

static int pwm_gd32_get_cycles_per_sec(const struct device *dev, uint32_t channel, uint64_t *cycles)
{
	struct pwm_gd32_data *data = dev->data;
	const struct pwm_gd32_config *config = dev->config;

	*cycles = (uint64_t)(data->tim_clk / (config->prescaler + 1U));

	return 0;
}

static DEVICE_API(pwm, pwm_gd32_driver_api) = {
	.set_cycles = pwm_gd32_set_cycles,
	.get_cycles_per_sec = pwm_gd32_get_cycles_per_sec,
};

static int pwm_gd32_init(const struct device *dev)
{
	const struct pwm_gd32_config *config = dev->config;
	struct pwm_gd32_data *data = dev->data;
	int ret;
#if defined(CONFIG_SOC_SERIES_GD32H7XX) || defined(CONFIG_SOC_SERIES_GD32H75E)
	timer_parameter_struct timer_initpara;
#endif

	(void)clock_control_on(GD32_CLOCK_CONTROLLER, (clock_control_subsys_t *)&config->clkid);

	(void)reset_line_toggle_dt(&config->reset);

	/* apply pin configuration */
	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		return ret;
	}

	/* cache timer clock value */
	(void)clock_control_get_rate(GD32_CLOCK_CONTROLLER,
				     (clock_control_subsys_t *)&config->clkid, &data->tim_clk);
#if defined(CONFIG_SOC_SERIES_GD32H7XX) || defined(CONFIG_SOC_SERIES_GD32H75E)
	/* initialize timer */
	timer_struct_para_init(&timer_initpara);

	timer_initpara.prescaler = config->prescaler;
	timer_initpara.alignedmode = config->alignedmode;
	timer_initpara.counterdirection = config->counterdirection;
	timer_initpara.period = 0u;
	timer_initpara.clockdivision = TIMER_CKDIV_DIV1;
	timer_initpara.repetitioncounter = 0;
	gd32_timer_init(config->reg, &timer_initpara);
#else
	/* basic timer operation: edge aligned, up counting, shadowed CAR */
	TIMER_CTL0(config->reg) =
		TIMER_CKDIV_DIV1 | TIMER_COUNTER_EDGE | TIMER_COUNTER_UP | TIMER_CTL0_ARSE;
	TIMER_PSC(config->reg) = config->prescaler;
#endif

	/* enable primary output for advanced timers */
	if (config->is_advanced) {
#if defined(TIMER_CTL2)
		/*
		 * Clear CTL2 to disable dead-time insertion/break for all channels.
		 * Only advanced timers have this register; residual DTIEN/BRKEN bits
		 * can suppress output even when POEN is enabled.
		 */
		TIMER_CTL2(config->reg) = 0x00000000U;
#endif
		TIMER_CCHP(config->reg) |= TIMER_CCHP_POEN;
	}

	/* enable timer counter */
	TIMER_CTL0(config->reg) |= TIMER_CTL0_CEN;

	return 0;
}

#if defined(CONFIG_SOC_SERIES_GD32H7XX) || defined(CONFIG_SOC_SERIES_GD32H75E)
#define PWM_GD32_DEFINE(i)                                                                         \
	static struct pwm_gd32_data pwm_gd32_data_##i;                                             \
                                                                                                   \
	PINCTRL_DT_INST_DEFINE(i);                                                                 \
                                                                                                   \
	static const struct pwm_gd32_config pwm_gd32_config_##i = {                                \
		.reg = DT_REG_ADDR(DT_INST_PARENT(i)),                                             \
		.clkid = DT_CLOCKS_CELL(DT_INST_PARENT(i), id),                                    \
		.reset = RESET_DT_SPEC_GET(DT_INST_PARENT(i)),                                     \
		.prescaler = DT_PROP(DT_INST_PARENT(i), prescaler),                                \
		.channels = DT_PROP(DT_INST_PARENT(i), channels),                                  \
		.alignedmode = DT_PROP(DT_INST_PARENT(i), alignedmode),                            \
		.counterdirection = DT_PROP(DT_INST_PARENT(i), counterdirection),                  \
		.deadtime = DT_PROP(DT_INST_PARENT(i), deadtime),                                  \
		.is_32bit = DT_PROP(DT_INST_PARENT(i), is_32bit),                                  \
		.is_advanced = DT_PROP(DT_INST_PARENT(i), is_advanced),                            \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(i),                                         \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(i, &pwm_gd32_init, NULL, &pwm_gd32_data_##i, &pwm_gd32_config_##i,   \
			      POST_KERNEL, CONFIG_PWM_INIT_PRIORITY, &pwm_gd32_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PWM_GD32_DEFINE)
#else
#define PWM_GD32_DEFINE(i)                                                                         \
	static struct pwm_gd32_data pwm_gd32_data_##i;                                             \
                                                                                                   \
	PINCTRL_DT_INST_DEFINE(i);                                                                 \
                                                                                                   \
	static const struct pwm_gd32_config pwm_gd32_config_##i = {                                \
		.reg = DT_REG_ADDR(DT_INST_PARENT(i)),                                             \
		.clkid = DT_CLOCKS_CELL(DT_INST_PARENT(i), id),                                    \
		.reset = RESET_DT_SPEC_GET(DT_INST_PARENT(i)),                                     \
		.prescaler = DT_PROP(DT_INST_PARENT(i), prescaler),                                \
		.channels = DT_PROP(DT_INST_PARENT(i), channels),                                  \
		.is_32bit = DT_PROP(DT_INST_PARENT(i), is_32bit),                                  \
		.is_advanced = DT_PROP(DT_INST_PARENT(i), is_advanced),                            \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(i),                                         \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(i, &pwm_gd32_init, NULL, &pwm_gd32_data_##i, &pwm_gd32_config_##i,   \
			      POST_KERNEL, CONFIG_PWM_INIT_PRIORITY, &pwm_gd32_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PWM_GD32_DEFINE)
#endif
