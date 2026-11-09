#!/usr/bin/env python3
"""中文：在 ASan/UBSan 下验证真实字体资源模块的读取与回收。
English: Verify real font-source reads and reclamation with ASan/UBSan.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="ttf-source-") as folder:
    work = Path(folder)
    (work / "esp_heap_caps.h").write_text(
        "#pragma once\n#include <stddef.h>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\n"
        "void *heap_caps_malloc(size_t,int);\nvoid heap_caps_free(void *);\nsize_t heap_caps_get_largest_free_block(int);\n")
    (work / "esp_timer.h").write_text("#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n")
    (work / "esp_log.h").write_text("#pragma once\nvoid source_test_log(const char *,const char *,...);\n#define ESP_LOGI(...) source_test_log(__VA_ARGS__)\n")
    binary = work / "source"
    subprocess.run([
        "cc", "-std=c11", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Werror", "-g",
        "-fsanitize=address,undefined", "-Dread=source_test_read", "-Dlseek=source_test_lseek",
        "-Dclose=source_test_close", "-I" + str(work), "-Imain/font",
        "tools/ttf_font_source_host_test.c", "main/font/ttf_font_source.c", "-o", str(binary),
    ], cwd=ROOT, check=True)
    subprocess.run([str(binary)], check=True)
