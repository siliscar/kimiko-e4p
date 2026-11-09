// 宿主可控制 DMA 内存资格。/ Host-controlled DMA memory eligibility.
#pragma once
#include <stdbool.h>
extern bool test_dma_internal;
static inline bool esp_ptr_internal(const void *p) { return p && test_dma_internal; }
