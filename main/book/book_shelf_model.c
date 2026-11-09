/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 从 app_book 提取书架数据规则；条目内存、封面失效和导航仍由调用方管理。
 * Shelf data rules extracted from app_book; callers retain entry memory, cover invalidation and navigation.
 */
#include "book_shelf_model.h"
#include "book_shelf_categories.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

bool book_shelf_category_matches(const book_shelf_query_t *query, const char *path) {
    if (query->uncategorized) {
        char child[BOOK_STORE_PATH_MAX];
        return !book_shelf_child(query->root, path, child, sizeof(child));
    }
    if (!query->category_path) return true;
    size_t size = strlen(query->category_path);
    return !strncmp(path, query->category_path, size) && path[size] == '/';
}

bool book_shelf_matches(const book_shelf_query_t *query, const book_shelf_entry_t *item) {
    bool category = query->logical_categories ?
        (query->uncategorized ? item->category_id == 0 : !query->category_id || item->category_id == query->category_id) :
        book_shelf_category_matches(query, item->path);
    return category && item->search_match &&
        (query->filter == 0 || (query->filter == 1 && !item->is_flash) ||
         (query->filter == 2 && item->is_flash));
}

int book_shelf_compare(const book_shelf_query_t *query, const book_shelf_entry_t *x, const book_shelf_entry_t *y) {
    if (book_shelf_matches(query, x) != book_shelf_matches(query, y)) return book_shelf_matches(query, x) ? -1 : 1;
    if (x->favorite != y->favorite) return x->favorite ? -1 : 1;
    if (query->recent_sort && x->recent != y->recent) return x->recent > y->recent ? -1 : 1;
    int name = strcasecmp(x->name, y->name);
    return name ? name : strcmp(x->path, y->path);
}

// 沿用单 UI 任务的 qsort；查询仅在同步排序期间借用，返回前清空。
// Retain single-UI-task qsort; borrow the query only during synchronous sorting and clear it before returning.
static const book_shelf_query_t *s_sort_query;
static int compare_entries(const void *a, const void *b) {
    return book_shelf_compare(s_sort_query, a, b);
}

int book_shelf_sort(book_shelf_entry_t *entries, int count, const book_shelf_query_t *query) {
    if (count > 1) {
        s_sort_query = query;
        qsort(entries, count, sizeof(*entries), compare_entries);
        s_sort_query = NULL;
    }
    int visible = 0;
    while (visible < count && book_shelf_matches(query, &entries[visible])) ++visible;
    return visible;
}

size_t book_shelf_selected_count(const book_shelf_entry_t *entries, int count) {
    size_t selected = 0;
    for (int i = 0; i < count; ++i) if (entries[i].selected) ++selected;
    return selected;
}

void book_shelf_clear_selection(book_shelf_entry_t *entries, int count) {
    for (int i = 0; i < count; ++i) entries[i].selected = false;
}

void book_shelf_toggle_selection(book_shelf_entry_t *entries, int visible_count, int index) {
    if (index >= 0 && index < visible_count) entries[index].selected = !entries[index].selected;
}

void book_shelf_select_page(book_shelf_entry_t *entries, int visible_count, int page, int rows) {
    for (int i = page * rows; i < visible_count && i < (page + 1) * rows; ++i) entries[i].selected = true;
}
