/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Quadrature decoder driver for GD32 timers
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_QDEC_GD32_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_QDEC_GD32_H_

#include <stdint.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set the quadrature decoder accumulated count.
 *
 * Presets the 32-bit accumulated position to @p count and resets the hardware
 * counter to 0. The requested value is clamped to the range that keeps
 * subsequent overflow adjustments and the 16-bit hardware counter within
 * int32_t. Later wraps and samples saturate at the same limits rather than
 * overflowing. The hardware counter is briefly stopped while it is reset;
 * encoder edges arriving during that interval are not counted. Callers must
 * preset the count only while the encoder is stationary.
 *
 * @param dev   QDEC GD32 device instance.
 * @param count Desired accumulated count; clamped if outside the safe range.
 *
 * @retval 0 Always succeeds.
 */
int qdec_gd32_set_count(const struct device *dev, int32_t count);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_QDEC_GD32_H_ */
