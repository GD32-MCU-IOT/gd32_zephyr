/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Clock gate definitions for the GD32W51x_F5HC series.
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_GD32W51X_F5HC_CLOCKS_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_GD32W51X_F5HC_CLOCKS_H_

#include "gd32-clocks-common.h"

/**
 * @name Register offsets
 * @{
 */

#define GD32_AHB1EN_OFFSET 0x30U /**< AHB1EN register offset */
#define GD32_AHB2EN_OFFSET 0x34U /**< AHB2EN register offset */
#define GD32_AHB3EN_OFFSET 0x38U /**< AHB3EN register offset */
#define GD32_APB1EN_OFFSET 0x40U /**< APB1EN register offset */
#define GD32_APB2EN_OFFSET 0x44U /**< APB2EN register offset */

/** @} */

/**
 * @name Clock enable/disable definitions for peripherals
 * @{
 */

/* AHB1 peripherals */
#define GD32_CLOCK_GPIOA GD32_CLOCK_CONFIG(AHB1EN, 0U)  /**< GPIOA clock gate */
#define GD32_CLOCK_GPIOB GD32_CLOCK_CONFIG(AHB1EN, 1U)  /**< GPIOB clock gate */
#define GD32_CLOCK_GPIOC GD32_CLOCK_CONFIG(AHB1EN, 2U)  /**< GPIOC clock gate */
#define GD32_CLOCK_GPIOD GD32_CLOCK_CONFIG(AHB1EN, 3U)  /**< GPIOD clock gate */
#define GD32_CLOCK_CRC   GD32_CLOCK_CONFIG(AHB1EN, 12U) /**< CRC clock gate */
#define GD32_CLOCK_DMA0  GD32_CLOCK_CONFIG(AHB1EN, 21U) /**< DMA0 clock gate */
#define GD32_CLOCK_DMA1  GD32_CLOCK_CONFIG(AHB1EN, 22U) /**< DMA1 clock gate */
#define GD32_CLOCK_USBFS GD32_CLOCK_CONFIG(AHB1EN, 24U) /**< USBFS clock gate */

/* AHB2 peripherals */
#define GD32_CLOCK_CAU   GD32_CLOCK_CONFIG(AHB2EN, 4U) /**< CAU clock gate */
#define GD32_CLOCK_HAU   GD32_CLOCK_CONFIG(AHB2EN, 5U) /**< HAU clock gate */
#define GD32_CLOCK_TRNG  GD32_CLOCK_CONFIG(AHB2EN, 6U) /**< TRNG clock gate */
#define GD32_CLOCK_PKCAU GD32_CLOCK_CONFIG(AHB2EN, 7U) /**< PKCAU clock gate */

/* AHB3 peripherals */
#define GD32_CLOCK_SQPI GD32_CLOCK_CONFIG(AHB3EN, 0U) /**< SQPI clock gate */
#define GD32_CLOCK_QSPI GD32_CLOCK_CONFIG(AHB3EN, 1U) /**< QSPI clock gate */

/* APB1 peripherals */
#define GD32_CLOCK_TIMER1 GD32_CLOCK_CONFIG(APB1EN, 0U)  /**< TIMER1 clock gate */
#define GD32_CLOCK_TIMER2 GD32_CLOCK_CONFIG(APB1EN, 1U)  /**< TIMER2 clock gate */
#define GD32_CLOCK_TIMER3 GD32_CLOCK_CONFIG(APB1EN, 2U)  /**< TIMER3 clock gate */
#define GD32_CLOCK_TIMER4 GD32_CLOCK_CONFIG(APB1EN, 3U)  /**< TIMER4 clock gate */
#define GD32_CLOCK_TIMER5 GD32_CLOCK_CONFIG(APB1EN, 4U)  /**< TIMER5 clock gate */
#define GD32_CLOCK_WWDGT  GD32_CLOCK_CONFIG(APB1EN, 11U) /**< WWDGT clock gate */
#define GD32_CLOCK_SPI1   GD32_CLOCK_CONFIG(APB1EN, 14U) /**< SPI1 clock gate */
#define GD32_CLOCK_USART1 GD32_CLOCK_CONFIG(APB1EN, 17U) /**< USART1 clock gate */
#define GD32_CLOCK_USART0 GD32_CLOCK_CONFIG(APB1EN, 18U) /**< USART0 clock gate */
#define GD32_CLOCK_I2C0   GD32_CLOCK_CONFIG(APB1EN, 21U) /**< I2C0 clock gate */
#define GD32_CLOCK_I2C1   GD32_CLOCK_CONFIG(APB1EN, 22U) /**< I2C1 clock gate */
#define GD32_CLOCK_PMU    GD32_CLOCK_CONFIG(APB1EN, 28U) /**< PMU clock gate */

/* APB2 peripherals */
#define GD32_CLOCK_TIMER0  GD32_CLOCK_CONFIG(APB2EN, 0U)  /**< TIMER0 clock gate */
#define GD32_CLOCK_USART2  GD32_CLOCK_CONFIG(APB2EN, 4U)  /**< USART2 clock gate */
#define GD32_CLOCK_ADC     GD32_CLOCK_CONFIG(APB2EN, 8U)  /**< ADC clock gate */
#define GD32_CLOCK_SPI0    GD32_CLOCK_CONFIG(APB2EN, 12U) /**< SPI0 clock gate */
#define GD32_CLOCK_SYSCFG  GD32_CLOCK_CONFIG(APB2EN, 14U) /**< SYSCFG clock gate */
#define GD32_CLOCK_TIMER15 GD32_CLOCK_CONFIG(APB2EN, 17U) /**< TIMER15 clock gate */
#define GD32_CLOCK_TIMER16 GD32_CLOCK_CONFIG(APB2EN, 18U) /**< TIMER16 clock gate */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_GD32W51X_F5HC_CLOCKS_H_ */
