#!/usr/bin/env python3
"""中文：执行真实导航与返回恢复的宿主回归。/ English: Run host regressions for real navigation and return restoration."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="app-navigation-") as directory:
    work = Path(directory)
    (work / "app_font_context.h").write_text("#pragma once\n#include <stdbool.h>\nbool app_font_activate_system(void);\n")
    binary = work / "navigation"
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
        "-fsanitize=address,undefined", "-I" + str(work),
        "-Itools/app_loop_stubs", "-Imain", "-Imain/ota", "-Imain/display", "-Imain/settings", "-Imain/time", "-Imain/app", "-Imain/ui",
        "-Itools/ui_gesture_stubs", "tools/app_navigation_host_test.c",
        "main/app/app_navigation.c", "-o", str(binary),
    ], cwd=ROOT, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True)
