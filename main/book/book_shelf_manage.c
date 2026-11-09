/*
 * 中文：同步执行书架管理，保留删除预留、元数据重试和批量压紧顺序。
 * English: Execute shelf management synchronously, retaining deletion reservation, metadata retries and batch compaction order.
 */
#include "book_shelf_manage.h"
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "book_store.h"
#include "book_progress.h"

const book_shelf_retry_t *book_shelf_retry_find(const book_shelf_retry_t *retries, const char *path) {
    for (const book_shelf_retry_t *p = retries; p; p = p->next)
        if (!strcmp(p->entry.path, path)) return p;
    return NULL;
}

// 在删除文件前预留记录，避免删除成功后才遇到内存不足。
// Reserve before unlink so allocation failure never loses a completed deletion's retry.
static book_shelf_retry_t *delete_retry_reserve(book_shelf_retry_t **retries, const book_shelf_entry_t *entry) {
    for (book_shelf_retry_t *p = *retries; p; p = p->next)
        if (!strcmp(p->entry.path, entry->path)) return p;
    book_shelf_retry_t *p = heap_caps_malloc(sizeof(*p), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = malloc(sizeof(*p));
    if (!p) return NULL;
    p->entry = *entry;
    p->next = *retries;
    *retries = p;
    return p;
}
static void delete_retry_discard(book_shelf_retry_t **retries, const char *path) {
    book_shelf_retry_t **p = retries;
    while (*p) {
        if (!strcmp((*p)->entry.path, path)) {
            book_shelf_retry_t *old = *p;
            *p = old->next;
            free(old);
            return;
        }
        p = &(*p)->next;
    }
}

book_shelf_manage_result_t book_shelf_manage_one(const book_shelf_management_t *management,
    const book_shelf_entry_t *entry, bool delete_file, bool *removed,
    void (*after_removed)(void *ctx, const char *path), void *ctx) {
    if (!strcmp(management->current_path, entry->path)) management->close();
    esp_err_t err;
    if (delete_file && !*removed) {
        book_shelf_retry_t *retry = delete_retry_reserve(management->retries, entry);
        if (!retry) return (book_shelf_manage_result_t){ESP_ERR_NO_MEM, true};
        bool file_removed = false;
        err = book_store_delete(entry->path, &file_removed);
        if (file_removed) {
            retry->entry.removed = true;
            *removed = true;
            management->discard_progress(entry->path);
            book_store_notify_changed();
            *management->store_revision = book_store_revision();
            if (after_removed) after_removed(ctx, entry->path);
        } else delete_retry_discard(management->retries, entry->path);
    } else {
        management->discard_progress(entry->path);
        err = book_progress_forget(entry->path);
    }
    if (err == ESP_OK) delete_retry_discard(management->retries, entry->path);
    return (book_shelf_manage_result_t){err, false};
}

book_shelf_batch_result_t book_shelf_manage_batch(const book_shelf_management_t *management,
    book_shelf_entry_t *entries, int *count, book_shelf_manage_kind_t kind,
    bool (*hide)(const char *path, bool hidden)) {
    book_shelf_batch_result_t result = {0};
    int write = 0;
    const bool unshelf = kind == BOOK_SHELF_HIDE;
    for (int i = 0; i < *count; ++i) {
        book_shelf_entry_t item = entries[i];
        bool discard = false;
        if (item.selected) {
            esp_err_t err;
            if (unshelf) {
                // 隐藏只改书架记录，文件与进度保持原样。
                // Hiding changes shelf records only; files and progress remain intact.
                discard = hide(item.path, true);
                err = discard ? ESP_OK : ESP_FAIL;
                if (err == ESP_OK) delete_retry_discard(management->retries, item.path);
            } else {
                book_shelf_manage_result_t applied = book_shelf_manage_one(management, &item,
                    kind == BOOK_SHELF_DELETE, &item.removed, NULL, NULL);
                if (applied.reserve_failed) { ++result.failed; entries[write++] = item; continue; }
                err = applied.error;
            }
            if (err == ESP_OK) {
                ++result.done; item.selected = false;
                if (!unshelf) {
                    item.has_progress = false; item.pct = 0;
                    item.chapter = 0; item.recent = 0;
                }
                discard = discard || item.removed;
            } else ++result.failed;
        }
        if (!discard) entries[write++] = item;
    }
    *count = write;
    return result;
}
