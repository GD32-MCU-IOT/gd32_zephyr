/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_GD32E23X_CLOCKS_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_GD32E23X_CLOCKS_H_

#include "gd32-clocks-common.h"

/**
 * @name Register offsets
 * @{
 */

/** AHB enable register offset. */
#define GD32_AHBEN_OFFSET  0x14U
/** APB1 enable register offset. */
#define GD32_APB1EN_OFFSET 0x1CU
/** APB2 enable register offset. */
#define GD32_APB2EN_OFFSET 0x18U

/** @} */

/**
 * @name Clock enable/disable definitions for peripherals
 * @{
 */

/** DMA clock. */
#define GD32_CLOCK_DMA    GD32_CLOCK_CONFIG(AHBEN, 0U)
/** SRAM scratch pad clock. */
#define GD32_CLOCK_SRAMSP GD32_CLOCK_CONFIG(AHBEN, 2U)
/** FMC self programming clock. */
#define GD32_CLOCK_FMCSP  GD32_CLOCK_CONFIG(AHBEN, 4U)
/** CRC clock. */
#define GD32_CLOCK_CRC    GD32_CLOCK_CONFIG(AHBEN, 6U)
/** GPIOA clock. */
#define GD32_CLOCK_GPIOA  GD32_CLOCK_CONFIG(AHBEN, 17U)
/** GPIOB clock. */
#define GD32_CLOCK_GPIOB  GD32_CLOCK_CONFIG(AHBEN, 18U)
/** GPIOC clock. */
#define GD32_CLOCK_GPIOC  GD32_CLOCK_CONFIG(AHBEN, 19U)
/** GPIOF clock. */
#define GD32_CLOCK_GPIOF  GD32_CLOCK_CONFIG(AHBEN, 22U)

/** TIMER2 clock. */
#define GD32_CLOCK_TIMER2  GD32_CLOCK_CONFIG(APB1EN, 1U)
/** TIMER5 clock. */
#define GD32_CLOCK_TIMER5  GD32_CLOCK_CONFIG(APB1EN, 4U)
/** TIMER13 clock. */
#define GD32_CLOCK_TIMER13 GD32_CLOCK_CONFIG(APB1EN, 8U)
/** WWDGT clock. */
#define GD32_CLOCK_WWDGT   GD32_CLOCK_CONFIG(APB1EN, 11U)
/** SPI1 clock. */
#define GD32_CLOCK_SPI1    GD32_CLOCK_CONFIG(APB1EN, 14U)
/** USART1 clock. */
#define GD32_CLOCK_USART1  GD32_CLOCK_CONFIG(APB1EN, 17U)
/** I2C0 clock. */
#define GD32_CLOCK_I2C0    GD32_CLOCK_CONFIG(APB1EN, 21U)
/** I2C1 clock. */
#define GD32_CLOCK_I2C1    GD32_CLOCK_CONFIG(APB1EN, 22U)
/** PMU clock. */
#define GD32_CLOCK_PMU     GD32_CLOCK_CONFIG(APB1EN, 28U)

/** CFGCMP clock. */
#define GD32_CLOCK_CFGCMP  GD32_CLOCK_CONFIG(APB2EN, 0U)
/** ADC clock. */
#define GD32_CLOCK_ADC     GD32_CLOCK_CONFIG(APB2EN, 9U)
/** TIMER0 clock. */
#define GD32_CLOCK_TIMER0  GD32_CLOCK_CONFIG(APB2EN, 11U)
/** SPI0 clock. */
#define GD32_CLOCK_SPI0    GD32_CLOCK_CONFIG(APB2EN, 12U)
/** USART0 clock. */
#define GD32_CLOCK_USART0  GD32_CLOCK_CONFIG(APB2EN, 14U)
/** TIMER14 clock. */
#define GD32_CLOCK_TIMER14 GD32_CLOCK_CONFIG(APB2EN, 16U)
/** TIMER15 clock. */
#define GD32_CLOCK_TIMER15 GD32_CLOCK_CONFIG(APB2EN, 17U)
/** TIMER16 clock. */
#define GD32_CLOCK_TIMER16 GD32_CLOCK_CONFIG(APB2EN, 18U)
/** DBGMCU clock. */
#define GD32_CLOCK_DBGMCU  GD32_CLOCK_CONFIG(APB2EN, 22U)

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_GD32E23X_CLOCKS_H_ */
