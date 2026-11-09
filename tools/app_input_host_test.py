#!/usr/bin/env python3
"""中文：在诊断开关两条路径执行真实输入管线与手势识别器。
English: Run the real input pipeline and gesture recognizer with diagnostics both enabled and disabled.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="app-input-") as directory:
    work = Path(directory)
    for diagnostic in (0, 1):
        (work / "sdkconfig.h").write_text(
            f"#pragma once\n#define CONFIG_TOUCH_DIAG {diagnostic}\n"
            "#define CONFIG_TOUCH_DIAG_BEEP 0\n")
        binary = work / f"input-{diagnostic}"
        subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
            "-fsanitize=address,undefined", "-I" + str(work),
            "-Itools/app_loop_stubs", "-Imain", "-Imain/display", "-Imain/app", "-Imain/ui",
            "-Itools/ui_gesture_stubs", "tools/app_input_host_test.c",
            "main/app/app_input.c", "main/ui/ui_gesture.c", "-o", str(binary),
        ], cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True)
