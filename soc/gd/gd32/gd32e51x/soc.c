/*
 * Copyright (c) 2025, GigaDevice Semiconductor Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <soc.h>

void soc_early_init_hook(void)
{
	SystemInit();
}
