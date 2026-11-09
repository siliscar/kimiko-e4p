#!/usr/bin/env python3
"""中文：天气的真实模型与可取消服务回归，不联网。
English: Real weather model and cancellable service regressions without networking.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
service_source = (ROOT / "main/services/weather_service.c").read_text()
assert '"request start origin=' not in service_source
assert '"request finish phase=' not in service_source
with tempfile.TemporaryDirectory(prefix="weather-host-") as directory:
    work = Path(directory)
    env, compiler = toolchain(work)
    flags = ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-g", "-fsanitize=address,undefined"]
    includes = ["-Imain/services", "-Itools", "-Imanaged_components/espressif__cjson/cJSON"]
    cjson = work / "cjson.o"
    subprocess.run(compiler + flags + includes + ["-Wno-deprecated-declarations", "-c",
        "managed_components/espressif__cjson/cJSON/cJSON.c", "-o", str(cjson)], cwd=ROOT, env=env, check=True)
    binary = work / "model"
    subprocess.run(compiler + flags + includes + ["tools/weather_model_host_test.c",
        "main/services/weather_model.c", str(cjson), "-lm", "-o", str(binary)], cwd=ROOT, env=env, check=True)
    subprocess.run([str(binary)], env=env, check=True)
    shutil.copytree(ROOT / "tools/ota_stubs", work / "stubs")
    stubs = work / "stubs"
    (stubs / "nvs.h").write_text((ROOT / "tools/book_storage_stubs/nvs.h").read_text() +
        "\n#define ESP_ERR_NVS_NOT_FOUND 0x1102\n")
    (stubs / "device_power.h").write_text("""#pragma once
#include <stdbool.h>
typedef struct {int battery;} pmu_snapshot_t;
const pmu_snapshot_t *device_power_get(void);
int pmu_battery_percent(const pmu_snapshot_t *);
bool pmu_battery_charging(const pmu_snapshot_t *);
""")
    (stubs / "ble_page_turner.h").write_text("#pragma once\n#include <stdbool.h>\nbool ble_pt_connected(void);\n")
    with (stubs / "wifi_transfer.h").open("a") as output:
        output.write("\n#define WIFI_TRANSFER_STOPPED 0\n#define WIFI_TRANSFER_READY 2\n")
    with (stubs / "esp_http_client.h").open("a") as output:
        output.write("\nesp_err_t esp_http_client_close(esp_http_client_handle_t);\n")
    binary = work / "service"
    subprocess.run(compiler + flags + includes + ["-I" + str(stubs), "-Imain/time", "-Imain/ota",
        "tools/weather_service_host_test.c", "main/services/weather_model.c", str(cjson),
        "-lm", "-o", str(binary)], cwd=ROOT, env=env, check=True)
    subprocess.run([str(binary)], env=env, check=True)
    subprocess.run([os.sys.executable, str(ROOT / "tools/weather_raster_host_test.py")], cwd=ROOT, env=env, check=True)
    subprocess.run([os.sys.executable, str(ROOT / "tools/weather_navigation_host_test.py")], cwd=ROOT, env=env, check=True)
    subprocess.run([os.sys.executable, str(ROOT / "tools/home_components_host_test.py")], cwd=ROOT, env=env, check=True)

# 共享系统回归保留原行为；这里单独核对新增交还和调度守卫的真实入口。
# Existing system regressions retain their behavior; verify new handoff/scheduling entries separately.
from home_cover_cache_host_test import function
for name, path, following in (
    ("quick_toggle_wifi", "main/app/app_quick_layer.c", "wifi_transfer_get_status"),
    ("app_lifecycle_pause_for_lock", "main/app/app_lifecycle.c", "wifi_transfer_pause_for_sleep"),
    ("app_lifecycle_prepare_poweroff", "main/app/app_lifecycle.c", "wifi_transfer_pause_for_sleep"),
):
    body = function(name, ROOT / path)
    assert body.index("app_weather_cancel_join") < body.index(following)
loop = (ROOT / "main/app/app_loop.c").read_text()
assert "pico_ota_auto_poll(!app_weather_busy()" in loop
assert "app_time_service_poll(!app_weather_busy()" in loop
for page in ("main/apps/app_dashboard.c", "main/apps/app_components.c"):
    assert ".on_before_lock=" in (ROOT / page).read_text()
print("PASS: weather scheduling and handoff entries for quick WiFi, lock, shutdown and both pages")
