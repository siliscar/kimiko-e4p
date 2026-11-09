/* 中文：TF 分类对应一级目录；同卷重命名和有界转移，不递归删除书籍。
 * English: SD categories are direct child folders; same-volume renames and bounded transfers never recursively delete books. */
#pragma once
#include "book_path_state.h"
#include "book_store.h"
#include <stdbool.h>

#define BOOK_CATEGORY_NAME_MAX 80
typedef struct { unsigned moved, total; bool removed; } book_category_result_t;
bool book_category_name_valid(const char *name);
bool book_category_pending(const char *root);
esp_err_t book_category_plan_move(const char *root, const char *source, const char *destination,
                                  char target[BOOK_STORE_PATH_MAX]);
esp_err_t book_category_create(const char *root, const char *name, char out[BOOK_STORE_PATH_MAX]);
esp_err_t book_category_rename(const char *root, const char *source, const char *name, book_path_state_extra_fn extra);
esp_err_t book_category_move(const char *root, const char *source, const char *destination, book_path_state_extra_fn extra);
/// 先检查全部直接文件；有子目录或非书籍内容时拒绝，不开始转移。
/// Preflight every direct file; refuse subfolders or non-book contents before transferring anything.
esp_err_t book_category_remove(const char *root, const char *source, const char *destination,
                               book_category_result_t *result, book_path_state_extra_fn extra);
/// 卡内只有一个未完成操作；先恢复它再允许扫描/新写操作，失败保留日志。
/// One unfinished operation per card; recover before scanning/new mutations, retaining the journal on failure.
esp_err_t book_category_recover(const char *root, book_path_state_extra_fn extra);
