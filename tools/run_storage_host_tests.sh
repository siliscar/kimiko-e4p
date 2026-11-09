#!/bin/sh
# 中文：验证真实后台交还服务、S31 USB、文件卡片及睡眠失败恢复。
# English: Verify real background handoff, S31 USB, file-card actions and sleep failure recovery.
set -eu
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
printf '%s\n' '#pragma once' '#include <stdbool.h>' 'bool book_cover_jobs_quiesce(unsigned);' > "$out/book_cover_jobs.h"
printf '%s\n' '#pragma once' '#include <stdbool.h>' 'bool ui_avatar_quiesce(unsigned);' > "$out/ui_avatar.h"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -I"$out" -Imain -Imain/services tools/storage_jobs_host_test.c main/services/storage_jobs.c -o "$out/test"
"$out/test"
python3 tools/test_usb_storage.py
python3 tools/file_sd_format_host_test.py
python3 tools/sleep_power_host_test.py
python3 tools/cover_jobs_host_test.py
python3 tools/avatar_async_host_test.py
