#!/bin/sh
# 联网升级的清单、下载与界面宿主回归；不依赖硬件或无线网络。
# Host regression for the online-update manifest, download and UI paths; no hardware or radio.
#
# macOS 的 ASan 运行时会挂住，排查时用 OTA_TEST_NO_SANITIZE=1 关掉。
# This Mac's ASan runtime hangs; set OTA_TEST_NO_SANITIZE=1 to drop it.
set -eu
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
san="-fsanitize=address,undefined"
[ "${OTA_TEST_NO_SANITIZE:-0}" = 1 ] && san=""
cjson=managed_components/espressif__cjson/cJSON
# 按产品目标编译，覆盖 5 MB 双槽与 ESP32-S31 芯片 ID。
# Build for the product target so the 5 MB dual slots and the ESP32-S31 chip ID are covered.
flags="-std=gnu11 -Wall -Wextra -Werror -g -DCONFIG_IDF_TARGET_ESP32S31 \
  -Imain -Imain/ota -Imain/time -Itools/ota_stubs -Itools/ota_stubs/freertos -I$cjson"

# 仅第三方 cJSON 使用系统已弃用的格式化 API；我方代码继续严格检查。
# Only vendored cJSON uses the system's deprecated formatting API; keep our code under strict diagnostics.
cc $san $flags -Wno-deprecated-declarations -c "$cjson/cJSON.c" -o "$out/cjson.o"

cc $san $flags tools/ota_release_host_test.c main/ota/ota_release.c "$out/cjson.o" -o "$out/release"
"$out/release"
# 再跑一次，验证 menuconfig 里的自建下载前缀只放开该前缀下的单文件名。
# Run again with the self-hosted prefix to check only a filename below it is released.
cc $san $flags -DOTA_TEST_EXTRA_ORIGIN \
  '-DPICO_OTA_EXTRA_ORIGIN="https://ota.example.com/files/"' \
  tools/ota_release_host_test.c main/ota/ota_release.c "$out/cjson.o" -o "$out/release_extra"
"$out/release_extra"

cc $san $flags tools/ota_online_host_test.c main/ota/ota_release.c "$out/cjson.o" \
  -o "$out/online" -lpthread
"$out/online"

python3 tools/ota_ui_host_test.py
