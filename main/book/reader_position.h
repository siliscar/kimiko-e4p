/*
 * 中文：阅读会话在当前章节布局中的位置；不直接持久化页码。
 * English: Session position within the current chapter layout; page numbers are not persisted directly.
 */
#pragma once
#include <stddef.h>

typedef struct {
    size_t chapter; ///< 当前书籍的章节索引 / Chapter index in the current book
    size_t page; ///< 当前章节布局中的页索引 / Page index in the current chapter layout
} reader_position_t;
