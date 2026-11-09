/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：单实例 TXT/EPUB/MOBI 书源与章节进度接口。
 * English: Singleton TXT/EPUB/MOBI source and chapter progress interface.
 * 冻结：正文由调用方释放；不依赖 UI。
 * Frozen: The caller frees loaded text; no UI dependencies.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "html_text.h"
typedef enum {
    BOOK_KIND_TXT, ///< 文本 / Plain text
    BOOK_KIND_EPUB, ///< EPUB 容器 / EPUB container
    BOOK_KIND_MOBI, ///< MOBI6 容器 / MOBI6 container
} book_kind_t;
/// 打开并替换当前书源。/ Open and replace the current source.
esp_err_t book_open(const char *path);
/// 关闭书源并释放目录。/ Close the source and free its index.
void book_close(void);
/// 当前书是 EPUB 时切换书内自带字体；TXT/MOBI 无事发生。改动在下一章解析时生效。
/// Toggle embedded faces when the open book is an EPUB; a no-op for TXT/MOBI. The change takes
/// effect when the next chapter is parsed.
void book_set_embedded_fonts(bool on);
/// 返回章节数。/ Return the chapter count.
size_t book_chapter_count(void);
/// 目录映射到原书源章节：EPUB 为 spine，MOBI 为固定分段，TXT 为源章节。
/// Navigation maps to source chapters: EPUB spine entries, fixed MOBI segments, or TXT chapters.
size_t book_navigation_count(void);
size_t book_navigation_chapter(size_t position);
esp_err_t book_navigation_title(size_t position, char *buf, size_t cap);
const char *book_navigation_anchor(size_t position);
size_t book_navigation_source_offset(size_t position);
/// 复制 UTF-8 标题，空间不足返回错误。/ Copy a UTF-8 title, failing if capacity is insufficient.
esp_err_t book_chapter_title(size_t i, char *buf, size_t cap);
/// 复制该章在容器里的文件名（不含目录）；用于判断正文链接是否指向本章。
/// Copy the chapter's container file name without its directory; body links use it to tell
/// whether a target lives in this chapter.
esp_err_t book_chapter_source_name(size_t i, char *buf, size_t cap);
/// 加载 NUL 结尾的 PSRAM UTF-8；调用方 free。/ Load NUL-terminated PSRAM UTF-8; caller frees it.
esp_err_t book_chapter_load(size_t i, char **utf8, size_t *len);
/// 保留 EPUB/MOBI 标题/段落块；TXT 的 blocks 为 NULL，调用方 html_text_free。
/// Preserve EPUB/MOBI heading/paragraph blocks; TXT has NULL blocks; caller uses html_text_free.
esp_err_t book_chapter_load_blocks(size_t i, html_text_t *out);
esp_err_t book_chapter_load_blocks_anchor(size_t i, const char *anchor,
                                           size_t *anchor_offset, html_text_t *out);
esp_err_t book_chapter_load_blocks_target(size_t i, const char *anchor,
                                           size_t source_offset, size_t *text_offset,
                                           html_text_t *out);
/// 读取当前 EPUB/MOBI 章内图片。/ Read an image from the current EPUB/MOBI chapter.
esp_err_t book_chapter_image(size_t chapter, const char *src, uint8_t **data, size_t *size, bool *png);
/// TXT 为源文件字节；EPUB 为 spine 原始 HTML 未压缩累计字节，不是 ZIP 文件大小。
/// TXT uses source-file bytes; EPUB uses cumulative uncompressed spine HTML bytes, not ZIP file size.
/// MOBI 为选定分支的原编码 HTML 总字节。/ MOBI uses original encoded HTML bytes of the selected branch.
uint32_t book_total_bytes(void);
/// TXT 为原文件章起点；EPUB 为前序 spine 原始 HTML 字节总和。
/// TXT uses original chapter offsets; EPUB uses the sum of preceding spine source HTML bytes.
/// MOBI 为该分段在选定分支内的原始字节起点。/ MOBI uses the segment's raw byte start in the selected branch.
uint32_t book_chapter_byte_offset(size_t i);
/// 当前类型；关闭时默认 TXT。/ Current kind, defaulting to TXT when closed.
book_kind_t book_kind(void);

/// 按书源能力启用章节题头，MOBI 不伪装为 EPUB。/ Enable chapter headings by capability without disguising MOBI as EPUB.
static inline bool book_has_chapter_headings(void) { return book_kind() != BOOK_KIND_TXT; }

esp_err_t book_chapter_image_dimensions(size_t chapter, const char *src, unsigned *width, unsigned *height);
