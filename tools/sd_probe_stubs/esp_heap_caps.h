#pragma once
#include <stdlib.h>
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_DMA 2
static inline void *heap_caps_malloc(size_t n, int caps) {(void)caps;return malloc(n);}
static inline void *heap_caps_aligned_alloc(size_t alignment, size_t n, int caps) {
    (void)caps;
    // 宿主分配器要求至少指针对齐。/ The host allocator requires at least pointer alignment.
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    return aligned_alloc(alignment, n);
}
static inline void heap_caps_free(void *p) {free(p);}
