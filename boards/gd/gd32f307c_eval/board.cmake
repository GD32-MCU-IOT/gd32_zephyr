# Copyright (c) 2026, GigaDevice Semiconductor Inc.
# SPDX-License-Identifier: Apache-2.0

board_runner_args(pyocd "--target=gd32f307vc" "--frequency=4000000" "--tool-opt=--pack=${ZEPHYR_HAL_GIGADEVICE_MODULE_DIR}/${CONFIG_SOC_SERIES}/support/GigaDevice.GD32F30x_DFP.2.6.0.pack")

include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)

board_runner_args(jlink "--device=GD32F307VC" "--speed=4000")
board_runner_args(gd32isp "--device=GD32F307VCT6")
include(${ZEPHYR_BASE}/boards/common/gd32isp.board.cmake)
