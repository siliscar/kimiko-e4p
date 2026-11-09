#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 chopper1026
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""多网络存储与触屏配网的实际实现回归。/ Regress real multi-network storage and provisioning UI.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix="kimiko-wifi-profiles-") as directory:
        work = Path(directory)
        env, compiler = toolchain(work)
        flags = ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-deprecated-declarations",
                 "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        cjson = ROOT / "managed_components/espressif__cjson/cJSON"
        cases = {
            "storage": (["tools/transfer_wifi_host_test.c", str(cjson / "cJSON.c")], [str(cjson)]),
            "ui": (["tools/transfer_ui_host_test.c", "main/ui/ui_text_edit.c", "main/ui/ui_text_input.c",
                    "main/ui/ui_keyboard.c", "main/ui/ui_ime.c", "components/book_search/book_search.c"],
                   ["tools/transfer_ui_stubs", "components/wifi_transfer/include",
                    "components/book_search/include", "main/ui", "main/services"]),
        }
        for name, (sources, includes) in cases.items():
            binary = work / name
            subprocess.run(compiler + flags + ["-I" + path for path in includes] + sources +
                           ["-lm", "-o", str(binary)], cwd=ROOT, env=env, check=True)
            subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)
        subprocess.run([sys.executable, "tools/test_transfer_wifi_forget.py"], cwd=ROOT, env=env, check=True)


if __name__ == "__main__":
    main()
