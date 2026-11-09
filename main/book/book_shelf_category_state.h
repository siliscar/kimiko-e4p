/*
 * 中文：书架分类的独立状态；调用方零初始化并通过接口访问，不持有书目或页面资源。
 * English: Independent shelf-category state; zero-initialize and use the API, without borrowing catalogs or page resources.
 */
#pragma once
#include "book_shelf_model.h"
#include "book_collection.h"

#define BOOK_SHELF_CATEGORY_LIMIT 128
#define BOOK_SHELF_CATEGORY_MENU_ROWS 7

typedef struct book_shelf_category_entry book_shelf_category_entry_t;
typedef struct {
    book_shelf_category_entry_t *entries; ///< 模块独占的目录表；调用方不得解引用。/ Module-owned table; callers must not dereference it.
    int count, capacity, selected, menu_page; ///< 内部状态，仅通过下列接口访问。/ Internal state, accessed only through the API below.
    bool menu_open; ///< 分类菜单状态，不持有绘制或输入对象。/ Menu state without drawing or input objects.
    char root[BOOK_STORE_PATH_MAX]; ///< 本次开机的根目录副本。/ Owned root path for this boot.
    const book_collection_t *collection; ///< 借用已提交分类；为空时只供旧目录迁移。/ Borrow committed categories; NULL retains legacy migration mode.
} book_shelf_categories_t;

/// 初始化或重试分配；返回根目录是否变化，变化时丢弃原分类和页码。/ Initialize or retry allocation; report root changes, which discard old folders and pages.
bool book_shelf_categories_init(book_shelf_categories_t *state, const char *root);
/// 按稳定ID刷新显示顺序，保留原页码和选择；失败不替换现有表。/ Refresh order by stable IDs, preserving pages/selection; allocation failure leaves the old table intact.
bool book_shelf_categories_bind(book_shelf_categories_t *state, const book_collection_t *collection);
/// 当前分类表是否已绑定逻辑索引。/ Whether the current table is bound to a logical index.
bool book_shelf_categories_logical(const book_shelf_categories_t *state);
uint32_t book_shelf_categories_id(const book_shelf_categories_t *state, int index);
int book_shelf_categories_index(const book_shelf_categories_t *state, uint32_t id);
/// 释放目录并恢复零状态；不可在持有借用字符串时调用。/ Release the table and restore zero state; no borrowed strings may remain in use.
void book_shelf_categories_dispose(book_shelf_categories_t *state);
/// 按原顺序登记或重新启用目录，分配失败或达到上限返回 -1。/ Register or re-enable a folder in original order; return -1 on allocation failure or the limit.
int book_shelf_categories_add(book_shelf_categories_t *state, const char *path);
/// 扫描前仅标记自定义目录不可用，保留旧页码。/ Mark custom folders unavailable before scanning, preserving remembered pages.
void book_shelf_categories_begin_scan(book_shelf_categories_t *state);
/// 同步借用条目计算计数；已选目录消失时回到全部书籍。/ Borrow entries synchronously to recount; fall back to all books if the selection disappears.
void book_shelf_categories_recount(book_shelf_categories_t *state, const book_shelf_entry_t *entries, int count);
/// 构造同步排序查询；路径借用至目录表或根目录下次变化。/ Build a synchronous sort query; paths are borrowed until the table or root next changes.
book_shelf_query_t book_shelf_categories_query(const book_shelf_categories_t *state, int filter, bool recent_sort);
/// 记住当前分类本次开机的排序；未设置时沿用全局默认。/ Remember this boot's category sort; untouched categories use the global default.
void book_shelf_categories_set_sort(book_shelf_categories_t *state, bool recent_sort);
/// 原显示名；返回值仅供同步使用，不转移所有权。/ Original display name, borrowed for synchronous use.
const char *book_shelf_categories_name(const book_shelf_categories_t *state, int index);
/// 一级目录的借用路径；系统分类返回根目录。/ Borrow a direct folder path; system categories return the root.
const char *book_shelf_categories_path(const book_shelf_categories_t *state, int index);
/// 本机改名保留页码；删除释放对应槽位。/ Local renames preserve pages; removals release their slot.
void book_shelf_categories_rebase(book_shelf_categories_t *state, const char *old_path, const char *new_path);
/// 返回分类书籍数，越界返回零。/ Return a category's book count, or zero outside the table.
int book_shelf_categories_count(const book_shelf_categories_t *state, int index);
/// 返回已选分类；分配失败时为全部。/ Return the selection, defaulting to all on allocation failure.
int book_shelf_categories_selected(const book_shelf_categories_t *state);
/// 按原可见顺序将序号转换为目录索引，无对应项返回 -1。/ Map a visible ordinal to its folder index in original order, or -1.
int book_shelf_categories_at(const book_shelf_categories_t *state, int ordinal);
/// 返回可用分类总数。/ Return the number of available categories.
int book_shelf_categories_available(const book_shelf_categories_t *state);
/// 保存当前分类和布局的页码；list_layout 区分两种既有布局。/ Remember the selected category page for either existing layout.
void book_shelf_categories_remember(book_shelf_categories_t *state, bool list_layout, int page);
/// 读取原页码，不按当前书籍数裁剪，用于页面进入时的准备。/ Read the saved page without count clamping, for page-entry preparation.
int book_shelf_categories_saved_page(const book_shelf_categories_t *state, bool list_layout);
/// 按现有条目数裁剪并写回当前分类页码。/ Clamp and store the selected category's page for the current entry count.
int book_shelf_categories_restore(book_shelf_categories_t *state, bool list_layout, int count, int rows);
/// 返回菜单是否打开。/ Return whether the menu is open.
bool book_shelf_categories_menu_is_open(const book_shelf_categories_t *state);
/// 有目录表时打开菜单并定位已选项，无目录表返回 false。/ Open and locate the selection when a table exists; otherwise return false.
bool book_shelf_categories_menu_open(book_shelf_categories_t *state);
/// 关闭菜单，保留已选项与页码。/ Close the menu while preserving selection and pages.
void book_shelf_categories_menu_close(book_shelf_categories_t *state);
/// 返回有界菜单页码，最多 128 个分类。/ Return the bounded menu page for at most 128 categories.
uint8_t book_shelf_categories_menu_page(const book_shelf_categories_t *state);
/// 移动菜单页，边界时不修改状态并返回 false。/ Move within menu pages, returning false without changes at a boundary.
bool book_shelf_categories_menu_move(book_shelf_categories_t *state, int direction);
/// 选择可用分类并关闭菜单；调用方先保存旧页码，再排序和恢复新页码。/ Select an available category and close the menu; callers first remember the old page, then sort and restore the new page.
bool book_shelf_categories_select(book_shelf_categories_t *state, int index);
