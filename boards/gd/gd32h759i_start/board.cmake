# Copyright (c) 2026 GigaDevice Semiconductor Inc.
# SPDX-License-Identifier: Apache-2.0


board_runner_args(pyocd "--target=GD32H759IM" "--tool-opt=--pack=${ZEPHYR_HAL_GIGADEVICE_MODULE_DIR}/${CONFIG_SOC_SERIES}/support/GigaDevice.GD32H73x_75x_DFP.1.6.0.pack")

include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
