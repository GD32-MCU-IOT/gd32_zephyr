/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>

_ASM_FILE_PROLOGUE

GTEXT(soc_early_reset_hook)

SECTION_FUNC(TEXT, soc_early_reset_hook)
    ldr r0, =CONFIG_SRAM_BASE_ADDRESS
    ldr r1, =(CONFIG_SRAM_BASE_ADDRESS + (CONFIG_SRAM_SIZE * 1024))
    movs r2, #0
1:
    str r2, [r0]
    adds r0, r0, #4
    cmp r0, r1
    blo 1b
    bx lr
