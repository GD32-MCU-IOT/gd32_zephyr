# Copyright (c) 2026 GigaDevice Semiconductor Inc.
# SPDX-License-Identifier: Apache-2.0

board_runner_args(pyocd "--target=GD32G553QE" "--frequency=4000000" "--tool-opt=--pack=${ZEPHYR_HAL_GIGADEVICE_MODULE_DIR}/${CONFIG_SOC_SERIES}/support/GigaDevice.GD32G5x3_DFP.1.3.0.pack")
board_runner_args(jlink "--device=GD32G553QE" "--speed=4000")

include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
