# Copyright (c) 2026, GigaDevice Semiconductor Inc.
# SPDX-License-Identifier: Apache-2.0

include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)

board_runner_args(jlink "--device=GD32F307VG" "--speed=4000")
board_runner_args(gd32isp "--device=GD32F307VGT6")
include(${ZEPHYR_BASE}/boards/common/gd32isp.board.cmake)
