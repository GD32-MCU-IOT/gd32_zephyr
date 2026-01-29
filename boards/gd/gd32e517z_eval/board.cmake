# Copyright (c) 2025, GigaDevice Semiconductor Inc.
# SPDX-License-Identifier: Apache-2.0

board_runner_args(pyocd "--target=gd32e517ze")
board_runner_args(
  jlink
  "--device=GD32E517ZE" "--iface=jtag" "--tool-opt=-JTAGConf -1,-1"
)

include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
