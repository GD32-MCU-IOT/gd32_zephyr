/*
 * Copyright (c) 2026, GigaDevice Semiconductor Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief GD32F30x MCU series initialization code
 *
 * This module provides routines to initialize and support board-level
 * hardware for the GigaDevice GD32F30x SoC.
 */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <soc.h>

/**
 * @brief Perform basic hardware initialization at boot.
 *
 * This needs to be run from the very beginning.
 */
void soc_early_init_hook(void)
{
	SystemInit();
}
