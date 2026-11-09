/*
 * 中文：书架目录扫描、历史补入和删除重试合并；同步借用目录，不绘制或导航。
 * English: Shelf directory scanning, history backfill and deletion-retry merging; borrow the catalog synchronously without painting or navigation.
 */
#pragma once
#include "book_shelf_model.h"
#include "nvs.h"

typedef struct {
    book_shelf_entry_t **entries; ///< 页面持有的条目数组，增长时更新 / Page-owned array, updated when grown
    int *count; ///< 已用条目数 / Used entry count
    size_t *capacity; ///< 已分配条目容量 / Allocated entry capacity
} book_shelf_catalog_t;

typedef struct {
    book_shelf_catalog_t catalog; ///< 同步借用；页面负责最终释放 / Synchronous borrow; the page performs final release
    const char *query; ///< 本次搜索文本 / Search text for this call
    bool (*cancel)(void *ctx); ///< 页面轮询导航并报告是否停止；NULL表示不轮询 / Page polls navigation and reports stop; NULL disables polling
    void *cancel_ctx; ///< 取消回调上下文 / Cancellation context
    int (*category_add)(const char *path); ///< 登记卡上书根的一级目录 / Register first-level card book-root folders
    bool (*favorite_read)(nvs_handle_t handle, const char *path); ///< 借用已打开的收藏句柄 / Borrow an open favorites handle
    bool (*hidden_read)(nvs_handle_t handle, const char *path); ///< 借用已打开的隐藏记录句柄 / Borrow an open hidden-record handle
} book_shelf_scan_t;

struct book_shelf_retry;
/// 保留原PSRAM增长策略；失败保持数组和容量不变。/ Keep the original PSRAM growth policy; failure preserves array and capacity.
bool book_shelf_catalog_reserve(const book_shelf_catalog_t *catalog);
/// 使用有界PSRAM目录栈；停止或失败前关闭本次打开的所有目录。
/// Use a bounded PSRAM directory stack; close all directories opened by this call on stop or failure.
void book_shelf_scan_dir(const book_shelf_scan_t *scan, const char *root, bool is_flash,
                         nvs_handle_t favorites, nvs_handle_t hidden,
                         bool *truncated, bool *unreadable, bool *skipped);
/// 按原顺序补入尺寸匹配的历史；返回是否新增，保留原枚举失败处理。
/// Backfill size-matched history in the original order; report additions and retain original enumeration-error handling.
bool book_shelf_scan_backfill(const book_shelf_scan_t *scan, bool *truncated);
/// 同路径重现仍以旧删除清理记录覆盖；不转移重试记录所有权。
/// Old deletion-cleanup records override reappearing paths; retry ownership never transfers.
void book_shelf_scan_retries(const book_shelf_scan_t *scan, const struct book_shelf_retry *retries,
                             bool *truncated);
