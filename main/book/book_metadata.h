/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：与当前阅读会话无关的元数据和封面分派。
 * English: Metadata and cover dispatch independent of the current reader session.
 */
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool book_metadata_supported(const char *path);
esp_err_t book_metadata_read(const char *path, char *title, size_t title_cap, char *author, size_t author_cap);
esp_err_t book_metadata_cached(const char *path, char *title, size_t title_cap, char *author, size_t author_cap);
esp_err_t book_metadata_cover_bounded(const char *path, uint8_t **data, size_t *size, bool *png, size_t budget);
