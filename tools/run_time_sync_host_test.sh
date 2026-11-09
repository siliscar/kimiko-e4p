#!/bin/sh
# 自动对时的纯判定逻辑：不依赖硬件，只用 main 下的头文件。/ Auto-sync policy only; no hardware includes.
set -eu
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -Imain -Imain/time tools/time_sync_host_test.c -o "$out/time_sync"
"$out/time_sync"
