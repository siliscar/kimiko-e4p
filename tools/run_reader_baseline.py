#!/usr/bin/env python3
"""中文：选择可运行 sanitizer 的宿主编译器并保存阅读回归基线。
English: Select a working sanitizer toolchain and save reader regression results.
"""
import argparse
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
SUITES = {
<<<<<<< HEAD
=======
    "excerpt": [sys.executable, "tools/book_excerpt_host_test.py"],
    "dictionary": ["bash", "tools/run_dictionary_host_tests.sh"],
>>>>>>> d4212af (新增（书摘）：随已显示阅读进度更新首页短句)
    "flash_fast": [sys.executable, "tools/flash_fast_host_test.py"],
    "weather": [sys.executable, "tools/weather_host_test.py"],
    "journal": [sys.executable, "tools/reading_journal_host_test.py"],
    "journal_reader": [sys.executable, "tools/reading_journal_reader_host_test.py"],
    "journal_ui": [sys.executable, "tools/reading_journal_ui_host_test.py"],
    "architecture": [sys.executable, "tools/architecture_host_test.py"],
    "builtin_images": [sys.executable, "tools/builtin_image_host_test.py"],
    "heading": [sys.executable, "tools/reader_title_style_host_test.py"],
    "font_policy": [sys.executable, "tools/lock_font_host_test.py"],
    "memory": [sys.executable, "tools/reader_memory_host_test.py"],
    "source_memory": [sys.executable, "tools/reader_source_memory_host_test.py"],
    "mobi": [sys.executable, "tools/test_book_mobi.py"],
    "perf": [sys.executable, "tools/reader_perf_host_test.py"],
    "clock": [sys.executable, "tools/time_service_host_test.py"],
    "time_policy": ["bash", "tools/run_time_sync_host_test.sh"],
    "ota": ["bash", "tools/run_ota_host_tests.sh"],
    "ota_auto": [sys.executable, "tools/ota_auto_host_test.py"],
    "ota_auto_ui": [sys.executable, "tools/ota_auto_ui_host_test.py"],
    "storage": ["bash", "tools/run_storage_host_tests.sh"],
    "transfer_web": ["node", "tools/transfer_web_host_test.js"],
    "wifi_profiles": [sys.executable, "tools/wifi_profiles_host_test.py"],
    "wifi_ui": [sys.executable, "tools/wifi_ui_raster_host_test.py"],
    "capture": [sys.executable, "tools/capture_reader_perf_host_test.py"],
    "metalio_drivers": [sys.executable, "tools/metalio_drivers_host_test.py"],
    "sleep_compat": [sys.executable, "tools/s31_sleep_compat_host_test.py"],
    "sleep_power": [sys.executable, "tools/sleep_power_host_test.py"],
    "board": [sys.executable, "tools/s31_board_host_test.py"],
    "hv_guard": [sys.executable, "tools/epd_power_guard_host_test.py"],
    "hv_bus": [sys.executable, "tools/board_hv_concurrency_host_test.py"],
    "power_gate": [sys.executable, "tools/e4p_vcom_gate_host_test.py"],
    "battery": [sys.executable, "tools/test_pmu_battery.py"],
    "battery_log": [sys.executable, "tools/battery_log_host_test.py"],
    "battery_log_task": [sys.executable, "tools/battery_log_task_host_test.py"],
    "battery_filter": [sys.executable, "tools/test_pmu_soc_filter.py"],
    "status_power": [sys.executable, "tools/ui_refresh_feedback_host_test.py", "--status-only"],
    "nav_layout": [sys.executable, "tools/ui_refresh_feedback_host_test.py", "--nav-only"],
    "quick_pixels": [sys.executable, "tools/quick_layer_raster_host_test.py"],
    "media": [sys.executable, "tools/test_sd_media_guard.py"],
    "shelf_covers": ["bash", "tools/run_shelf_cover_host_tests.sh"],
    "shelf": [sys.executable, "tools/shelf_categories_host_test.py"],
    "categories": ["bash", "tools/run_category_management_tests.sh"],
    "categories_ui": [sys.executable, "tools/category_ui_host_test.py"],
    "management": [sys.executable, "tools/book_ui_host_test.py"],
    "files": [sys.executable, "tools/file_listing_host_test.py"],
    "files_ui": [sys.executable, "tools/file_ui_host_test.py"],
    "session": [sys.executable, "tools/reader_session_host_test.py"],
    "chapter": [sys.executable, "tools/reader_chapter_host_test.py"],
    "open": [sys.executable, "tools/reader_open_host_test.py"],
    "present": [sys.executable, "tools/reader_present_host_test.py"],
    "turn": [sys.executable, "tools/reader_turn_host_test.py"],
    "reflow": [sys.executable, "tools/reader_reflow_host_test.py"],
    "font_source": [sys.executable, "tools/ttf_font_source_host_test.py"],
    "font_faces": [sys.executable, "tools/title_font_host_test.py"],
    "font_catalog": [sys.executable, "tools/font_catalog_host_test.py"],
    "settings_font": [sys.executable, "tools/settings_layout_font_host_test.py"],
    "font_cache": [sys.executable, "tools/ttf_block_cache_host_test.py"],
    "glyph_cache": [sys.executable, "tools/ttf_glyph_cache_host_test.py"],
    "images": [sys.executable, "tools/reader_images_host_test.py"],
    "webp": [sys.executable, "tools/webp_host_test.py"],
    "settings_view": ["bash", "tools/run_reader_settings_host_tests.sh"],
    "settings_backup": [sys.executable, "tools/settings_backup_host_test.py"],
    "controls": ["bash", "tools/run_reader_controls_host_tests.sh"],
    "prefetch": [sys.executable, "tools/reader_idle_prefetch_host_test.py"],
    "background": [sys.executable, "tools/reader_stability_host_test.py"],
    "loop": ["bash", "tools/run_app_loop_host_tests.sh"],
    "power_key": [sys.executable, "tools/power_key_sampler_host_test.py"],
    "lifecycle": [sys.executable, "tools/app_lifecycle_host_test.py"],
    "platform_input": [sys.executable, "tools/s31_input_host_test.py"],
    "ble": [sys.executable, "tools/ble_turner_host_test.py"],
    "ble_recent": [sys.executable, "tools/ble_recent_host_test.py"],
    "pinpad": [sys.executable, "tools/ui_pinpad_host_test.py"],
    "lock": [sys.executable, "tools/lock_screen_host_test.py"],
    "input": [sys.executable, "tools/app_input_host_test.py"],
    "routing": [sys.executable, "tools/app_navigation_host_test.py"],
    "quick": [sys.executable, "tools/app_quick_layer_host_test.py"],
    "book": ["bash", "tools/run_book_host_tests.sh"],
    "display": ["bash", "tools/run_display_host_test.sh"],
    "refresh": [sys.executable, "tools/display_refresh_host_test.py"],
    "display_contract": [sys.executable, "tools/display_contract_host_test.py"],
    "navigation": ["bash", "tools/run_display_navigation_tests.sh"],
    "ripple": ["bash", "tools/run_water_turn_host_test.sh"],
}


def run(command, env, timeout):
    # 超时结束整组子进程，避免遗留编译器或测试。/ Stop the process group on timeout so compilers and tests cannot linger.
    start = time.monotonic()
    with subprocess.Popen(command, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True,
                          start_new_session=True) as process:
        try:
            output, _ = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            output, _ = process.communicate()
            return {"exit_code": 124, "seconds": time.monotonic() - start,
                    "output": output + "\nTIMEOUT\n"}
    return {"exit_code": process.returncode, "seconds": time.monotonic() - start,
            "output": output}


def toolchain(folder):
    env = os.environ.copy()
    compiler = shlex.split(env.get("CC", "cc"))
    if platform.system() == "Darwin" and "CC" not in env:
        developer = Path("/Library/Developer/CommandLineTools")
        candidate = developer / "usr/bin/clang"
        if candidate.exists():
            compiler = [str(candidate)]
            env["DEVELOPER_DIR"] = str(developer)
            if not env.get("SDKROOT"):
                env["SDKROOT"] = subprocess.check_output(
                    ["xcrun", "--show-sdk-path"], env=env, text=True).strip()
    if env.get("SDKROOT"):
        compiler += ["-isysroot", env["SDKROOT"]]
    compiler[0] = shutil.which(compiler[0]) or compiler[0]
    source, binary = folder / "probe.c", folder / "probe"
    source.write_text("int main(void) { return 0; }\n")
    result = run(compiler + ["-fsanitize=address,undefined", str(source),
                            "-o", str(binary)], env, 30)
    if result["exit_code"] == 0:
        result = run([str(binary)], env, 10)
    if result["exit_code"]:
        raise RuntimeError("Sanitizer compiler/runtime probe failed:\n" + result["output"])
    # 旧测试直接调用 cc/gcc；仅本次子进程使用同一编译器。/ Legacy tests invoke cc/gcc directly; share one compiler only within this run.
    for name in ("cc", "gcc", "clang"):
        wrapper = folder / name
        wrapper.write_text("#!" + sys.executable + "\nimport os,sys\ncommand=" +
                           repr(compiler) + "\nos.execv(command[0],command+sys.argv[1:])\n"
                           if Path(compiler[0]).is_absolute() else
                           "#!/bin/sh\nexec " + shlex.join(compiler) + ' "$@"\n')
        wrapper.chmod(0o755)
    # 默认 cc 先解析为绝对路径，避免包装器再次找到自己。/ Resolve a default compiler before its wrapper can find itself.
    env["PATH"] = str(folder) + os.pathsep + env["PATH"]
    return env, compiler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("suites", nargs="*", choices=tuple(SUITES))
    parser.add_argument("--output", type=Path, default=ROOT / "build/reader-baseline")
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="reader-cc-") as directory:
        env, compiler = toolchain(Path(directory))
        report = {"schema": 1, "compiler": compiler, "sanitizer_probe": "passed",
                  "platform": platform.platform(), "suites": {}}
        for name in args.suites or SUITES:
            result = run(SUITES[name], env, args.timeout)
            (args.output / (name + ".log")).write_text(result.pop("output"))
            report["suites"][name] = result
            print(name + ": " + ("PASS" if result["exit_code"] == 0 else "FAIL"), flush=True)
        (args.output / "results.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    return int(any(r["exit_code"] for r in report["suites"].values()))


if __name__ == "__main__":
    raise SystemExit(main())
