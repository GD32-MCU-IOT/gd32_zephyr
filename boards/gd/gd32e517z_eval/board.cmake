# Copyright (c) 2026 GigaDevice Semiconductor Inc.
# SPDX-License-Identifier: Apache-2.0

board_runner_args(pyocd "--target=gd32e517ze" "--frequency=4000000" "--tool-opt=--pack=${ZEPHYR_HAL_GIGADEVICE_MODULE_DIR}/${CONFIG_SOC_SERIES}/support/GigaDevice.GD32E51x_DFP.1.5.0.pack")
board_runner_args(
  jlink
  "--device=GD32E517ZE" "--iface=jtag" "--tool-opt=-JTAGConf -1,-1" "--speed=4000"
)

include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
