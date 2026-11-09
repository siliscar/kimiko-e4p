#!/usr/bin/env python3
"""中文：以地址和未定义行为检查运行真实设置备份回归。
English: Run actual settings-backup regressions with address and undefined-behavior checks.
"""
from pathlib import Path
import subprocess
import tempfile
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="settings-backup-") as directory:
    folder = Path(directory)
    env, _ = toolchain(folder)
    binary = folder / "test"
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    "-Itools/settings_backup_stubs", "-Icomponents/wifi_transfer/include",
                    "-Imain", "-Imain/services", "tools/settings_backup_host_test.c", "-o", str(binary)],
                   cwd=ROOT, env=env, check=True)
    subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)
