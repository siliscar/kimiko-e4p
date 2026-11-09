#!/usr/bin/env python3
"""中文：执行真实顶部快捷控制器的宿主回归。/ English: Run host regressions for the real top quick-layer controller."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="app-quick-") as directory:
    work = Path(directory)
    (work / "usb_storage.h").write_text("#pragma once\n#include <stdbool.h>\nbool usb_storage_active(void);\n")
    binary = work / "quick"
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
        "-fsanitize=address,undefined", "-I" + str(work),
        "-Itools/app_loop_stubs", "-Imain", "-Imain/display", "-Imain/ota", "-Imain/settings", "-Imain/app", "-Imain/ui",
        "-Icomponents/ble_page_turner/include", "-Itools/ui_gesture_stubs",
        "tools/app_quick_layer_host_test.c", "main/app/app_quick_layer.c",
        "main/ui/ui_quick_menu_input.c", "main/display/display_refresh.c", "main/display/display_refresh_policy.c", "-Imain/app",
        "tools/refresh_loop_test_boundary.c", "-o", str(binary),
    ], cwd=ROOT, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True)
