/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Reset line definitions for the GD32W51x_F5HC series.
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_RESET_GD32W51X_F5HC_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_RESET_GD32W51X_F5HC_H_

#include "gd32-common.h"

/**
 * @name Register offsets
 * @{
 */

#define GD32_AHB1RST_OFFSET 0x10U /**< AHB1RST register offset */
#define GD32_AHB2RST_OFFSET 0x14U /**< AHB2RST register offset */
#define GD32_AHB3RST_OFFSET 0x18U /**< AHB3RST register offset */
#define GD32_APB1RST_OFFSET 0x20U /**< APB1RST register offset */
#define GD32_APB2RST_OFFSET 0x24U /**< APB2RST register offset */

/** @} */

/**
 * @name Reset definitions for peripherals
 * @{
 */

/* AHB1 peripherals */
#define GD32_RESET_GPIOA GD32_RESET_CONFIG(AHB1RST, 0U)  /**< GPIOA reset line */
#define GD32_RESET_GPIOB GD32_RESET_CONFIG(AHB1RST, 1U)  /**< GPIOB reset line */
#define GD32_RESET_GPIOC GD32_RESET_CONFIG(AHB1RST, 2U)  /**< GPIOC reset line */
#define GD32_RESET_GPIOD GD32_RESET_CONFIG(AHB1RST, 3U)  /**< GPIOD reset line */
#define GD32_RESET_CRC   GD32_RESET_CONFIG(AHB1RST, 12U) /**< CRC reset line */
#define GD32_RESET_DMA0  GD32_RESET_CONFIG(AHB1RST, 21U) /**< DMA0 reset line */
#define GD32_RESET_DMA1  GD32_RESET_CONFIG(AHB1RST, 22U) /**< DMA1 reset line */
#define GD32_RESET_USBFS GD32_RESET_CONFIG(AHB1RST, 24U) /**< USBFS reset line */

/* AHB2 peripherals */
#define GD32_RESET_CAU   GD32_RESET_CONFIG(AHB2RST, 4U) /**< CAU reset line */
#define GD32_RESET_HAU   GD32_RESET_CONFIG(AHB2RST, 5U) /**< HAU reset line */
#define GD32_RESET_TRNG  GD32_RESET_CONFIG(AHB2RST, 6U) /**< TRNG reset line */
#define GD32_RESET_PKCAU GD32_RESET_CONFIG(AHB2RST, 7U) /**< PKCAU reset line */

/* AHB3 peripherals */
#define GD32_RESET_SQPI GD32_RESET_CONFIG(AHB3RST, 0U) /**< SQPI reset line */
#define GD32_RESET_QSPI GD32_RESET_CONFIG(AHB3RST, 1U) /**< QSPI reset line */

/* APB1 peripherals */
#define GD32_RESET_TIMER1 GD32_RESET_CONFIG(APB1RST, 0U)  /**< TIMER1 reset line */
#define GD32_RESET_TIMER2 GD32_RESET_CONFIG(APB1RST, 1U)  /**< TIMER2 reset line */
#define GD32_RESET_TIMER3 GD32_RESET_CONFIG(APB1RST, 2U)  /**< TIMER3 reset line */
#define GD32_RESET_TIMER4 GD32_RESET_CONFIG(APB1RST, 3U)  /**< TIMER4 reset line */
#define GD32_RESET_TIMER5 GD32_RESET_CONFIG(APB1RST, 4U)  /**< TIMER5 reset line */
#define GD32_RESET_WWDGT  GD32_RESET_CONFIG(APB1RST, 11U) /**< WWDGT reset line */
#define GD32_RESET_SPI1   GD32_RESET_CONFIG(APB1RST, 14U) /**< SPI1 reset line */
#define GD32_RESET_USART1 GD32_RESET_CONFIG(APB1RST, 17U) /**< USART1 reset line */
#define GD32_RESET_USART0 GD32_RESET_CONFIG(APB1RST, 18U) /**< USART0 reset line */
#define GD32_RESET_I2C0   GD32_RESET_CONFIG(APB1RST, 21U) /**< I2C0 reset line */
#define GD32_RESET_I2C1   GD32_RESET_CONFIG(APB1RST, 22U) /**< I2C1 reset line */
#define GD32_RESET_PMU    GD32_RESET_CONFIG(APB1RST, 28U) /**< PMU reset line */

/* APB2 peripherals */
#define GD32_RESET_TIMER0  GD32_RESET_CONFIG(APB2RST, 0U)  /**< TIMER0 reset line */
#define GD32_RESET_USART2  GD32_RESET_CONFIG(APB2RST, 4U)  /**< USART2 reset line */
#define GD32_RESET_ADC     GD32_RESET_CONFIG(APB2RST, 8U)  /**< ADC reset line */
#define GD32_RESET_SPI0    GD32_RESET_CONFIG(APB2RST, 12U) /**< SPI0 reset line */
#define GD32_RESET_SYSCFG  GD32_RESET_CONFIG(APB2RST, 14U) /**< SYSCFG reset line */
#define GD32_RESET_TIMER15 GD32_RESET_CONFIG(APB2RST, 17U) /**< TIMER15 reset line */
#define GD32_RESET_TIMER16 GD32_RESET_CONFIG(APB2RST, 18U) /**< TIMER16 reset line */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_RESET_GD32W51X_F5HC_H_ */
