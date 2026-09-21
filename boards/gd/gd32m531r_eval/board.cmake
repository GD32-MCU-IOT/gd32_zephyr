# Copyright (c) 2026 GigaDevice Semiconductor Inc.
# SPDX-License-Identifier: Apache-2.0

board_runner_args(pyocd
  "--target=GD32M531RC"
  "--frequency=4000000"
  "--tool-opt=--pack=${ZEPHYR_HAL_GIGADEVICE_MODULE_DIR}/${CONFIG_SOC_SERIES}/support/GigaDevice.GD32M53x_DFP.1.0.0.pack"
  "--tool-opt=-O"
  "--tool-opt=connect_mode=halt"
  "--tool-opt=-O"
  "--tool-opt=reset_type=hw"
)

board_runner_args(jlink "--device=GD32M531RC" "--speed=4000")

include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
