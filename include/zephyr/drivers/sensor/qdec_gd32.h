/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_QDEC_GD32_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_QDEC_GD32_H_

#include <stdint.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

int qdec_gd32_set_count(const struct device *dev, int32_t count);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_QDEC_GD32_H_ */
