/* 中文：逻辑分类与选书界面；借用书目，只发事务命令，不执行文件操作。
 * English: Logical-category and book-picking UI; borrow the catalog and emit transactions without file operations. */
#pragma once
#include "book_shelf_category_state.h"
#include "ui_keyboard.h"
#include "ui_gesture.h"

typedef enum { CATEGORY_LIST, CATEGORY_ACTIONS, CATEGORY_EDIT, CATEGORY_CONFIRM,
               CATEGORY_ORDER, CATEGORY_BOOKS, CATEGORY_FILTER, CATEGORY_BOOK_INFO,
               CATEGORY_SELECTED_ACTIONS } book_category_screen_t;
typedef enum { CATEGORY_IDLE, CATEGORY_CREATE, CATEGORY_RENAME, CATEGORY_MOVE, CATEGORY_REMOVE,
               CATEGORY_RECOVER, CATEGORY_OPEN, CATEGORY_ADD, CATEGORY_UP, CATEGORY_DOWN,
               CATEGORY_SEARCH, CATEGORY_UNDO, CATEGORY_UNCLASSIFY } book_category_command_t;
typedef void (*book_category_cover_fn)(uint8_t *fb, int row, int index, EpdRect rect);
typedef struct {
    book_category_screen_t screen, edit_parent;
    book_category_command_t command;
    bool picker, renaming, blocked, review, selecting_only, can_undo;
    int page, book_page, count, book_info;
    uint32_t source, destination, filter;
    char draft[BOOK_COLLECTION_NAME_BYTES], notice[128], query[65];
    ui_text_edit_t input;
    book_shelf_entry_t *books; ///< 同步借用，绘制不修改书目。/ Borrow synchronously; drawing never mutates the catalog.
    book_category_cover_fn cover;
} book_category_ui_t;

void book_category_ui_begin(book_category_ui_t *ui, bool picker);
void book_category_ui_end(book_category_ui_t *ui);
/// 锁屏取消排队命令和按住状态。/ Lock cancels queued commands and keyboard holds.
void book_category_ui_suspend(book_category_ui_t *ui);
void book_category_ui_refresh(book_category_ui_t *ui, const book_shelf_categories_t *categories, uint32_t focus);
void book_category_ui_edit(book_category_ui_t *ui, const book_shelf_categories_t *categories, bool rename);
void book_category_ui_books(book_category_ui_t *ui, uint32_t target, bool selecting_only);
int book_category_ui_rows(bool books);
int book_category_ui_book_at(const book_category_ui_t *ui, int ordinal);
int book_category_ui_book_count(const book_category_ui_t *ui);
bool book_category_ui_back(book_category_ui_t *ui);
void book_category_ui_draw(const book_category_ui_t *ui, const book_shelf_categories_t *categories, uint8_t *fb);
/// 返回0无变化、1重绘、2退出；副作用命令由下一轮消费。/ Return0 unchanged,1 repaint,2 close; consume side-effect commands on the next tick.
int book_category_ui_gesture(book_category_ui_t *ui, const book_shelf_categories_t *categories,
                             const ui_gesture_event_t *event, int64_t now_ms);
ui_keyboard_update_t book_category_ui_input_paint(book_category_ui_t *ui, uint8_t *fb);
