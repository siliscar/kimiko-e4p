/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：无 DRM MOBI6 书源；双格式文件固定使用 MOBI6 分支。
 * English: DRM-free MOBI6 source; hybrids always use the MOBI6 rendition.
 * 冻结：v1 分段与规范化坐标用于持久化进度；改变前必须迁移进度与书签。
 * Frozen: v1 segment/normalization coordinates persist; migrate progress/bookmarks before changing them.
 */
#pragma once
#include "html_text.h"

typedef struct book_mobi book_mobi_t;
esp_err_t book_mobi_open(const char *path, book_mobi_t **out);
void book_mobi_close(book_mobi_t *book);
size_t book_mobi_chapter_count(const book_mobi_t *book);
size_t book_mobi_navigation_count(const book_mobi_t *book);
size_t book_mobi_navigation_chapter(const book_mobi_t *book, size_t position);
size_t book_mobi_navigation_source_offset(const book_mobi_t *book, size_t position);
esp_err_t book_mobi_navigation_title(const book_mobi_t *book, size_t position, char *buf, size_t cap);
esp_err_t book_mobi_chapter_title(const book_mobi_t *book, size_t index, char *buf, size_t cap);
esp_err_t book_mobi_chapter_name(const book_mobi_t *book, size_t index, char *buf, size_t cap);
esp_err_t book_mobi_load_target(book_mobi_t *book, size_t index, const char *anchor,
                                size_t source_offset, size_t *text_offset, html_text_t *out);
uint32_t book_mobi_total_bytes(const book_mobi_t *book);
uint32_t book_mobi_chapter_byte_offset(const book_mobi_t *book, size_t index);
esp_err_t book_mobi_image(book_mobi_t *book, size_t chapter, const char *src,
                          uint8_t **data, size_t *size, bool *png);
esp_err_t book_mobi_image_dimensions(book_mobi_t *book, size_t chapter, const char *src,
                                     unsigned *width, unsigned *height);
esp_err_t book_mobi_cover_bounded(const char *path, uint8_t **data, size_t *size, bool *png, size_t budget);
esp_err_t book_mobi_metadata(const char *path, char *title, size_t title_cap, char *author, size_t author_cap);
esp_err_t book_mobi_metadata_cached(const char *path, char *title, size_t title_cap, char *author, size_t author_cap);
