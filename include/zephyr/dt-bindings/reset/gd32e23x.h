/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief GD32E23x reset controller dt-bindings.
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_RESET_GD32E23X_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_RESET_GD32E23X_H_

#include "gd32-common.h"

/**
 * @name Register offsets
 * @{
 */

/** AHB reset register offset. */
#define GD32_AHBRST_OFFSET  0x28U
/** APB1 reset register offset. */
#define GD32_APB1RST_OFFSET 0x10U
/** APB2 reset register offset. */
#define GD32_APB2RST_OFFSET 0x0CU

/** @} */

/**
 * @name Reset definitions
 * @{
 */

/** GPIOA reset. */
#define GD32_RESET_GPIOA GD32_RESET_CONFIG(AHBRST, 17U)
/** GPIOB reset. */
#define GD32_RESET_GPIOB GD32_RESET_CONFIG(AHBRST, 18U)
/** GPIOC reset. */
#define GD32_RESET_GPIOC GD32_RESET_CONFIG(AHBRST, 19U)
/** GPIOF reset. */
#define GD32_RESET_GPIOF GD32_RESET_CONFIG(AHBRST, 22U)

/** TIMER2 reset. */
#define GD32_RESET_TIMER2  GD32_RESET_CONFIG(APB1RST, 1U)
/** TIMER5 reset. */
#define GD32_RESET_TIMER5  GD32_RESET_CONFIG(APB1RST, 4U)
/** TIMER13 reset. */
#define GD32_RESET_TIMER13 GD32_RESET_CONFIG(APB1RST, 8U)
/** WWDGT reset. */
#define GD32_RESET_WWDGT   GD32_RESET_CONFIG(APB1RST, 11U)
/** SPI1 reset. */
#define GD32_RESET_SPI1    GD32_RESET_CONFIG(APB1RST, 14U)
/** USART1 reset. */
#define GD32_RESET_USART1  GD32_RESET_CONFIG(APB1RST, 17U)
/** I2C0 reset. */
#define GD32_RESET_I2C0    GD32_RESET_CONFIG(APB1RST, 21U)
/** I2C1 reset. */
#define GD32_RESET_I2C1    GD32_RESET_CONFIG(APB1RST, 22U)
/** PMU reset. */
#define GD32_RESET_PMU     GD32_RESET_CONFIG(APB1RST, 28U)

/** CFGCMP reset. */
#define GD32_RESET_CFGCMP  GD32_RESET_CONFIG(APB2RST, 0U)
/** ADC reset. */
#define GD32_RESET_ADC     GD32_RESET_CONFIG(APB2RST, 9U)
/** TIMER0 reset. */
#define GD32_RESET_TIMER0  GD32_RESET_CONFIG(APB2RST, 11U)
/** SPI0 reset. */
#define GD32_RESET_SPI0    GD32_RESET_CONFIG(APB2RST, 12U)
/** USART0 reset. */
#define GD32_RESET_USART0  GD32_RESET_CONFIG(APB2RST, 14U)
/** TIMER14 reset. */
#define GD32_RESET_TIMER14 GD32_RESET_CONFIG(APB2RST, 16U)
/** TIMER15 reset. */
#define GD32_RESET_TIMER15 GD32_RESET_CONFIG(APB2RST, 17U)
/** TIMER16 reset. */
#define GD32_RESET_TIMER16 GD32_RESET_CONFIG(APB2RST, 18U)

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_RESET_GD32E23X_H_ */
