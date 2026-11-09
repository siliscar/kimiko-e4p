// 宿主缓存对齐契约。/ Host cache alignment contract.
#pragma once
#include <stddef.h>
#include "esp_err.h"
static inline esp_err_t esp_cache_get_alignment(unsigned caps, size_t *alignment) {
    (void)caps;
    *alignment = 64;
    return ESP_OK;
}
