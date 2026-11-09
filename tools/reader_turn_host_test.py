#!/usr/bin/env python3
"""中文：编译真实翻页控制并检查同步边界。/ English: Compile real turn control and check synchronous boundaries."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="reader-turn-") as directory:
    binary = Path(directory) / "turn"
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
        "-fsanitize=address,undefined", "-Imain/book",
        "tools/reader_turn_host_test.c", "main/book/reader_turn.c",
        "-o", str(binary),
    ], cwd=ROOT, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True)
