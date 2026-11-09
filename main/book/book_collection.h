/* 中文：独立书架分类、归属与双副本保存；不移动书籍、不修改阅读记录。
 * English: Independent shelf categories, assignments and checked dual-copy storage; never move books or rewrite reading records. */
#pragma once
#include "book_shelf_model.h"
#include "esp_err.h"
#include <stdio.h>

#define BOOK_COLLECTION_CATEGORIES 126
#define BOOK_COLLECTION_BOOKS 8192
#define BOOK_COLLECTION_NAME_CHARS 24
#define BOOK_COLLECTION_NAME_BYTES 97
#define BOOK_COLLECTION_ALL UINT32_MAX

typedef struct {
    uint32_t id;
    char name[BOOK_COLLECTION_NAME_BYTES];
    char legacy_name[BOOK_STORE_PATH_MAX]; ///< 仅作一次迁移来源，不再推导归属。/ One-time migration provenance, never a live assignment rule.
} book_collection_category_t;
typedef struct book_collection_data book_collection_data_t;
typedef struct {
    book_collection_data_t *data, *undo;
    char prefix[BOOK_STORE_PATH_MAX];
    uint32_t generation;
    int slot;
    bool ready, blocked, recovered;
} book_collection_t;

/// 调用方先挂载介质；坏索引不自动清空，允许从另一副本恢复。/ Mount storage first; never clear a corrupt index automatically, and recover from the other copy when possible.
esp_err_t book_collection_open(book_collection_t *state, const char *prefix);
void book_collection_close(book_collection_t *state);
void book_collection_drop_undo(book_collection_t *state);
bool book_collection_can_undo(const book_collection_t *state);
bool book_collection_migrated(const book_collection_t *state);
int book_collection_count(const book_collection_t *state);
const book_collection_category_t *book_collection_at(const book_collection_t *state, int ordinal);
const book_collection_category_t *book_collection_find(const book_collection_t *state, uint32_t id);
/// 查找原目录名的迁移ID，便于首次保留旧分类选择。/ Look up a legacy folder's migrated ID to preserve the initial selection.
uint32_t book_collection_legacy_id(const book_collection_t *state, const char *name);
uint32_t book_collection_assignment(const book_collection_t *state, const char *path);
/// 验证并去除首尾空白，按Unicode字符计数。/ Validate and trim surrounding spaces, counting Unicode characters.
bool book_collection_name(const char *input, char out[BOOK_COLLECTION_NAME_BYTES]);
/// 完整扫描后一次迁移旧目录并记录新路径；不删除暂时离线的记录。/ Migrate legacy folders and register new paths after a complete scan; retain temporarily offline records.
esp_err_t book_collection_sync(book_collection_t *state, const char *root,
                               const char *const *folders, size_t folder_count,
                               book_shelf_entry_t *books, int count, bool complete);
/// 同一事务可新建分类并归入既有选择；失败保留原分类和勾选。/ Create and optionally classify the existing selection in one transaction; failure preserves categories and selection.
esp_err_t book_collection_create(book_collection_t *state, const char *name,
                                 const book_shelf_entry_t *books, int count, bool selected,
                                 uint32_t *id, unsigned *changed);
esp_err_t book_collection_assign(book_collection_t *state, const book_shelf_entry_t *books,
                                 int count, bool selected, uint32_t target, unsigned *changed);
/// 仅将来源分类内的勾选书变为未分类；其他筛选留下的勾选不受影响。/ Unclassify selected books in the source only, without affecting selections from other filters.
esp_err_t book_collection_unclassify(book_collection_t *state, const book_shelf_entry_t *books,
                                     int count, uint32_t source, unsigned *changed);
esp_err_t book_collection_rename(book_collection_t *state, uint32_t id, const char *name);
esp_err_t book_collection_remove(book_collection_t *state, uint32_t id);
esp_err_t book_collection_order(book_collection_t *state, uint32_t id, int direction);
esp_err_t book_collection_undo(book_collection_t *state);
/// 将已提交归属同步到借用书目，纯内存操作。/ Apply committed assignments to a borrowed catalog, in memory only.
void book_collection_apply(const book_collection_t *state, book_shelf_entry_t *books, int count);
/// 独立的有校验流，用于版本化备份。/ Independent checked stream for versioned backups.
esp_err_t book_collection_export(const book_collection_t *state, FILE *file);
esp_err_t book_collection_import(book_collection_t *state, FILE *file);
