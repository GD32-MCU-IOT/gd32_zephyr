/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _SOC_ARM_GIGADEVICE_GD32E51X_SOC_H_
#define _SOC_ARM_GIGADEVICE_GD32E51X_SOC_H_

#ifndef _ASMLANGUAGE

#include <gd32e51x.h>
#include <gd32_usart.h>

static inline volatile uint32_t *gd32e51x_usart_data_tx(uint32_t usart_periph)
{
	if (usart_periph == USART5) {
		return &USART5_TDATA(usart_periph);
	}

	return &USART_DATA(usart_periph);
}

static inline volatile uint32_t *gd32e51x_usart_data_rx(uint32_t usart_periph)
{
	if (usart_periph == USART5) {
		return &USART5_RDATA(usart_periph);
	}

	return &USART_DATA(usart_periph);
}

static inline FlagStatus gd32e51x_usart_flag_get(uint32_t usart_periph,
						  usart_flag_enum flag)
{
	if (usart_periph != USART5) {
		return usart_flag_get(usart_periph, flag);
	}

	switch (flag) {
	case USART_FLAG_TBE:
		return usart5_flag_get(usart_periph, USART5_FLAG_TBE);
	case USART_FLAG_TC:
		return usart5_flag_get(usart_periph, USART5_FLAG_TC);
	case USART_FLAG_RBNE:
		return usart5_flag_get(usart_periph, USART5_FLAG_RBNE);
	case USART_FLAG_IDLE:
		return usart5_flag_get(usart_periph, USART5_FLAG_IDLE);
	case USART_FLAG_ORERR:
		return usart5_flag_get(usart_periph, USART5_FLAG_ORERR);
	case USART_FLAG_NERR:
		return usart5_flag_get(usart_periph, USART5_FLAG_NERR);
	case USART_FLAG_FERR:
		return usart5_flag_get(usart_periph, USART5_FLAG_FERR);
	case USART_FLAG_PERR:
		return usart5_flag_get(usart_periph, USART5_FLAG_PERR);
	default:
		return RESET;
	}
}

static inline void gd32e51x_usart_flag_clear(uint32_t usart_periph, usart_flag_enum flag)
{
	if (usart_periph != USART5) {
		usart_flag_clear(usart_periph, flag);
		return;
	}

	switch (flag) {
	case USART_FLAG_IDLE:
		usart5_flag_clear(usart_periph, USART5_FLAG_IDLE);
		break;
	case USART_FLAG_RBNE:
		usart5_flag_clear(usart_periph, USART5_FLAG_RBNE);
		break;
	case USART_FLAG_ORERR:
		usart5_flag_clear(usart_periph, USART5_FLAG_ORERR);
		break;
	case USART_FLAG_NERR:
		usart5_flag_clear(usart_periph, USART5_FLAG_NERR);
		break;
	case USART_FLAG_FERR:
		usart5_flag_clear(usart_periph, USART5_FLAG_FERR);
		break;
	case USART_FLAG_PERR:
		usart5_flag_clear(usart_periph, USART5_FLAG_PERR);
		break;
	default:
		break;
	}
}

static inline void gd32e51x_usart_interrupt_enable(uint32_t usart_periph,
						    usart_interrupt_enum interrupt)
{
	if (usart_periph != USART5) {
		usart_interrupt_enable(usart_periph, interrupt);
		return;
	}

	switch (interrupt) {
	case USART_INT_TC:
		usart5_interrupt_enable(usart_periph, USART5_INT_TC);
		break;
	case USART_INT_RBNE:
		usart5_interrupt_enable(usart_periph, USART5_INT_RBNE);
		break;
	case USART_INT_ERR:
		usart5_interrupt_enable(usart_periph, USART5_INT_ERR);
		break;
	case USART_INT_PERR:
		usart5_interrupt_enable(usart_periph, USART5_INT_PERR);
		break;
	case USART_INT_IDLE:
		usart5_interrupt_enable(usart_periph, USART5_INT_IDLE);
		break;
	default:
		break;
	}
}

static inline void gd32e51x_usart_interrupt_disable(uint32_t usart_periph,
						     usart_interrupt_enum interrupt)
{
	if (usart_periph != USART5) {
		usart_interrupt_disable(usart_periph, interrupt);
		return;
	}

	switch (interrupt) {
	case USART_INT_TC:
		usart5_interrupt_disable(usart_periph, USART5_INT_TC);
		break;
	case USART_INT_RBNE:
		usart5_interrupt_disable(usart_periph, USART5_INT_RBNE);
		break;
	case USART_INT_ERR:
		usart5_interrupt_disable(usart_periph, USART5_INT_ERR);
		break;
	case USART_INT_PERR:
		usart5_interrupt_disable(usart_periph, USART5_INT_PERR);
		break;
	case USART_INT_IDLE:
		usart5_interrupt_disable(usart_periph, USART5_INT_IDLE);
		break;
	default:
		break;
	}
}

static inline FlagStatus gd32e51x_usart_interrupt_flag_get(uint32_t usart_periph,
							    usart_interrupt_flag_enum int_flag)
{
	if (usart_periph != USART5) {
		return usart_interrupt_flag_get(usart_periph, int_flag);
	}

	switch (int_flag) {
	case USART_INT_FLAG_IDLE:
		return usart5_interrupt_flag_get(usart_periph, USART5_INT_FLAG_IDLE);
	case USART_INT_FLAG_RBNE:
		return usart5_interrupt_flag_get(usart_periph, USART5_INT_FLAG_RBNE);
	case USART_INT_FLAG_TC:
		return usart5_interrupt_flag_get(usart_periph, USART5_INT_FLAG_TC);
	default:
		return RESET;
	}
}

#define gd32_usart_flag_get           gd32e51x_usart_flag_get
#define gd32_usart_flag_clear         gd32e51x_usart_flag_clear
#define gd32_usart_interrupt_enable   gd32e51x_usart_interrupt_enable
#define gd32_usart_interrupt_disable  gd32e51x_usart_interrupt_disable
#define gd32_usart_interrupt_flag_get gd32e51x_usart_interrupt_flag_get

struct pinctrl_dev_config;

void gd32e51x_usart5_pinmux_quirk(uint32_t usart_periph,
				   const struct pinctrl_dev_config *pcfg);

#endif /* _ASMLANGUAGE */

#endif /* _SOC_ARM_GIGADEVICE_GD32E51X_SOC_H_ */
