#!/usr/bin/env python3
"""中文：编译真实阅读呈现路由并验证显示边界。/ English: Compile real reader routing and verify display boundaries."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="reader-present-") as directory:
    binary = Path(directory) / "present"
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
        "-fsanitize=address,undefined", "-Itools/display_host_stubs",
        "-Icomponents/e0470_page_turn/include", "-Imain", "-Imain/display", "-Imain/book",
        "tools/reader_present_host_test.c", "main/book/reader_present.c",
        "main/display/display_refresh.c", "main/display/display_refresh_policy.c", "-Imain/app",
        "-o", str(binary),
    ], cwd=ROOT, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True)
