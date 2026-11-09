#!/usr/bin/env bash
# 中文：执行会话位置、翻页模式和阅读设置的页面回归；仅在 build 内生成产物。
# English: Run page regressions for session position, turn modes and reader settings; artifacts stay in build.
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tools/reader_fullscreen_host_test.py
python3 tools/reader_spacing_host_test.py
python3 tools/reader_tap_dismiss_host_test.py
python3 tools/settings_entry_clock_host_test.py
python3 tools/reader_controls_host_test.py
python3 tools/reading_settings_host_test.py
python3 tools/reader_indent_hold_host_test.py
python3 tools/reader_chrome_host_test.py
python3 tools/reader_shake_host_test.py
