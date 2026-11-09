/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：TXT、EPUB 与 MOBI 单实例书源分派，不依赖界面。
 * English: Singleton TXT, EPUB and MOBI source dispatch without UI dependencies.
 * 冻结：TXT 保留源字节；EPUB 采用累计 spine HTML 字节；MOBI6 固定 v1 原始 HTML 分段；不改原书。
 * Frozen: TXT keeps source bytes; EPUB uses cumulative spine HTML; MOBI6 fixes v1 raw HTML segments; never modify originals.
 * 中文：新增原生 MOBI6 书源，不改变 TXT/EPUB 的既有进度语义。
 * English: Native MOBI6 support preserves existing TXT/EPUB progress semantics.
 */
#include "../diagnostics/reader_perf.h"
#include "../diagnostics/reader_memory.h"
#include "book_source_internal.h"
#include "book_epub.h"
#include "book_mobi.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
static book_txt_t s_book;
static book_epub_t *s_epub;
static book_mobi_t *s_mobi;
esp_err_t book_open(const char *path) {
    book_close();
    if (!path || !*path) return ESP_ERR_INVALID_ARG;
    const char *ext = strrchr(path, '.');
    if (ext && !strcasecmp(ext, ".epub")) return book_epub_open(path, &s_epub);
    if (ext && !strcasecmp(ext, ".mobi")) return book_mobi_open(path, &s_mobi);
    if (!ext || strcasecmp(ext, ".txt")) return ESP_ERR_NOT_SUPPORTED;
    esp_err_t err = book_txt_open(&s_book, path);
    if (err != ESP_OK) book_close();
    return err;
}
void book_close(void) {
    book_epub_close(s_epub); s_epub = NULL;
    book_mobi_close(s_mobi); s_mobi = NULL;
    if (s_book.file) fclose(s_book.file);
    free(s_book.entries); memset(&s_book, 0, sizeof(s_book));
}
void book_set_embedded_fonts(bool on) {
    if (s_epub) book_epub_set_fonts_enabled(s_epub, on);
}
size_t book_chapter_count(void) { return s_mobi ? book_mobi_chapter_count(s_mobi) : s_epub ? book_epub_chapter_count(s_epub) : s_book.count; }
size_t book_navigation_count(void) { return s_mobi ? book_mobi_navigation_count(s_mobi) : s_epub ? book_epub_navigation_count(s_epub) : s_book.count; }
size_t book_navigation_chapter(size_t position) {
    if (s_mobi) return book_mobi_navigation_chapter(s_mobi, position);
    return s_epub ? book_epub_navigation_chapter(s_epub, position) :
                    position < s_book.count ? position : SIZE_MAX;
}
esp_err_t book_navigation_title(size_t position, char *buf, size_t cap) {
    if (s_mobi) return book_mobi_navigation_title(s_mobi, position, buf, cap);
    if (s_epub) return book_epub_navigation_title(s_epub, position, buf, cap);
    return book_chapter_title(position, buf, cap);
}
const char *book_navigation_anchor(size_t position) {
    return s_epub ? book_epub_navigation_anchor(s_epub, position) : NULL;
}
size_t book_navigation_source_offset(size_t position) {
    if (s_mobi) return book_mobi_navigation_source_offset(s_mobi, position);
    return s_epub ? book_epub_navigation_source_offset(s_epub, position) : SIZE_MAX;
}
esp_err_t book_chapter_title(size_t i, char *buf, size_t cap) {
    if (s_mobi) return book_mobi_chapter_title(s_mobi, i, buf, cap);
    if (s_epub) return book_epub_chapter_title(s_epub, i, buf, cap);
    if (!buf || !cap || i >= s_book.count) return ESP_ERR_INVALID_ARG;
    size_t n = strlen(s_book.entries[i].title);
    if (n >= cap) { buf[0] = 0; return ESP_ERR_INVALID_SIZE; }
    memcpy(buf, s_book.entries[i].title, n + 1); return ESP_OK;
}
esp_err_t book_chapter_source_name(size_t i, char *buf, size_t cap) {
    if (buf && cap) buf[0] = 0;
    if (s_mobi) return book_mobi_chapter_name(s_mobi, i, buf, cap);
    if (!s_epub) return ESP_ERR_NOT_SUPPORTED;
    return book_epub_chapter_name(s_epub, i, buf, cap);
}
esp_err_t book_chapter_load(size_t i, char **utf8, size_t *len) {
    if (!utf8 || !len) return ESP_ERR_INVALID_ARG;
    *utf8 = NULL; *len = 0;
    if (s_epub || s_mobi) {
        html_text_t text = {0};
        esp_err_t err = s_mobi ? book_mobi_load_target(s_mobi, i, NULL, SIZE_MAX, NULL, &text) : book_epub_load(s_epub, i, &text);
        if (err != ESP_OK) return err;
        *utf8 = text.utf8; *len = text.len; text.utf8 = NULL;
        html_text_free(&text); return ESP_OK;
    }
    if (!s_book.file || i >= s_book.count) return ESP_ERR_INVALID_ARG;
    return book_txt_load(&s_book, i, utf8, len);
}
esp_err_t book_chapter_load_blocks(size_t i, html_text_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (s_mobi) return book_mobi_load_target(s_mobi, i, NULL, SIZE_MAX, NULL, out);
    if (s_epub) return book_epub_load(s_epub, i, out);
    return book_chapter_load(i, &out->utf8, &out->len);
}
esp_err_t book_chapter_load_blocks_anchor(size_t i, const char *anchor,
                                           size_t *anchor_offset, html_text_t *out) {
    return book_chapter_load_blocks_target(i, anchor, SIZE_MAX, anchor_offset, out);
}
esp_err_t book_chapter_load_blocks_target(size_t i, const char *anchor,
                                           size_t source_offset, size_t *text_offset,
                                           html_text_t *out) {
#if CONFIG_READER_PERF
    READER_PERF_SCOPE(READER_PERF_SOURCE);
#endif
    if (text_offset) *text_offset = 0;
    if (s_mobi) return book_mobi_load_target(s_mobi, i, anchor, source_offset, text_offset, out);
    if (s_epub) return book_epub_load_target(s_epub, i, anchor, source_offset, text_offset, out);
    return book_chapter_load_blocks(i, out);
}
uint32_t book_total_bytes(void) { return s_mobi ? book_mobi_total_bytes(s_mobi) : s_epub ? book_epub_total_bytes(s_epub) : s_book.total; }
uint32_t book_chapter_byte_offset(size_t i) {
    if (s_mobi) return book_mobi_chapter_byte_offset(s_mobi, i);
    return s_epub ? book_epub_chapter_byte_offset(s_epub, i) : i < s_book.count ? s_book.entries[i].offset : 0;
}
book_kind_t book_kind(void) { return s_mobi ? BOOK_KIND_MOBI : s_epub ? BOOK_KIND_EPUB : BOOK_KIND_TXT; }
esp_err_t book_chapter_image(size_t chapter, const char *src, uint8_t **data, size_t *size, bool *png) {
    if (s_mobi) return book_mobi_image(s_mobi, chapter, src, data, size, png);
    return s_epub ? book_epub_image(s_epub, chapter, src, data, size, png) : ESP_ERR_NOT_SUPPORTED;
}

esp_err_t book_chapter_image_dimensions(size_t chapter, const char *src, unsigned *width, unsigned *height) {
    if (s_mobi) return book_mobi_image_dimensions(s_mobi, chapter, src, width, height);
    return s_epub ? book_epub_image_dimensions(s_epub, chapter, src, width, height) : ESP_ERR_NOT_SUPPORTED;
}

#if CONFIG_READER_PERF && CONFIG_READER_MEMORY
void book_source_memory_stats(reader_source_memory_stats_t *stats) {
    *stats = (reader_source_memory_stats_t){0};
    stats->txt_index = reader_memory_owned(s_book.entries);
    stats->open_files = s_book.file != NULL;
    book_epub_memory_stats(s_epub, stats);
    book_mobi_memory_stats(s_mobi, stats);
}
#endif
