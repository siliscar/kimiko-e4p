#!/usr/bin/env python3
"""中文：编译真实重排模块并注入失败。/ English: Compile real reflow control and inject failures."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="reader-reflow-") as directory:
    binary = Path(directory) / "reflow"
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
        "-fsanitize=address,undefined", "-Itools/book_layout_stubs", "-Itools/book_source_host_stubs",
        "-Imain/book", "-Imain", "-Imain/settings", "tools/reader_reflow_host_test.c", "main/book/reader_reflow.c",
        "-o", str(binary),
    ], cwd=ROOT, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True)
