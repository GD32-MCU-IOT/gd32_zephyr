# Copyright (c) 2026 GigaDevice Semiconductor Inc.
# SPDX-License-Identifier: Apache-2.0

board_runner_args(pyocd
  "--target=GD32E508ZE"
  "--frequency=4000000"
  "--tool-opt=--pack=${ZEPHYR_HAL_GIGADEVICE_MODULE_DIR}/${CONFIG_SOC_SERIES}/support/GigaDevice.GD32E50x_DFP.1.9.0.pack"
)
board_runner_args(
  jlink
  "--device=GD32E508ZE" "--iface=jtag" "--tool-opt=-JTAGConf -1,-1"
)

include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
