#!/usr/bin/env bash
# 中文：运行真实面板绘制、字号适配、命中与完整字形覆盖回归。
# English: Run real panel painting, scaled glyph fitting, hit testing and full glyph coverage regressions.
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tools/reader_settings_view_host_test.py
python3 tools/reader_ui_scale_host_test.py
python3 tools/reading_settings_host_test.py
python3 tools/ui_font_coverage_host_test.py
