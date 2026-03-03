/*
 * Copyright (c) 2026, GigaDevice Semiconductor Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file SoC configuration header for the GD32F30x SoC series.
 */

#ifndef ZEPHYR_SOC_GD_GD32_GD32F30X_SOC_H_
#define ZEPHYR_SOC_GD_GD32_GD32F30X_SOC_H_

#include <zephyr/sys/util.h>

#ifndef _ASMLANGUAGE
#include <gd32f30x.h>

/* The GigaDevice HAL headers define this, but it conflicts with the Zephyr can.h */
#undef CAN_MODE_NORMAL

#endif /* !_ASMLANGUAGE */

#endif /* ZEPHYR_SOC_GD_GD32_GD32F30X_SOC_H_ */
