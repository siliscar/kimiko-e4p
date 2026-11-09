/*
 * 中文：有界目录遍历、书籍候选和历史补入；页面持有目录数组和导航状态。
 * English: Bounded directory traversal, book candidates and history backfill; the page owns the catalog array and navigation state.
 */
#include "book_shelf_scan.h"
#include "book_shelf_manage.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "book_file_path.h"
#include "book_metadata.h"
#include "book_progress.h"
#include "book_title.h"
#include "boot_state.h"
#include "book_search.h"
#include "settings.h"

// 截断必须停在UTF8字符边界。/ Truncation must stop at a UTF8 character boundary.
static void copy_text(char *dst, size_t cap, const char *src) {
    if (!cap) return;
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n && ((unsigned char)src[n] & 0xc0) == 0x80) --n;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}
static void clean_filename(char *dst, size_t cap, const char *filename) {
    copy_text(dst, cap, filename);
    char *ext = strrchr(dst, '.');
    if (ext) *ext = 0;
    char *start = dst;
    while (*start == ' ' || *start == '\t') ++start;
    if (start != dst) memmove(dst, start, strlen(start) + 1);
    size_t len = strlen(dst);
    while (len && (dst[len - 1] == ' ' || dst[len - 1] == '\t')) dst[--len] = 0;
}

bool book_shelf_catalog_reserve(const book_shelf_catalog_t *catalog) {
    if ((size_t)*catalog->count < *catalog->capacity) return true;
    if (*catalog->count == INT_MAX) return false;
    size_t cap = *catalog->capacity ? *catalog->capacity * 2 : 32;
    if (cap > INT_MAX || cap > SIZE_MAX / sizeof(**catalog->entries)) return false;
    book_shelf_entry_t *entries = heap_caps_realloc(*catalog->entries, cap * sizeof(*entries),
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!entries) return false;
    *catalog->entries = entries;
    *catalog->capacity = cap;
    return true;
}

// 显式目录栈放在PSRAM，保持原深度上限和逐项导航轮询。
// Keep explicit directory frames in PSRAM with the original depth limit and per-entry navigation polling.
#define BOOK_SCAN_DEPTH_LIMIT 12
typedef struct {
    DIR *dir;
    char path[BOOK_STORE_PATH_MAX];
} shelf_scan_frame_t;

void book_shelf_scan_dir(const book_shelf_scan_t *scan, const char *root, bool is_flash,
                         nvs_handle_t favorites, nvs_handle_t hidden,
                         bool *truncated, bool *unreadable, bool *skipped) {
    if (*truncated) return;
    shelf_scan_frame_t *frames = heap_caps_calloc(BOOK_SCAN_DEPTH_LIMIT + 1, sizeof(*frames),
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!frames) { *truncated = true; return; }
    size_t root_len = strnlen(root, sizeof(frames[0].path));
    if (root_len >= sizeof(frames[0].path)) { *skipped = true; free(frames); return; }
    frames[0].dir = opendir(root);
    if (!frames[0].dir) { *unreadable = true; free(frames); return; }
    memcpy(frames[0].path, root, root_len + 1);
    int depth = 0;
    while (depth >= 0 && !*truncated) {
        if (scan->cancel && scan->cancel(scan->cancel_ctx)) break;
        errno = 0;
        struct dirent *ent = readdir(frames[depth].dir);
        if (!ent) {
            if (errno) *unreadable = true;
            closedir(frames[depth].dir);
            frames[depth].dir = NULL;
            --depth;
            continue;
        }
        if (ent->d_name[0] == '.') continue;
        char path[BOOK_STORE_PATH_MAX];
        int len = snprintf(path, sizeof(path), "%s/%s", frames[depth].path, ent->d_name);
        if (len < 0 || (size_t)len >= sizeof(path)) { *skipped = true; continue; }
        struct stat st;
        if (stat(path, &st) != 0) { *unreadable = true; continue; }
        if (S_ISDIR(st.st_mode)) {
            if (!is_flash && depth == 0 && !strcmp(root, app_settings_books_dir())) scan->category_add(path);
            if (depth == BOOK_SCAN_DEPTH_LIMIT) { *skipped = true; continue; }
            DIR *child = opendir(path);
            if (!child) { *unreadable = true; continue; }
            ++depth;
            frames[depth].dir = child;
            memcpy(frames[depth].path, path, (size_t)len + 1);
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue;
        if (!book_file_path_supported(path)) continue;
        // 移出书架只隐藏文件，重新打开才恢复上架。
        // Removal hides a file from the shelf until the book is reopened.
        if (hidden && scan->hidden_read(hidden, path)) continue;
        if (strlen(ent->d_name) >= sizeof(((book_shelf_entry_t *)0)->name) ||
            st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX) { *skipped = true; continue; }
        if (!book_shelf_catalog_reserve(&scan->catalog)) { *truncated = true; break; }
        book_shelf_entry_t candidate = {0};
        book_shelf_entry_t *item = &candidate;
        copy_text(item->path, sizeof(item->path), path);
        if (!book_title_from_path(item->path, item->name, sizeof(item->name)))
            clean_filename(item->name, sizeof(item->name), ent->d_name);
        if (book_metadata_supported(item->path)) {
            char title[sizeof(item->name)] = {0};
            char author[sizeof(item->author)] = {0};
            // 首屏只读已有元数据缓存，不逐本解析正文。
            // First paint reads cached metadata only, without scanning every book.
            if (book_metadata_cached(item->path, title, sizeof(title), author, sizeof(author)) == ESP_OK)
                copy_text(item->author, sizeof(item->author), author);
        }
        item->search_match = book_search_match(item->name, scan->query);
        item->size = st.st_size;
        item->is_flash = is_flash;
        book_progress_t p;
        item->has_progress = book_progress_load(item->path, item->size, &p);
        item->pct = item->has_progress ? p.pct : 0;
        item->chapter = item->has_progress ? p.chapter : 0;
        item->recent = item->has_progress ? p.last_open_s : 0;
        item->favorite = favorites && scan->favorite_read(favorites, item->path);
        (*scan->catalog.entries)[(*scan->catalog.count)++] = candidate;
    }
    for (int i = 0; i <= depth; ++i)
        if (frames[i].dir) closedir(frames[i].dir);
    free(frames);
}

typedef struct {
    const book_shelf_scan_t *scan;
    nvs_handle_t favorites, hidden;
    bool truncated;
} shelf_backfill_t;

static bool shelf_backfill_visit(const char *path, const book_progress_t *progress, void *ctx) {
    shelf_backfill_t *backfill = ctx;
    const book_shelf_scan_t *scan = backfill->scan;
    if (backfill->truncated) return false;
    if (!book_file_path_supported(path)) return true;
    for (int i = 0; i < *scan->catalog.count; ++i)
        if (!strcmp((*scan->catalog.entries)[i].path, path)) return true;
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        (uint64_t)st.st_size > UINT32_MAX || (uint32_t)st.st_size != progress->file_size)
        return true;
    if (backfill->hidden && scan->hidden_read(backfill->hidden, path)) return true;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (strlen(base) >= sizeof(((book_shelf_entry_t *)0)->name)) return true;
    if (!book_shelf_catalog_reserve(&scan->catalog)) { backfill->truncated = true; return false; }
    book_shelf_entry_t candidate = {0};
    book_shelf_entry_t *item = &candidate;
    copy_text(item->path, sizeof(item->path), path);
    if (!book_title_from_path(item->path, item->name, sizeof(item->name)))
        clean_filename(item->name, sizeof(item->name), base);
    if (!item->name[0]) return true;
    if (book_metadata_supported(item->path) && pico_boot_asset_allowed(item->path)) {
        char title[sizeof(item->name)] = {0};
        char author[sizeof(item->author)] = {0};
        if (book_metadata_cached(item->path, title, sizeof(title), author, sizeof(author)) == ESP_OK)
            copy_text(item->author, sizeof(item->author), author);
    }
    item->size = (uint32_t)st.st_size;
    item->is_flash = !strncmp(path, "/flash/", 7);
    item->has_progress = true;
    item->pct = progress->pct;
    item->chapter = progress->chapter;
    item->recent = progress->last_open_s;
    item->search_match = book_search_match(item->name, scan->query);
    item->favorite = backfill->favorites && scan->favorite_read(backfill->favorites, item->path);
    (*scan->catalog.entries)[(*scan->catalog.count)++] = candidate;
    return true;
}

bool book_shelf_scan_backfill(const book_shelf_scan_t *scan, bool *truncated) {
    nvs_handle_t favorites = 0, hidden = 0;
    (void)nvs_open("rp_favs", NVS_READONLY, &favorites);
    (void)nvs_open("rp_shelf", NVS_READONLY, &hidden);
    const int before = *scan->catalog.count;
    shelf_backfill_t backfill = {.scan = scan, .favorites = favorites, .hidden = hidden};
    (void)book_progress_list(shelf_backfill_visit, &backfill);
    if (favorites) nvs_close(favorites);
    if (hidden) nvs_close(hidden);
    if (truncated && backfill.truncated) *truncated = true;
    return *scan->catalog.count != before;
}

void book_shelf_scan_retries(const book_shelf_scan_t *scan, const struct book_shelf_retry *retries,
                             bool *truncated) {
    for (const book_shelf_retry_t *p = retries; p; p = p->next) {
        int i = 0;
        while (i < *scan->catalog.count && strcmp((*scan->catalog.entries)[i].path, p->entry.path)) ++i;
        if (i == *scan->catalog.count) {
            if (!book_shelf_catalog_reserve(&scan->catalog)) { *truncated = true; continue; }
            ++*scan->catalog.count;
        }
        (*scan->catalog.entries)[i] = p->entry;
        (*scan->catalog.entries)[i].selected = false;
        (*scan->catalog.entries)[i].search_match = book_search_match(p->entry.name, scan->query);
    }
}
