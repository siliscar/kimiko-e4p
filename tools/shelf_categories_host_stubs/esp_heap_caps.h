// 中文：分类状态宿主测试的分配接口，由测试程序注入失败。/ English: Category-state host allocator interface, with failures injected by each test.
#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *heap_caps_calloc(size_t count, size_t size, int caps);
void *heap_caps_realloc(void *pointer, size_t size, int caps);
