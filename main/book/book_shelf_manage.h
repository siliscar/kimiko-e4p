/*
 * 中文：书架管理执行和删除清理重试；页面保留确认、提示、排序与导航。
 * English: Shelf management execution and deletion-cleanup retries; the page retains confirmation, notices, ordering and navigation.
 */
#pragma once
#include "book_shelf_model.h"
#include "esp_err.h"

typedef struct book_shelf_retry {
    book_shelf_entry_t entry; ///< 已删除但清理未完的原条目 / Original entry whose deletion cleanup is unfinished
    struct book_shelf_retry *next; ///< 跨页面退出保留的重试链 / Retry chain retained across page exits
} book_shelf_retry_t;

typedef struct {
    book_shelf_retry_t **retries; ///< 页面持有的重试链；服务预留和移除节点 / Page-owned retry chain; service reserves and removes nodes
    const char *current_path; ///< 当前会话路径；close可原地清空 / Current session path; close may clear it in place
    unsigned *store_revision; ///< 删除后同步原存储版本 / Synchronize the existing store revision after removal
    void (*close)(void); ///< 修改当前书前交还阅读资源 / Hand back reading resources before modifying the current book
    void (*discard_progress)(const char *path); ///< 清理前丢弃此路径的待保存进度 / Drop this path's pending progress before cleanup
} book_shelf_management_t;

typedef struct {
    esp_err_t error; ///< 原存储或进度清理结果 / Original store or progress-cleanup result
    bool reserve_failed; ///< 删除前预留失败，未调用删除 / Pre-deletion reservation failed; deletion was not called
} book_shelf_manage_result_t;

typedef enum {
    BOOK_SHELF_DELETE, ///< 删除文件并清进度 / Delete file and forget progress
    BOOK_SHELF_CLEAR, ///< 仅清进度 / Forget progress only
    BOOK_SHELF_HIDE, ///< 仅移出书架 / Hide from shelf only
} book_shelf_manage_kind_t;

typedef struct {
    unsigned done; ///< 成功数量 / Successful count
    unsigned failed; ///< 失败数量 / Failed count
} book_shelf_batch_result_t;

/// 只读借用节点；节点被成功清理后失效。/ Borrow a node read-only until successful cleanup removes it.
const book_shelf_retry_t *book_shelf_retry_find(const book_shelf_retry_t *retries, const char *path);
/// 同步执行；所有参数有效。先交还当前书再预留；已删文件仅重试进度清理。
/// Execute synchronously with valid arguments. Close the current book before reservation; already-deleted files retry progress cleanup only.
/// 文件删除后先标记重试、丢弃待保存进度和通知版本，再调用removed钩子；只有成功才移除重试。
/// After removal mark retry, drop pending progress and notify revision before the removed hook; remove retries only on success.
book_shelf_manage_result_t book_shelf_manage_one(const book_shelf_management_t *management,
    const book_shelf_entry_t *entry, bool delete_file, bool *removed,
    void (*after_removed)(void *ctx, const char *path), void *ctx);
/// 原地压紧条目；失败项保留选择和重试，隐藏操作不交还书源或改进度。
/// Compact entries in place; failures retain selection/retry, and hiding neither closes sources nor changes progress.
book_shelf_batch_result_t book_shelf_manage_batch(const book_shelf_management_t *management,
    book_shelf_entry_t *entries, int *count, book_shelf_manage_kind_t kind,
    bool (*hide)(const char *path, bool hidden));
