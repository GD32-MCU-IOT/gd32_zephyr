/* SPDX-License-Identifier: Apache-2.0 */

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
 * int32_t.
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
