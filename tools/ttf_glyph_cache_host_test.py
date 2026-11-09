#!/usr/bin/env python3
"""中文：运行真实字形缓存，验证共享预算、键、淘汰和释放。/ English: Run the real glyph cache to check shared budget, keys, eviction and release."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="ttf-glyph-cache-") as directory:
    work = Path(directory)
    (work / "esp_heap_caps.h").write_text(
        "#pragma once\nvoid heap_caps_free(void *allocation);\n")
    binary = work / "cache"
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
        "-fsanitize=address,undefined", "-I" + str(work), "-Imain/font",
        "tools/ttf_glyph_cache_host_test.c", "main/font/ttf_glyph_cache.c",
        "-o", str(binary),
    ], cwd=ROOT, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True)
