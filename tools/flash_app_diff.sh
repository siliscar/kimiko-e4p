#!/bin/bash
# 增量刷应用分区：把新编出的应用镜像与上次已刷的基线（build/Read_Pico_flashed.bin）
# 按 4KB 扇区求差，只写差异扇区；差异超过一半时自动退回整包刷写。
# 约束：写粒度必须是完整 4KB 扇区——esptool 写前按扇区擦除，跨扇区零散写会
# 把同扇区其余字节清成 0xFF，所以差集先对齐扩展成整扇区再写。
# 首次使用先整刷一次建立基线；基线在每次成功刷写后自动更新为当前镜像。
# NVS 在尾部 0xFF8000，本脚本只写 0x10000 起的应用区，永不触碰 NVS。
# Incremental app-partition flash: diff the freshly built app image against the
# last-flashed baseline (build/Read_Pico_flashed.bin) by 4KB sectors and write
# only the dirty sectors; falls back to a full flash when more than half the
# image differs. Constraint: writes must cover whole 4KB sectors — esptool
# erases per sector before writing, so scattered partial-sector writes would
# wipe the rest of each sector to 0xFF; the diff is therefore expanded to full
# sectors. Establish the baseline with one full flash first; it is refreshed to
# the current image after every successful write. NVS lives at the tail
# 0xFF8000 and is never touched: this script only writes the app region from
# 0x10000.
#
# 用法 / usage:
#   PORT=/dev/cu.usbmodemXXXX tools/flash_app_diff.sh          # 增量 / incremental
#   PORT=/dev/cu.usbmodemXXXX tools/flash_app_diff.sh --full   # 整包 / full
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
PY="${ESPTOOL_PY:-$HOME/.espressif/python_env/idf6.1_py3.13_env/bin/python}"
PORT="${PORT:-/dev/cu.usbmodem31101}"
CHIP_ARGS=(--chip esp32s31 -p "$PORT" -b 460800
           --before default-reset --after hard-reset --no-stub)

lsof "$PORT" >/dev/null 2>&1 && { echo "port occupied: $PORT"; exit 1; }
[ -f "$BUILD/Read_Pico.bin" ] || { echo "missing $BUILD/Read_Pico.bin; run idf.py build first"; exit 1; }

full_flash() {
    (cd "$BUILD" && "$PY" -m esptool "${CHIP_ARGS[@]}" merge-bin \
        -o merged-binary.bin @flash_args)
    "$PY" -m esptool "${CHIP_ARGS[@]}" write-flash 0x0 "$BUILD/merged-binary.bin"
    cp "$BUILD/Read_Pico.bin" "$BUILD/Read_Pico_flashed.bin"
}

if [ "${1:-}" = "--full" ]; then
    full_flash
    exit 0
fi

[ -f "$BUILD/Read_Pico_flashed.bin" ] || {
    echo "no baseline $BUILD/Read_Pico_flashed.bin; run with --full once first"; exit 1;
}

# 求差并输出 esptool 参数："偏移 文件 ..."；CLEAN=无差异，FULL=差异过大。
# Diff and print esptool arguments "offset file ..."; CLEAN=no delta,
# FULL=delta too large to bother.
PLAN="$("$PY" - "$BUILD/Read_Pico_flashed.bin" "$BUILD/Read_Pico.bin" <<'EOF'
import json, sys
old = bytearray(open(sys.argv[1], "rb").read())
new = bytearray(open(sys.argv[2], "rb").read())
n = max(len(old), len(new))
old += b"\xff" * (n - len(old))
new += b"\xff" * (n - len(new))
S = 4096
sectors = n // S + (1 if n % S else 0)
dirty = [i for i in range(sectors) if old[i*S:(i+1)*S] != new[i*S:(i+1)*S]]
if not dirty:
    print("CLEAN"); raise SystemExit
spans = []
for i in dirty:
    if spans and i == spans[-1][1]:
        spans[-1][1] = i + 1
    else:
        spans.append([i, i + 1])
if sum((b - a) * S for a, b in spans) * 2 > n:
    print("FULL"); raise SystemExit
pairs = []
for k, (a, b) in enumerate(spans):
    path = f"{sys.argv[2]}.diff{k}.bin"
    open(path, "wb").write(bytes(new[a*S:b*S]))
    pairs += [str(0x10000 + a * S), path]
print(" ".join(pairs))
EOF
)"

case "$PLAN" in
    CLEAN) echo "app image unchanged; nothing to flash"; exit 0;;
    FULL)  echo "diff too large, falling back to full flash"; full_flash; exit 0;;
esac

read -r -a PAIRS <<< "$PLAN"
"$PY" -m esptool "${CHIP_ARGS[@]}" write-flash "${PAIRS[@]}"
rm -f "$BUILD"/Read_Pico.bin.diff*.bin
cp "$BUILD/Read_Pico.bin" "$BUILD/Read_Pico_flashed.bin"
echo "incremental flash done: $(( ${#PAIRS[@]} / 2 )) span(s)"
