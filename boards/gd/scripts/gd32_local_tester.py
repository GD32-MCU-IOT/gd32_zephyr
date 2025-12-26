#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2025 GigaDevice Semiconductor Inc.

"""
GD32 Zephyr Local Test Runner
==============================

A lightweight, GD32-focused build test runner for Zephyr RTOS.
Supports both testcase.yaml and sample.yaml discovery.

Features:
- Auto-discovery of GD32 boards
- Support for testcase.yaml and sample.yaml
- Board (platform) filtering
- Tag filtering
- Pristine build control (never/auto/always)
- Parallel build support
- Detailed logging and error reporting
- JSON and JUnit XML report generation

Example usage:
  # Test blinky on all GD32 boards
  python3 gd32_local_tester.py -T samples/basic/blinky

  # Test specific boards
  python3 gd32_local_tester.py -T samples/basic/blinky -p gd32f407v_start -p gd32e507z_eval

  # Test with tags
  python3 gd32_local_tester.py -T tests/kernel/common -t kernel

  # Parallel builds with 4 jobs
  python3 gd32_local_tester.py -T samples/basic/blinky -j 4
"""

import argparse
import json
import os
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

try:
    import yaml
except ImportError:
    print("ERROR: PyYAML not installed. Install with: pip3 install pyyaml")
    sys.exit(1)

# ------------------------------------------------------------
# Configuration
# ------------------------------------------------------------

GD32_BOARDS_DIR = "boards/gd"
BUILD_DIR_PREFIX = "gd32_build_"
DEFAULT_TIMEOUT = 600  # 10 minutes
MAX_LOG_LENGTH = 1000

# ------------------------------------------------------------
# Utilities
# ------------------------------------------------------------


class Logger:
    """Colorful logger for terminal output"""

    COLORS = {
        'INFO': '\033[36m',  # Cyan
        'WARN': '\033[33m',  # Yellow
        'ERROR': '\033[31m',  # Red
        'OK': '\033[32m',  # Green
        'RESET': '\033[0m',
    }

    def __init__(self, verbose: bool = False):
        self.verbose = verbose

    def _log(self, level: str, msg: str):
        color = self.COLORS.get(level, '')
        reset = self.COLORS['RESET']
        print(f"{color}[{level:5s}]{reset} {msg}")

    def info(self, msg: str):
        self._log('INFO', msg)

    def warn(self, msg: str):
        self._log('WARN', msg)

    def error(self, msg: str):
        self._log('ERROR', msg)

    def ok(self, msg: str):
        self._log('OK', msg)

    def debug(self, msg: str):
        if self.verbose:
            self._log('DEBUG', msg)


# ------------------------------------------------------------
# Data models
# ------------------------------------------------------------


@dataclass
class TestCase:
    """Represents a test case or sample"""

    name: str
    path: Path
    tags: list[str]
    platforms: list[str]
    source_type: str  # 'testcase' or 'sample'

    def __str__(self):
        return f"{self.name} ({self.source_type})"


@dataclass
class BuildResult:
    """Result of a single build"""

    board: str
    testcase: str
    success: bool
    message: str
    duration: float
    build_dir: Path | None = None
    log_output: str = ""


# ------------------------------------------------------------
# Board discovery
# ------------------------------------------------------------


def discover_gd32_boards(zephyr_base: Path) -> list[str]:
    """Discover all GD32 boards in the Zephyr tree"""
    boards = []
    gd_boards_dir = zephyr_base / GD32_BOARDS_DIR

    if not gd_boards_dir.exists():
        return boards

    for board_dir in gd_boards_dir.iterdir():
        if (
            board_dir.is_dir()
            and board_dir.name not in ['scripts', '__pycache__']
            and (
                (board_dir / 'board.yml').exists()
                or list(board_dir.glob('*.dts'))
                or list(board_dir.glob('*_defconfig'))
            )
        ):
            boards.append(board_dir.name)

    return sorted(boards)


# ------------------------------------------------------------
# Test/Sample discovery
# ------------------------------------------------------------


def parse_yaml_file(yaml_path: Path, source_type: str) -> list[TestCase]:
    """Parse testcase.yaml or sample.yaml"""
    cases = []

    try:
        with open(yaml_path, encoding='utf-8') as f:
            data = yaml.safe_load(f) or {}
    except Exception as e:
        print(f"Warning: Failed to parse {yaml_path}: {e}")
        return cases

    if source_type == 'testcase':
        tests = data.get('tests', {})
        for name, cfg in tests.items():
            # Handle relative path
            if 'path' in cfg:
                test_path = (yaml_path.parent / cfg['path']).resolve()
            else:
                test_path = yaml_path.parent

            tags = cfg.get('tags', [])
            platforms = cfg.get('platform_allow', cfg.get('platforms', []))

            cases.append(
                TestCase(
                    name=name,
                    path=test_path,
                    tags=tags,
                    platforms=platforms if platforms else [],
                    source_type='testcase',
                )
            )

    elif source_type == 'sample':
        sample_name = data.get('name', yaml_path.parent.name)
        common = data.get('common', {})
        tags = common.get('tags', [])

        # For samples, we use the sample directory itself
        test_path = yaml_path.parent

        # Get platform filters if any
        platforms = common.get('platform_allow', [])

        cases.append(
            TestCase(
                name=sample_name,
                path=test_path,
                tags=tags,
                platforms=platforms if platforms else [],
                source_type='sample',
            )
        )

    return cases


def discover_tests_and_samples(root: Path, log: Logger) -> list[TestCase]:
    """Discover both testcase.yaml and sample.yaml files"""
    cases = []

    # Search for testcase.yaml
    for yaml_file in root.rglob('testcase.yaml'):
        log.debug(f"Found testcase.yaml: {yaml_file}")
        cases.extend(parse_yaml_file(yaml_file, 'testcase'))

    # Search for sample.yaml
    for yaml_file in root.rglob('sample.yaml'):
        log.debug(f"Found sample.yaml: {yaml_file}")
        cases.extend(parse_yaml_file(yaml_file, 'sample'))

    return cases


# ------------------------------------------------------------
# Builder
# ------------------------------------------------------------


def west_build(
    board: str,
    testcase: TestCase,
    pristine: str,
    build_dir: Path,
    zephyr_base: Path,
    timeout: int,
    log: Logger,
) -> BuildResult:
    """Execute west build for a single board/test combination"""

    start_time = time.time()

    # Prepare build command
    cmd = ['west', 'build']

    if pristine != 'never':
        cmd += ['-p', pristine]

    # Convert paths to strings and strip any whitespace
    board_str = str(board).strip()
    build_dir_str = str(build_dir).strip()

    # Convert test path to relative path from ZEPHYR_BASE
    try:
        test_path_relative = testcase.path.relative_to(zephyr_base)
        test_path_str = str(test_path_relative)
    except ValueError:
        # If path is not relative to zephyr_base, use absolute path
        test_path_str = str(testcase.path)

    test_path_str = test_path_str.strip()

    cmd += ['-b', board_str, '-d', build_dir_str, test_path_str]

    log.debug(f"Command: {' '.join(cmd)}")

    try:
        result = subprocess.run(
            cmd,
            cwd=str(zephyr_base),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout,
            env=os.environ.copy(),
        )

        duration = time.time() - start_time
        output = result.stdout

        if result.returncode == 0:
            return BuildResult(
                board=board,
                testcase=testcase.name,
                success=True,
                message='Build successful',
                duration=duration,
                build_dir=build_dir,
                log_output=output[-MAX_LOG_LENGTH:] if len(output) > MAX_LOG_LENGTH else output,
            )
        else:
            # Extract relevant error message
            error_lines = [
                line
                for line in output.split('\n')
                if 'error:' in line.lower() or 'fatal error:' in line.lower()
            ]

            if error_lines:
                # Show last 5 error lines
                error_msg = '\n'.join(error_lines[-5:])
            else:
                # No explicit error lines, show last part of output
                output_lines = output.strip().split('\n')
                error_msg = '\n'.join(output_lines[-10:])

            return BuildResult(
                board=board,
                testcase=testcase.name,
                success=False,
                message=f'Build failed:\n{error_msg}',
                duration=duration,
                build_dir=build_dir,
                log_output=output[-MAX_LOG_LENGTH:] if len(output) > MAX_LOG_LENGTH else output,
            )

    except subprocess.TimeoutExpired:
        duration = timeout
        return BuildResult(
            board=board,
            testcase=testcase.name,
            success=False,
            message=f'Build timeout after {timeout}s',
            duration=duration,
            log_output="Build timed out",
        )

    except Exception as e:
        duration = time.time() - start_time
        return BuildResult(
            board=board,
            testcase=testcase.name,
            success=False,
            message=f'Build error: {str(e)}',
            duration=duration,
            log_output=str(e),
        )


def build_worker(
    board: str, testcase: TestCase, args, work_dir: Path, zephyr_base: Path, log: Logger
) -> BuildResult:
    """Worker function for parallel builds"""

    # Create unique build directory
    safe_name = testcase.name.replace('/', '_').replace('.', '_')
    build_dir = work_dir / f"{BUILD_DIR_PREFIX}{board}_{safe_name}"

    log.info(f"Building {board} :: {testcase.name}")

    result = west_build(
        board=board,
        testcase=testcase,
        pristine=args.pristine,
        build_dir=build_dir,
        zephyr_base=zephyr_base,
        timeout=args.timeout,
        log=log,
    )

    if result.success:
        log.ok(f"✓ {board} :: {testcase.name} ({result.duration:.1f}s)")
    else:
        log.error(f"✗ {board} :: {testcase.name} - {result.message[:100]}")

    return result


# ------------------------------------------------------------
# Filtering
# ------------------------------------------------------------


def filter_boards_for_testcase(
    testcase: TestCase, all_gd32_boards: list[str], platform_filter: list[str] | None
) -> list[str]:
    """Determine which boards to test for a given testcase"""

    # If testcase specifies platforms, use those
    if testcase.platforms:
        boards = [b for b in testcase.platforms if b in all_gd32_boards]
    else:
        # Test on all GD32 boards
        boards = all_gd32_boards

    # Apply user's platform filter
    if platform_filter:
        boards = [b for b in boards if b in platform_filter]

    return boards


# ------------------------------------------------------------
# Reporting
# ------------------------------------------------------------


def generate_json_report(results: list[BuildResult], output_file: Path):
    """Generate JSON test report"""

    report = {
        'timestamp': datetime.now().isoformat(),
        'summary': {
            'total': len(results),
            'passed': sum(1 for r in results if r.success),
            'failed': sum(1 for r in results if not r.success),
            'duration': sum(r.duration for r in results),
        },
        'results': [
            {
                'board': r.board,
                'testcase': r.testcase,
                'success': r.success,
                'message': r.message,
                'duration': r.duration,
            }
            for r in results
        ],
    }

    with open(output_file, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=2, ensure_ascii=False)

    return report


def generate_junit_xml(results: list[BuildResult], output_file: Path):
    """Generate JUnit XML report for CI integration"""

    total = len(results)
    failures = sum(1 for r in results if not r.success)
    total_time = sum(r.duration for r in results)

    xml_lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<testsuites tests="{total}" failures="{failures}" time="{total_time:.2f}">',
        '  <testsuite name="GD32 Build Tests">',
    ]

    for result in results:
        test_name = f"{result.board}.{result.testcase}"
        xml_lines.append(f'    <testcase name="{test_name}" time="{result.duration:.2f}">')

        if not result.success:
            xml_lines.append(f'      <failure message="{result.message[:200]}">')
            xml_lines.append(f'        {result.log_output[:500]}')
            xml_lines.append('      </failure>')

        xml_lines.append('    </testcase>')

    xml_lines.append('  </testsuite>')
    xml_lines.append('</testsuites>')

    with open(output_file, 'w', encoding='utf-8') as f:
        f.write('\n'.join(xml_lines))


def print_summary(results: list[BuildResult], log: Logger):
    """Print test summary to console"""

    total = len(results)
    passed = sum(1 for r in results if r.success)
    failed = total - passed
    total_time = sum(r.duration for r in results)

    log.info("=" * 70)
    log.info("Test Summary:")
    log.info(f"  Total:    {total}")
    log.ok(f"  Passed:   {passed}")
    if failed > 0:
        log.error(f"  Failed:   {failed}")
    log.info(f"  Duration: {total_time:.1f}s")
    log.info("=" * 70)

    if failed > 0:
        log.error("Failed builds:")
        for r in results:
            if not r.success:
                log.error(f"  - {r.board} :: {r.testcase}")
                log.error(f"    {r.message}")
                if r.log_output:
                    log.error("    Build output (last 1000 chars):")
                    log.error(f"    {r.log_output[-1000:]}")


# ------------------------------------------------------------
# Main
# ------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(
        description="GD32 Local Build Test Runner",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        allow_abbrev=False,
        epilog="""
Examples:
  # Test blinky on all GD32 boards
  %(prog)s -T samples/basic/blinky

  # Test specific boards
  %(prog)s -T samples/basic/blinky -p gd32f407v_start -p gd32e507z_eval

  # Parallel builds
  %(prog)s -T samples/basic/blinky -j 4

  # With tag filter
  %(prog)s -T tests/kernel/common -t kernel
        """,
    )

    parser.add_argument(
        '-T',
        '--tests-root',
        type=str,
        required=True,
        help='Root directory to search for tests/samples (relative to ZEPHYR_BASE)',
    )

    parser.add_argument(
        '-p',
        '--platform',
        action='append',
        dest='platforms',
        help='Board/platform filter (can be specified multiple times)',
    )

    parser.add_argument(
        '-t',
        '--tag',
        action='append',
        dest='tags',
        help='Tag filter (can be specified multiple times)',
    )

    parser.add_argument(
        '--pristine',
        choices=['never', 'auto', 'always'],
        default='auto',
        help='Pristine build mode (default: auto)',
    )

    parser.add_argument(
        '-j', '--jobs', type=int, default=1, help='Number of parallel build jobs (default: 1)'
    )

    parser.add_argument(
        '--timeout',
        type=int,
        default=DEFAULT_TIMEOUT,
        help=f'Build timeout in seconds (default: {DEFAULT_TIMEOUT})',
    )

    parser.add_argument(
        '--build-dir', type=str, help='Base directory for builds (default: boards/gd/scripts/build)'
    )

    parser.add_argument(
        '--json-report',
        type=str,
        default=None,
        help='JSON report output file (default: build/gd32_test_report.json)',
    )

    parser.add_argument(
        '--junit-xml',
        type=str,
        default=None,
        help='JUnit XML report output file '
        '(default: build/gd32_test_report.xml if --junit-xml flag used)',
    )

    parser.add_argument('--list-boards', action='store_true', help='List all GD32 boards and exit')

    parser.add_argument('-v', '--verbose', action='store_true', help='Verbose output')

    args = parser.parse_args()

    log = Logger(verbose=args.verbose)

    # Validate environment
    zephyr_base = os.environ.get('ZEPHYR_BASE')
    if not zephyr_base:
        log.error('ZEPHYR_BASE environment variable not set')
        log.error('Please source zephyr-env.sh first')
        return 1

    zephyr_base = Path(zephyr_base)
    if not zephyr_base.exists():
        log.error(f'ZEPHYR_BASE path does not exist: {zephyr_base}')
        return 1

    # Discover GD32 boards
    log.info("Discovering GD32 boards...")
    gd32_boards = discover_gd32_boards(zephyr_base)

    if not gd32_boards:
        log.error('No GD32 boards found')
        return 1

    log.ok(f"Found {len(gd32_boards)} GD32 boards")

    if args.list_boards:
        print("\nAvailable GD32 boards:")
        for board in gd32_boards:
            print(f"  - {board}")
        return 0

    # Resolve test root
    test_root = zephyr_base / args.tests_root
    if not test_root.exists():
        log.error(f'Test root does not exist: {test_root}')
        return 1

    # Discover tests and samples
    log.info(f"Discovering tests and samples in {test_root}...")
    testcases = discover_tests_and_samples(test_root, log)

    if not testcases:
        log.error('No testcase.yaml or sample.yaml found')
        return 1

    log.ok(f"Found {len(testcases)} test(s)/sample(s)")

    # Apply tag filter
    if args.tags:
        testcases = [tc for tc in testcases if set(args.tags).intersection(tc.tags)]
        log.info(f"After tag filter: {len(testcases)} test(s)")

    if not testcases:
        log.warn('No tests match the specified filters')
        return 0

    # Build work directory
    if args.build_dir:
        work_dir = Path(args.build_dir)
        work_dir.mkdir(parents=True, exist_ok=True)
    else:
        # Use boards/gd/scripts/build as default build directory
        script_dir = Path(__file__).parent
        work_dir = script_dir / 'build'
        work_dir.mkdir(parents=True, exist_ok=True)

    log.info(f"Build directory: {work_dir}")

    # Generate build matrix
    build_tasks = []
    for tc in testcases:
        boards = filter_boards_for_testcase(tc, gd32_boards, args.platforms)
        for board in boards:
            build_tasks.append((board, tc))

    if not build_tasks:
        log.warn('No builds to execute (empty build matrix)')
        return 0

    log.info(f"Build matrix: {len(build_tasks)} builds")
    log.info(f"Parallel jobs: {args.jobs}")

    # Execute builds
    results = []

    if args.jobs == 1:
        # Sequential builds
        for board, tc in build_tasks:
            result = build_worker(board, tc, args, work_dir, zephyr_base, log)
            results.append(result)
    else:
        # Parallel builds
        with ThreadPoolExecutor(max_workers=args.jobs) as executor:
            futures = {
                executor.submit(build_worker, board, tc, args, work_dir, zephyr_base, log): (
                    board,
                    tc,
                )
                for board, tc in build_tasks
            }

            for future in as_completed(futures):
                result = future.result()
                results.append(result)

    # Generate reports
    log.info("Generating reports...")

    # Set default JSON report path to build directory
    if args.json_report:
        json_report_path = Path(args.json_report)
    else:
        json_report_path = work_dir / 'gd32_test_report.json'

    generate_json_report(results, json_report_path)
    log.ok(f"JSON report: {json_report_path}")

    # Set default JUnit XML report path to build directory if enabled
    if args.junit_xml is not None:
        if args.junit_xml:
            junit_path = Path(args.junit_xml)
        else:
            junit_path = work_dir / 'gd32_test_report.xml'
        generate_junit_xml(results, junit_path)
        log.ok(f"JUnit XML report: {junit_path}")

    # Print summary
    print_summary(results, log)

    # Return exit code
    failed_count = sum(1 for r in results if not r.success)
    return 0 if failed_count == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
