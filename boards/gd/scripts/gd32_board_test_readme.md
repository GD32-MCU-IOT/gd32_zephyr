<!--
Copyright (c) 2025 Gigadevice Semiconductor Inc.

SPDX-License-Identifier: Apache-2.0
-->

# GD32 开发板测试指南

本文档介绍如何为 Zephyr RTOS 中的 GD32 开发板添加测试支持，以及如何进行本地编译测试。

## 目录

- [1. 概述](#1-概述)
- [2. 添加新开发板支持](#2-添加新开发板支持)
- [3. 本地测试方法](#3-本地测试方法)

---

## 1. 概述

### 1.1 测试框架架构

GD32 测试框架由以下组件构成：

```
boards/gd/scripts/
├── gd32_peripheral_matrix.yaml      # 外设矩阵配置（核心）
├── gd32_local_tester.py             # 本地测试运行器
└── gd32_board_test_readme.md        # 本文档

.github/workflows/
└── gd32_ci.yaml                      # GitHub CI/CD 工作流
```

### 1.2 外设矩阵配置文件

`gd32_peripheral_matrix.yaml` 是测试框架的核心配置文件，包含：

- **test_groups**: CI 测试组定义
  - 用于按功能分组测试
  - 定义每个测试组对应的板子和测试

---

## 2. 添加新开发板支持

### 2.1 以 GD32F527I-EVAL 为例

假设你要添加一个新的 GD32F527I-EVAL 开发板，步骤如下：

#### 步骤 1: 确定开发板信息

首先收集以下信息，确定新适配开发板支持外设：
- **系列**: gd32f5
- **架构**: ARM Cortex-M33
- **支持的外设**: UART, GPIO, I2C, SPI, EEPROM
- **可用的测试用例**: 基础测试、驱动测试、子系统测试

#### 步骤 2: 更新外设矩阵配置文件
在 `boards/gd/scripts/gd32_peripheral_matrix.yaml` 中添加新板子的配置；
对于适配支持UART，SPI，I2C基线外设的开发板，需要在eeprom，spi_flash，uart，shell等 `test_groups` 中添加。
下面以eeprom举例：

```yaml
test_groups:
  # ... 其他测试组 ...

  # EEPROM测试组
  eeprom:
    description: "EEPROM功能测试"
    tests:
      - samples/drivers/eeprom
    boards:
      - gd32e103v_eval
      - gd32f450i_eval
      - gd32f470i_eval
      - gd32f527i_eval      # 添加新板子
```

需要注意：对于RAM小于16K的板子（如GD32C231系列），不要添加shell测试，否则会出现失败。

---

## 3. 本地测试方法

### 3.1 环境准备

```bash
# 1. 进入 Zephyr 工作区
cd /path/to/zephyrproject/zephyr

# 2. 激活 Zephyr 环境
source ../.venv/bin/activate

# 3. 设置环境变量
source zephyr-env.sh

# 4. 确认 west 可用
west --version
```

### 3.2 测试方法:

#### 测试单个示例

```bash
# 在所有 GD32 板子上测试 blinky
python3 boards/gd/scripts/gd32_local_tester.py \
  -T samples/basic/blinky \
  -j 4

# 在指定板子上测试 blinky
python3 boards/gd/scripts/gd32_local_tester.py \
  -T samples/basic/blinky \
  -p gd32f527i_eval

# 在指定板子上测试 eeprom
python3 boards/gd/scripts/gd32_local_tester.py \
  -T samples/drivers/eeprom \
  -p gd32f527i_eval

```

#### 测试整个目录

```bash
# 测试所有 UART 驱动示例
python3 boards/gd/scripts/gd32_local_tester.py \
  -T samples/drivers/uart \
  -p gd32f527i_eval \
  -j 4
```

#### 测试注意事项
- 提交之前需要先验证所有开发板的基本功能（samples/basic/blinky），确保没有编译错误。
- 对于新增的开发板，需要测试所支持的示例（samples/drivers/eeprom、samples/drivers/uart 等）。

### 3.4 测试输出

测试完成后会生成：

```
boards/gd/scripts/build/
├── gd32_build_gd32f527i_eval_blinky/      # 构建目录
│   └── zephyr/
│       ├── zephyr.elf                      # ELF 可执行文件
│       ├── zephyr.bin                      # 二进制固件
│       └── zephyr.hex                      # HEX 固件
├── gd32_test_report.json                   # JSON 测试报告
└── gd32_test_report.xml                    # JUnit XML 报告
```
