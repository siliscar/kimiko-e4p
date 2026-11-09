/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 从阅读页提取的书架条目、筛选、排序和批量选择规则；不访问存储或绘图。
 * Shelf entries, filtering, sorting and selection extracted from the reader page; no I/O or painting.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "book_path.h"

typedef struct {
    char name[256];                 ///< 显示书名。/ Display title.
    char author[128];               ///< 作者。/ Author.
    char path[BOOK_STORE_PATH_MAX]; ///< 完整书源路径。/ Full source path.
    uint32_t size;                 ///< 文件字节数。/ File size in bytes.
    uint16_t chapter;              ///< 已保存章节。/ Saved chapter.
    bool is_flash;                 ///< 内置存储标记。/ Internal-storage marker.
    bool has_progress;             ///< 已有阅读记录。/ Saved reading progress exists.
    uint8_t pct;                   ///< 已保存百分比。/ Saved percentage.
    uint32_t recent;               ///< 最近阅读顺序。/ Recent-reading order.
    bool selected;                 ///< 批量选择。/ Batch selection.
    bool removed;                  ///< 已删文件待清理。/ Deleted file awaiting cleanup.
    bool search_match;             ///< 已计算的搜索匹配结果。/ Precomputed search match.
    bool favorite;                 ///< 收藏标记。/ Favorite marker.
    uint32_t category_id;          ///< 当前书库的逻辑分类，零为未分类。/ Logical category in this library; zero means uncategorized.
    bool new_book;                 ///< 本次完整扫描新发现。/ Newly discovered by this complete scan.
} book_shelf_entry_t;

typedef struct {
    const char *root;          ///< 书架根目录，调用期间借用。/ Shelf root borrowed for the call.
    const char *category_path; ///< 已选目录；NULL 表示全部。/ Selected folder; NULL means all.
    bool uncategorized;       ///< 仅未分类书籍，优先于目录。/ Uncategorized only, before folder selection.
    int filter;               ///< 原筛选值：0 全部、1 卡、2 内置。/ Existing filter: 0 all, 1 card, 2 flash.
    bool recent_sort;         ///< 收藏之后按最近阅读排序。/ Sort by recency after favorites.
    bool logical_categories;  ///< 分类按稳定ID而非文件夹筛选。/ Filter by stable IDs rather than folders.
    uint32_t category_id;     ///< 逻辑分类ID，未分类由uncategorized区分。/ Logical category ID; uncategorized is selected separately.
} book_shelf_query_t;

/// 复用原目录边界规则；无文件访问。/ Apply the original folder boundary rules without I/O.
bool book_shelf_category_matches(const book_shelf_query_t *query, const char *path);
/// 复用原可见性规则，不额外剔除待清理条目。/ Apply original visibility rules, including pending cleanup entries.
bool book_shelf_matches(const book_shelf_query_t *query, const book_shelf_entry_t *entry);
/// 原排序比较规则；不修改条目或查询。/ Original ordering without changing entries or query.
int book_shelf_compare(const book_shelf_query_t *query, const book_shelf_entry_t *a, const book_shelf_entry_t *b);
/// 原地排序并返回可见前缀长度；仅单 UI 任务同步调用，不分配内存。
/// Sort in place and return the visible prefix; synchronous single-UI-task calls only, with no allocation.
int book_shelf_sort(book_shelf_entry_t *entries, int count, const book_shelf_query_t *query);
/// 计入不可见条目的已选状态。/ Include selected entries outside the visible prefix.
size_t book_shelf_selected_count(const book_shelf_entry_t *entries, int count);
/// 清除全部选择。/ Clear all selection.
void book_shelf_clear_selection(book_shelf_entry_t *entries, int count);
/// 只切换可见前缀内的条目。/ Toggle an entry only within the visible prefix.
void book_shelf_toggle_selection(book_shelf_entry_t *entries, int visible_count, int index);
/// 选中批量页的可见条目，保留其他页的选择。/ Select a visible batch page, preserving other selections.
void book_shelf_select_page(book_shelf_entry_t *entries, int visible_count, int page, int rows);
