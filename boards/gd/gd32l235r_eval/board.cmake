# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

set(gd32l23x_pack_dir "${ZEPHYR_HAL_GIGADEVICE_MODULE_DIR}/${CONFIG_SOC_SERIES}/support")

board_runner_args(pyocd
  "--target=GD32L235RB"
  "--frequency=4000000"
  "--tool-opt=--pack=${gd32l23x_pack_dir}/GigaDevice.GD32L23x_DFP.2.3.0.pack"
)
board_runner_args(jlink "--device=GD32L235RBT6" "--iface=SWD" "--speed=4000")

include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
