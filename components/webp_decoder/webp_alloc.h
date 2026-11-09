/* SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：将第三方解码器的动态内存限制到 PSRAM，保持上游源码不变。
 * English: Route decoder allocations to PSRAM without changing upstream sources.
 */
#pragma once
#include <stdlib.h>
#include "esp_heap_caps.h"
static inline void *webp_decoder_malloc(size_t size) {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static inline void *webp_decoder_calloc(size_t count, size_t size) {
    return heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
#define malloc webp_decoder_malloc
#define calloc webp_decoder_calloc
#ifdef free
#undef free
#endif
#define free heap_caps_free
