#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2025 GigaDevice Semiconductor Inc.

"""
GD32 智能测试选择器
==================

根据开发板的外设支持情况，智能选择合适的测试用例。
读取 gd32_peripheral_matrix.yaml 配置文件，为每个开发板生成最优测试计划。

使用方法:
  # 生成所有开发板的测试计划
  python3 gd32_smart_test_selector.py --output test_plan.json

  # 仅生成特定开发板的测试计划
  python3 gd32_smart_test_selector.py -p gd32f407v_start -p gd32e507z_eval

  # 按测试组生成
  python3 gd32_smart_test_selector.py --group essential

  # 执行测试
  python3 gd32_smart_test_selector.py --execute -j 4
"""

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path

import yaml

# ------------------------------------------------------------
# 数据结构
# ------------------------------------------------------------


@dataclass
class BoardConfig:
    """开发板配置"""

    name: str
    series: str
    peripherals: list[str]
    test_suites: dict[str, list[str]]
    arch: str = "arm"


@dataclass
class TestPlan:
    """测试计划"""

    board: str
    test_path: str
    category: str
    required_peripherals: list[str]


# ------------------------------------------------------------
# 配置加载器
# ------------------------------------------------------------


class PeripheralMatrixLoader:
    """外设矩阵配置加载器"""

    def __init__(self, config_file: str = None):
        if config_file is None:
            script_dir = Path(__file__).parent
            config_file = script_dir / "gd32_peripheral_matrix.yaml"

        self.config_file = Path(config_file)
        self.boards: dict[str, BoardConfig] = {}
        self.test_groups: dict = {}
        self.ci_config: dict = {}

        self.load_config()

    def load_config(self):
        """加载配置文件"""
        if not self.config_file.exists():
            raise FileNotFoundError(f"配置文件不存在: {self.config_file}")

        with open(self.config_file, encoding='utf-8') as f:
            data = yaml.safe_load(f)

        # 加载开发板配置
        boards_data = data.get('boards', {})
        for board_name, board_info in boards_data.items():
            self.boards[board_name] = BoardConfig(
                name=board_name,
                series=board_info.get('series', 'unknown'),
                peripherals=board_info.get('peripherals', []),
                test_suites=board_info.get('test_suites', {}),
                arch=board_info.get('arch', 'arm'),
            )

        # 加载测试组
        self.test_groups = data.get('test_groups', {})

        # 加载 CI 配置
        self.ci_config = data.get('ci_config', {})

    def get_board_config(self, board_name: str) -> BoardConfig:
        """获取开发板配置"""
        return self.boards.get(board_name)

    def get_all_boards(self) -> list[str]:
        """获取所有开发板列表"""
        return list(self.boards.keys())

    def get_boards_by_series(self, series: str) -> list[str]:
        """按系列获取开发板"""
        return [name for name, board in self.boards.items() if board.series == series]

    def get_boards_with_peripheral(self, peripheral: str) -> list[str]:
        """获取支持特定外设的开发板"""
        return [name for name, board in self.boards.items() if peripheral in board.peripherals]


# ------------------------------------------------------------
# 测试计划生成器
# ------------------------------------------------------------


class TestPlanGenerator:
    """测试计划生成器"""

    def __init__(self, matrix_loader: PeripheralMatrixLoader):
        self.loader = matrix_loader

    def generate_for_board(self, board_name: str) -> list[TestPlan]:
        """为单个开发板生成测试计划"""
        board_config = self.loader.get_board_config(board_name)
        if not board_config:
            return []

        test_plans = []

        # 遍历测试套件
        for category, tests in board_config.test_suites.items():
            for test_path in tests:
                # 推断测试所需的外设
                required_peripherals = self._infer_required_peripherals(test_path)

                # 检查开发板是否支持所需外设
                if all(p in board_config.peripherals for p in required_peripherals):
                    test_plans.append(
                        TestPlan(
                            board=board_name,
                            test_path=test_path,
                            category=category,
                            required_peripherals=required_peripherals,
                        )
                    )

        return test_plans

    def generate_for_all_boards(self, board_filter: list[str] = None) -> list[TestPlan]:
        """为所有开发板生成测试计划"""
        all_plans = []

        boards = board_filter if board_filter else self.loader.get_all_boards()

        for board_name in boards:
            plans = self.generate_for_board(board_name)
            all_plans.extend(plans)

        return all_plans

    def generate_by_group(self, group_name: str) -> list[TestPlan]:
        """按测试组生成测试计划"""
        group_config = self.loader.test_groups.get(group_name)
        if not group_config:
            return []

        tests = group_config.get('tests', [])
        boards = group_config.get('boards', [])

        # 如果 boards 是 'all'，则使用所有开发板
        if boards == 'all':
            boards = self.loader.get_all_boards()

        test_plans = []
        for board in boards:
            for test_path in tests:
                required_peripherals = self._infer_required_peripherals(test_path)
                test_plans.append(
                    TestPlan(
                        board=board,
                        test_path=test_path,
                        category=group_name,
                        required_peripherals=required_peripherals,
                    )
                )

        return test_plans

    def _infer_required_peripherals(self, test_path: str) -> list[str]:
        """根据测试路径推断所需外设"""
        # 简单的推断逻辑，可以根据实际需求扩展
        peripherals = []

        test_lower = test_path.lower()

        if 'i2c' in test_lower:
            peripherals.append('i2c')
        if 'spi' in test_lower:
            peripherals.append('spi')
        if 'uart' in test_lower or 'console' in test_lower or 'shell' in test_lower:
            peripherals.append('uart')
        if 'gpio' in test_lower or 'blinky' in test_lower:
            peripherals.append('gpio')
        if 'can' in test_lower:
            peripherals.append('can')
        if 'net' in test_lower or 'ethernet' in test_lower:
            peripherals.append('ethernet')
        if 'adc' in test_lower:
            peripherals.append('adc')
        if 'pwm' in test_lower:
            peripherals.append('pwm')

        return peripherals

    def export_to_json(self, test_plans: list[TestPlan], output_file: str):
        """导出测试计划到 JSON 文件"""
        data = {
            "test_plans": [
                {
                    "board": plan.board,
                    "test_path": plan.test_path,
                    "category": plan.category,
                    "required_peripherals": plan.required_peripherals,
                }
                for plan in test_plans
            ],
            "summary": {
                "total_tests": len(test_plans),
                "boards": len(set(plan.board for plan in test_plans)),
                "unique_tests": len(set(plan.test_path for plan in test_plans)),
            },
        }

        with open(output_file, 'w', encoding='utf-8') as f:
            json.dump(data, f, indent=2, ensure_ascii=False)

        print(f"✅ 测试计划已导出到: {output_file}")
        print(f"   总测试数: {data['summary']['total_tests']}")
        print(f"   涉及开发板: {data['summary']['boards']}")
        print(f"   独立测试: {data['summary']['unique_tests']}")

    def print_summary(self, test_plans: list[TestPlan]):
        """打印测试计划摘要"""
        boards_set = set(plan.board for plan in test_plans)
        tests_set = set(plan.test_path for plan in test_plans)

        print("\n" + "=" * 60)
        print("GD32 测试计划摘要")
        print("=" * 60)
        print(f"总测试任务数: {len(test_plans)}")
        print(f"涉及开发板: {len(boards_set)}")
        print(f"独立测试用例: {len(tests_set)}")
        print()

        # 按开发板分组统计
        board_stats = {}
        for plan in test_plans:
            if plan.board not in board_stats:
                board_stats[plan.board] = []
            board_stats[plan.board].append(plan.test_path)

        print("开发板测试分布:")
        for board, tests in sorted(board_stats.items()):
            print(f"  {board:25s}: {len(tests):3d} 个测试")

        print("=" * 60 + "\n")


# ------------------------------------------------------------
# 命令行接口
# ------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(
        description="GD32 智能测试选择器",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        allow_abbrev=False,
    )

    parser.add_argument('-c', '--config', help='外设矩阵配置文件路径', default=None)

    parser.add_argument('-p', '--platform', action='append', help='指定开发板（可多次使用）')

    parser.add_argument('-g', '--group', help='按测试组生成测试计划')

    parser.add_argument('-o', '--output', default='gd32_test_plan.json', help='输出 JSON 文件路径')

    parser.add_argument('--list-boards', action='store_true', help='列出所有支持的开发板')

    parser.add_argument('--list-groups', action='store_true', help='列出所有测试组')

    parser.add_argument('--execute', action='store_true', help='生成计划后立即执行测试')

    parser.add_argument(
        '-j', '--jobs', type=int, default=1, help='并行任务数（仅在 --execute 时有效）'
    )

    args = parser.parse_args()

    # 加载配置
    try:
        loader = PeripheralMatrixLoader(args.config)
    except Exception as e:
        print(f"❌ 加载配置文件失败: {e}")
        return 1

    # 列出开发板
    if args.list_boards:
        print("\n支持的 GD32 开发板:")
        print("-" * 40)
        for board in sorted(loader.get_all_boards()):
            config = loader.get_board_config(board)
            print(f"  {board:25s} ({config.series.upper()}, {config.arch})")
            print(f"    外设: {', '.join(config.peripherals)}")
        print()
        return 0

    # 列出测试组
    if args.list_groups:
        print("\n可用的测试组:")
        print("-" * 40)
        for group_name, group_config in loader.test_groups.items():
            print(f"  {group_name}:")
            print(f"    描述: {group_config.get('description', 'N/A')}")
            print(f"    测试: {len(group_config.get('tests', []))}")
        print()
        return 0

    # 生成测试计划
    generator = TestPlanGenerator(loader)

    if args.group:
        # 按组生成
        test_plans = generator.generate_by_group(args.group)
        print(f"按测试组 '{args.group}' 生成测试计划...")
    elif args.platform:
        # 按指定开发板生成
        test_plans = generator.generate_for_all_boards(args.platform)
        print(f"为 {len(args.platform)} 个开发板生成测试计划...")
    else:
        # 为所有开发板生成
        test_plans = generator.generate_for_all_boards()
        print("为所有 GD32 开发板生成测试计划...")

    # 打印摘要
    generator.print_summary(test_plans)

    # 导出
    generator.export_to_json(test_plans, args.output)

    # 执行测试（如果需要）
    if args.execute:
        print(f"\n开始执行测试（并行任务数: {args.jobs}）...")
        # 调用 gd32_local_tester.py
        # 这里可以集成实际的执行逻辑
        print("⚠️  执行功能待实现，请使用 gd32_local_tester.py")

    return 0


if __name__ == '__main__':
    sys.exit(main())
