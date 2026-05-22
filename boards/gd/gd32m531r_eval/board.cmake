# Copyright (c) 2026 GigaDevice Semiconductor Inc.
# SPDX-License-Identifier: Apache-2.0

# DFP pack is maintained in the HAL module tree, not duplicated per-board.
file(GLOB GD32M531_DFP_PACK
  "${ZEPHYR_HAL_GIGADEVICE_MODULE_DIR}/gd32m53x/support/GigaDevice.GD32M53x_DFP.*.pack"
)
if(GD32M531_DFP_PACK)
  list(SORT GD32M531_DFP_PACK)
  list(GET GD32M531_DFP_PACK -1 GD32M531_DFP_PACK)
endif()

board_runner_args(pyocd
  "--target=GD32M531RC"
  "--frequency=4000000"
  "--tool-opt=--pack=${GD32M531_DFP_PACK}"
  "--tool-opt=-O"
  "--tool-opt=connect_mode=halt"
  "--tool-opt=-O"
  "--tool-opt=reset_type=hw"
)

board_runner_args(jlink "--device=GD32M531RC" "--speed=4000")

include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
