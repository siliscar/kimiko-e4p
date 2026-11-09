#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef struct { uint64_t capacity_bytes; } board_sd_info_t;
static inline esp_err_t board_sd_get_info(board_sd_info_t *info) {
    info->capacity_bytes = 64ULL * 1024 * 1024 * 1024;
    return ESP_OK;
}
