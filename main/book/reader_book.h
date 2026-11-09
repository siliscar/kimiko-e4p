/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：从阅读页面提取当前书籍信息；仅内存状态，不改变进度或书签的持久格式。
 * English: Current-book metadata extracted from the book page; memory only, without changing progress or bookmark formats.
 */
#pragma once
#include <stdint.h>
#include "book_path.h"

typedef struct {
    char path[BOOK_STORE_PATH_MAX]; ///< 开书成功后记录；关闭书源后失效。/ Set after source open succeeds; invalidated after source close.
    char title[128]; ///< 文件名清理或书名索引得到的书名。/ Book title from the title index or cleaned filename.
    char chapter_title[128]; ///< 成功加载章节或目录目标后由页面更新。/ Updated by the page after chapter load or TOC targeting succeeds.
    uint32_t file_size; ///< 当前文件尺寸，用于核对进度与书签归属。/ Current file size for progress and bookmark ownership checks.
} reader_book_t;
