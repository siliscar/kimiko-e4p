"""中文：编译真实生命周期和文件任务服务，覆盖检查点顺序及睡眠前置失败短路。
English: Compile real lifecycle/file-job services for checkpoint ordering and pre-sleep failure short-circuiting.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    exe = Path(tmp) / "lifecycle"
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-g",
        "-Itools/app_loop_stubs", "-Imain", "-Imain/boot", "-Imain/display", "-Imain/ota", "-Imain/services", "-Imain/time", "-Icomponents/ble_page_turner/include",
        "-Icomponents/board_support/include", "-Imain/app", "-Imain/ui", "-Imain/book", "-Itools/ui_gesture_stubs",
        "tools/app_lifecycle_host_test.c", "main/app/app_lifecycle.c", "main/services/storage_jobs.c",
        "-o", str(exe),
    ], cwd=ROOT, check=True)
    subprocess.run([str(exe)], check=True)
