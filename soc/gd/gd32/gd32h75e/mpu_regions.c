/*
 * Copyright (c) 2026 GigaDevice Semiconductor Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/devicetree.h>
#include <zephyr/linker/devicetree_regions.h>
#include <zephyr/arch/arm/mpu/arm_mpu_mem_cfg.h>

static const struct arm_mpu_region mpu_regions[] = {
	/* configure the MPU attributes for the entire 4GB area, Reserved, no access.
	 * This configuration is highly recommended to prevent Speculative Prefetching
	 * of external memory, which may cause CPU read locks and even system errors
	 * The subregions 0/1/2/7 are exempted (matching subregion_disable=0x87
	 * in the GD32H7 SDK sample):
	 *   0: 0x00000000-0x1FFFFFFF (Code/Flash)
	 *   1: 0x20000000-0x3FFFFFFF (SRAM)
	 *   2: 0x40000000-0x5FFFFFFF (on-chip peripheral bus)
	 *   7: 0xE0000000-0xFFFFFFFF (PPB: NVIC/SCB/SysTick/debug)
	 * Only subregions 3-6 (0x60000000-0xDFFFFFFF, external memory bus) are actually
	 *  denied here.
	 */
	MPU_REGION_ENTRY("BACKGROUND", 0x0,
		{(STRONGLY_ORDERED_SHAREABLE | REGION_4G | MPU_RASR_XN_Msk | NO_ACCESS_Msk |
		  SUB_REGION_0_DISABLED | SUB_REGION_1_DISABLED | SUB_REGION_2_DISABLED |
		  SUB_REGION_7_DISABLED)}),

	MPU_REGION_ENTRY("FLASH", CONFIG_FLASH_BASE_ADDRESS, REGION_FLASH_ATTR(REGION_FLASH_SIZE)),
	MPU_REGION_ENTRY("SRAM", CONFIG_SRAM_BASE_ADDRESS, REGION_RAM_ATTR(REGION_SRAM_SIZE)),
	/*
	 * System memory attributes inhibit the speculative fetch,
	 * preventing the RDSERR Flash error
	 */
	MPU_REGION_ENTRY(
		"SYSTEM", 0x1FF00000,
		{(STRONGLY_ORDERED_SHAREABLE | REGION_512K | MPU_RASR_XN_Msk | P_RW_U_NA_Msk)}),
};

const struct arm_mpu_config mpu_config = {
	.num_regions = ARRAY_SIZE(mpu_regions),
	.mpu_regions = mpu_regions,
};
