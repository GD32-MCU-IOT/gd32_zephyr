/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief GD32H7XX and GD32H75E ADC clock prescaler definitions
 *
 * Device Tree Bindings for GD32H7XX and GD32H75E ADC clock configurations.
 * These definitions refer to ADC_CLK_* values from the corresponding ADC HAL.
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_ADC_GD32H7XX_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_ADC_GD32H7XX_H_

/** Synchronous clock mode: ADC clock = HCLK / 2 */
#define GD32_ADC_CLK_SYNC_HCLK_DIV2  0x00080000
/** Synchronous clock mode: ADC clock = HCLK / 4 */
#define GD32_ADC_CLK_SYNC_HCLK_DIV4  0x00090000
/** Synchronous clock mode: ADC clock = HCLK / 6 */
#define GD32_ADC_CLK_SYNC_HCLK_DIV6  0x000A0000
/** Synchronous clock mode: ADC clock = HCLK / 8 */
#define GD32_ADC_CLK_SYNC_HCLK_DIV8  0x000B0000
/** Synchronous clock mode: ADC clock = HCLK / 10 */
#define GD32_ADC_CLK_SYNC_HCLK_DIV10 0x000C0000
/** Synchronous clock mode: ADC clock = HCLK / 12 */
#define GD32_ADC_CLK_SYNC_HCLK_DIV12 0x000D0000
/** Synchronous clock mode: ADC clock = HCLK / 14 */
#define GD32_ADC_CLK_SYNC_HCLK_DIV14 0x000E0000
/** Synchronous clock mode: ADC clock = HCLK / 16 */
#define GD32_ADC_CLK_SYNC_HCLK_DIV16 0x000F0000

/** Asynchronous clock mode: ADC clock = async_clk / 1 */
#define GD32_ADC_CLK_ASYNC_DIV1   0x00000000
/** Asynchronous clock mode: ADC clock = async_clk / 2 */
#define GD32_ADC_CLK_ASYNC_DIV2   0x00100000
/** Asynchronous clock mode: ADC clock = async_clk / 4 */
#define GD32_ADC_CLK_ASYNC_DIV4   0x00200000
/** Asynchronous clock mode: ADC clock = async_clk / 6 */
#define GD32_ADC_CLK_ASYNC_DIV6   0x00300000
/** Asynchronous clock mode: ADC clock = async_clk / 8 */
#define GD32_ADC_CLK_ASYNC_DIV8   0x00400000
/** Asynchronous clock mode: ADC clock = async_clk / 10 */
#define GD32_ADC_CLK_ASYNC_DIV10  0x00500000
/** Asynchronous clock mode: ADC clock = async_clk / 12 */
#define GD32_ADC_CLK_ASYNC_DIV12  0x00600000
/** Asynchronous clock mode: ADC clock = async_clk / 16 */
#define GD32_ADC_CLK_ASYNC_DIV16  0x00700000
/** Asynchronous clock mode: ADC clock = async_clk / 32 */
#define GD32_ADC_CLK_ASYNC_DIV32  0x00800000
/** Asynchronous clock mode: ADC clock = async_clk / 64 */
#define GD32_ADC_CLK_ASYNC_DIV64  0x00900000
/** Asynchronous clock mode: ADC clock = async_clk / 128 */
#define GD32_ADC_CLK_ASYNC_DIV128 0x00A00000
/** Asynchronous clock mode: ADC clock = async_clk / 256 */
#define GD32_ADC_CLK_ASYNC_DIV256 0x00B00000

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_ADC_GD32H7XX_H_ */
