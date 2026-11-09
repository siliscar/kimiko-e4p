/* SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：WebP 宿主回归的分配失败与释放统计；不进入固件。
 * English: Allocation failure and cleanup accounting for host tests only.
 */
#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void *test_heap_malloc(size_t size);
void *test_heap_calloc(size_t count, size_t size);
void *test_heap_realloc(void *ptr, size_t size);
void test_heap_free(void *ptr);
static inline void *heap_caps_malloc(size_t n, int caps) { (void)caps; return test_heap_malloc(n); }
static inline void *heap_caps_calloc(size_t n, size_t s, int caps) { (void)caps; return test_heap_calloc(n, s); }
static inline void *heap_caps_realloc(void *p, size_t n, int caps) { (void)caps; return test_heap_realloc(p, n); }
static inline void heap_caps_free(void *p) { test_heap_free(p); }
static inline size_t heap_caps_get_free_size(int caps) { (void)caps; return 32u * 1024u * 1024u; }
static inline size_t heap_caps_get_largest_free_block(int caps) { (void)caps; return 32u * 1024u * 1024u; }
#define free test_heap_free
