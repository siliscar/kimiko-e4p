#!/usr/bin/env python3
"""中文：编译真实刷新策略与波形目录回归。/ English: Compile real refresh-policy and waveform-catalog regressions."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="display-refresh-") as directory:
    work = Path(directory)
    common = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g", "-fsanitize=address,undefined"]
    includes = ["-Imain", "-Imain/display", "-Imain/settings", "-Imain/app"]
    binary = work / "refresh"
    subprocess.run(common + ["-Itools/display_host_stubs", "-Icomponents/e0470_page_turn/include"] + includes + [
        "tools/display_refresh_host_test.c", "main/display/display_refresh.c",
        "main/display/display_refresh_policy.c", "main/display/display_refresh_context.c", "-o", str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True)
    binary = work / "catalog"
    subprocess.run(common + includes + ["-Itools/waveform_host_stubs", "-Icomponents/epdiy/include", "-Icomponents/epdiy/src",
        "-Icomponents/e0470_epaper_waveform/include", "-Icomponents/e0470_epaper_waveform/waveforms",
        "tools/display_waveform_catalog_host_test.c", "main/display/display_waveform_catalog.c",
        "main/display/display_main_waveform.c", "main/display/display_reader_waveform.c", "main/display/display_normalize.c",
        "components/e0470_epaper_waveform/e0470_epaper_waveform.c", "components/e0470_epaper_waveform/e0470_waveform_trim.c",
        "-o", str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True)

# 页面测试保留原物理出口断言；专项矩阵检查它们表达的意图。/ Page tests retain physical-exit assertions; the dedicated matrix checks their intents.
for name in ("dashboard_home_host_test.py", "settings_scroll_host_test.py", "input_editors_host_test.py", "pager_host_test.py"):
    subprocess.run([sys.executable, str(ROOT / "tools" / name)], cwd=ROOT, check=True)
