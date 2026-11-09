#!/usr/bin/env bash
# 中文：分类操作的可复跑主机测试，不连接设备。/ Reproducible category-operation host tests without a device.
set -euo pipefail
mkdir -p build/category-tests
cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Itools/book_storage_stubs -Imain/book \
  tools/category_management_host_test.c main/book/book_category_ops.c main/book/book_path_state.c \
  main/book/book_progress.c main/book/book_title.c -o build/category-tests/operations
build/category-tests/operations
cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Itools/shelf_categories_host_stubs -Itools/book_storage_stubs -Imain/book \
  tools/collection_host_test.c -o build/category-tests/collections
build/category-tests/collections
