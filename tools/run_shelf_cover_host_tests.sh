#!/usr/bin/env bash
# 中文：运行封面上下文、工作队列及页面集成回归。/ English: Run cover context, worker and page integration regressions.
set -euo pipefail
python3 tools/shelf_cover_state_host_test.py
python3 tools/cover_jobs_host_test.py
python3 tools/shelf_cover_batch_host_test.py
python3 tools/shelf_fast_cache_host_test.py
python3 tools/cover_disk_cache_host_test.py
python3 tools/bookshelf_list_host_test.py
