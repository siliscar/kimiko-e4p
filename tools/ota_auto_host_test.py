#!/usr/bin/env python3
"""中文：编译实际自动 OTA 服务并保留 sanitizer。/ English: Compile the real automatic OTA service with sanitizers."""
from pathlib import Path
import subprocess
import tempfile
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="ota-auto-") as directory:
    work = Path(directory)
    env, _ = toolchain(work)
    for header in ("settings.h", "device_power.h", "wifi_transfer.h", "esp_timer.h", "nvs.h"):
        (work / header).write_text("#pragma once\n")
    (work / "esp_err.h").write_text("#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n")
    binary = work / "test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I" + str(work),
                    "tools/ota_auto_host_test.c", "-o", str(binary)], cwd=ROOT, env=env, check=True)
    subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)
