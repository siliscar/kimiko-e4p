#!/usr/bin/env python3
"""中文：编译真实字体块缓存，注入文件读取和分配失败。/ English: Compile the real font block cache with file-I/O and allocation failure injection."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="ttf-block-cache-") as directory:
    work = Path(directory)
    (work / "esp_heap_caps.h").write_text(
        "#pragma once\n#include <stddef.h>\n#define MALLOC_CAP_SPIRAM 1\n"
        "#define MALLOC_CAP_8BIT 2\nvoid *heap_caps_malloc(size_t bytes, int caps);\n")
    (work / "esp_timer.h").write_text(
        "#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n")
    binary = work / "cache"
    subprocess.run([
        "cc", "-std=c11", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Werror", "-g",
        "-fsanitize=address,undefined", "-Dread=cache_test_read", "-Dlseek=cache_test_lseek",
        "-I" + str(work), "-Imain/font", "tools/ttf_block_cache_host_test.c",
        "main/font/ttf_block_cache.c", "-o", str(binary),
    ], cwd=ROOT, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True)
