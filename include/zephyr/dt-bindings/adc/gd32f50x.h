/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief GD32F50X ADC clock prescaler definitions
 *
 * Device Tree Bindings for GD32F50X ADC clock and prescaler configurations.
 * These definitions refer to hal\gigadevice\gd32f50x\standard_peripheral\include\gd32f50x_rcu.h.
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_ADC_GD32F50X_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_ADC_GD32F50X_H_

/** @brief ADC clock divided by 2 from APB2 */
#define GD32_RCU_ADCCK_APB2_DIV2  0x00000000
/** @brief ADC clock divided by 4 from APB2 */
#define GD32_RCU_ADCCK_APB2_DIV4  0x00000001
/** @brief ADC clock divided by 6 from APB2 */
#define GD32_RCU_ADCCK_APB2_DIV6  0x00000002
/** @brief ADC clock divided by 8 from APB2 */
#define GD32_RCU_ADCCK_APB2_DIV8  0x00000003
/** @brief ADC clock divided by 12 from APB2 */
#define GD32_RCU_ADCCK_APB2_DIV12 0x00000005
/** @brief ADC clock divided by 16 from APB2 */
#define GD32_RCU_ADCCK_APB2_DIV16 0x00000007
/** @brief ADC clock divided by 3 from AHB */
#define GD32_RCU_ADCCK_AHB_DIV3   0x00000008
/** @brief ADC clock divided by 5 from AHB */
#define GD32_RCU_ADCCK_AHB_DIV5   0x0000000C
/** @brief ADC clock divided by 6 from AHB */
#define GD32_RCU_ADCCK_AHB_DIV6   0x0000000D
/** @brief ADC clock divided by 10 from AHB */
#define GD32_RCU_ADCCK_AHB_DIV10  0x0000000E
/** @brief ADC clock divided by 20 from AHB */
#define GD32_RCU_ADCCK_AHB_DIV20  0x0000000F

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_ADC_GD32F50X_H_ */
