/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
typedef struct { bool mounted; } board_sd_info_t;
esp_err_t board_sd_get_info(board_sd_info_t *);
