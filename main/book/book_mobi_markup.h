/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：传统 MOBI 的有界标记与字节坐标适配。
 * English: Bounded legacy MOBI markup and byte-coordinate adaptation.
 */
#pragma once
#include "html_text.h"

#define MOBI_TAG_MAX 8192u
#define MOBI_SEGMENT_MAX (256u * 1024u)
#define MOBI_NAV_MAX 2048u
#define MOBI_TITLE_CAP 128u

typedef struct {
    const char *bytes;
    size_t len;
    char name[32];
    bool tag, closing, comment;
} mobi_token_t;

/* 中文：末尾不完整标签返回零，不消耗任何字节。
 * English: Return zero for an incomplete trailing tag without consuming it. */
size_t book_mobi_token(const char *data, size_t len, bool final, mobi_token_t *token);
bool book_mobi_attr(const mobi_token_t *token, const char *name, const char **value, size_t *len);
bool book_mobi_number(const char *value, size_t len, uint32_t *number);
esp_err_t book_mobi_decode(const char *data, size_t len, uint32_t encoding, char **out, size_t *size);
bool book_mobi_utf8(const char *data, size_t len);
esp_err_t book_mobi_markup(const char *raw, size_t len, uint32_t start, uint32_t encoding,
                           uint32_t first_image, uint32_t record_count, const char *anchor,
                           size_t source_offset, size_t *text_offset, html_text_t *out);
