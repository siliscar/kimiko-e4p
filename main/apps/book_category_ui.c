/* 中文：分类管理、稳定ID目标选择和跨筛选选书；绘制保持纯函数。
 * English: Category management, stable-ID target picking and selections across filters; drawing stays pure. */
#include "book_category_ui.h"
#include "book_selection_ui.h"
#include "book_shelf_categories.h"
#include "book_search.h"
#include "ui_kit.h"
#include "ui_nav.h"
#include <stdio.h>
#include <string.h>

int book_category_ui_rows(bool books) {
    int px = ui_text_effective_px(28);
    return books ? (px > 44 ? 3 : px > 32 ? 4 : 5) : (px > 44 ? 4 : px > 32 ? 5 : 7);
}
static EpdRect row_rect(int row) {
    int height = 728 / book_category_ui_rows(false);
    return (EpdRect){36, 224 + height * row, 612, height};
}
static EpdRect book_rect(const book_category_ui_t *ui, int row) {
    int top=ui->notice[0]?452:380;
    int height=(1004-top)/book_category_ui_rows(true);
    return (EpdRect){36,top+height*row,612,height};
}
static EpdRect select_rect(bool results) { return results?(EpdRect){338,284,310,76}:(EpdRect){36,284,278,76}; }
static EpdRect field(void) { return (EpdRect){36, 294, 612, 88}; }
static void copy(char *out, size_t cap, const char *s) {
    if (!cap) return;
    size_t n = strlen(s);
    if (n >= cap) { n = cap - 1; while (n && ((unsigned char)s[n] & 0xc0) == 0x80) --n; }
    memcpy(out, s, n); out[n] = 0;
}
static void text(uint8_t *fb, EpdRect rect, int px, const char *s, bool center) {
    char line[292]; copy(line, sizeof(line) - 4, s);
    px = ui_text_effective_px(px); if (px > rect.height - 10) px = rect.height - 10;
    if (ui_text_fixed_width_px(px, line) > rect.width) {
        size_t n = strlen(line);
        do {
            if (n) { --n; while (n && ((unsigned char)line[n] & 0xc0) == 0x80) --n; }
            memcpy(line + n, "...", 4);
        } while (n && ui_text_fixed_width_px(px, line) > rect.width);
    }
    ui_text_fixed_vc(fb, center ? rect.x + rect.width / 2 : rect.x, rect.y + rect.height / 2,
                     px, line, center ? EPD_DRAW_ALIGN_CENTER : EPD_DRAW_ALIGN_LEFT, false);
}
static void command_text(uint8_t *fb, EpdRect r, int px, const char *s, bool enabled) {
    book_selection_text(fb,r,px,s,enabled);
}
static void button(uint8_t *fb, EpdRect r, const char *label) {
    ui_draw_control_frame(fb, r, 8, UI_GRAY_BLACK);
    command_text(fb, (EpdRect){r.x + 12, r.y, r.width - 24, r.height}, 28, label, true);
}
static size_t line_end(const char *s, int px, int width) {
    char line[288]; size_t end = 0;
    while (s[end]) {
        size_t next = end + 1;
        while (s[next] && ((unsigned char)s[next] & 0xc0) == 0x80) ++next;
        memcpy(line, s, next); line[next] = 0;
        if (end && ui_text_fixed_width_px(px, line) > width) break;
        end = next;
    }
    return end;
}
static void full_text(uint8_t *fb, EpdRect r, int base_px, const char *s) {
    int px = ui_text_effective_px(base_px), lines;
    do {
        const char *p = s; lines = 0;
        while (*p) { p += line_end(p, px, r.width); ++lines; }
        if (lines * (px + 12) <= r.height || px <= 14) break;
        --px;
    } while (true);
    int y = r.y + (r.height - lines * (px + 12)) / 2 + (px + 12) / 2;
    while (*s) {
        size_t n = line_end(s, px, r.width); char line[288]; memcpy(line, s, n); line[n] = 0;
        ui_text_fixed_vc(fb, r.x, y, px, line, EPD_DRAW_ALIGN_LEFT, false);
        s += n; y += px + 12;
    }
}
static int at(const book_category_ui_t *ui, const book_shelf_categories_t *categories, int ordinal) {
    for (int n = 0, index; (index = book_shelf_categories_at(categories, n)) >= 0; ++n) {
        if (ui->screen != CATEGORY_FILTER && ((ui->picker && !index) || (!ui->picker && index < 2))) continue;
        if (ordinal-- == 0) return index;
    }
    return -1;
}
static int pages(const book_category_ui_t *ui, const book_shelf_categories_t *categories) {
    int n = 0; while (at(ui, categories, n) >= 0) ++n;
    int rows = book_category_ui_rows(false);
    return n ? (n + rows - 1) / rows : 1;
}
static const char *name_of(const book_shelf_categories_t *c, uint32_t id) {
    return book_shelf_categories_name(c, book_shelf_categories_index(c, id));
}
static bool book_matches(const book_category_ui_t *ui, int i) {
    const book_shelf_entry_t *b = &ui->books[i];
    if (b->removed) return false;
    if (ui->review) return b->selected;
    return (ui->selecting_only || b->category_id != ui->destination) &&
           (ui->filter == BOOK_COLLECTION_ALL || ui->filter == b->category_id) && book_search_match(b->name, ui->query);
}
int book_category_ui_book_at(const book_category_ui_t *ui, int ordinal) {
    for (int i = 0; i < ui->count; ++i) if (book_matches(ui, i) && ordinal-- == 0) return i;
    return -1;
}
int book_category_ui_book_count(const book_category_ui_t *ui) {
    int n = 0; for (int i = 0; i < ui->count; ++i) if (book_matches(ui, i)) ++n;
    return n;
}
void book_category_ui_refresh(book_category_ui_t *ui, const book_shelf_categories_t *categories, uint32_t focus) {
    if (focus) for (int n = 0, index; (index = at(ui, categories, n)) >= 0; ++n)
        if (book_shelf_categories_id(categories, index) == focus) { ui->page = n / book_category_ui_rows(false); break; }
    int last = pages(ui, categories) - 1;
    if (ui->page > last) ui->page = last;
    if (ui->page < 0) ui->page = 0;
}
void book_category_ui_begin(book_category_ui_t *ui, bool picker) {
    memset(ui, 0, sizeof(*ui)); ui->picker = picker;
}
void book_category_ui_end(book_category_ui_t *ui) {
    if (ui->screen == CATEGORY_EDIT) ui_keyboard_end();
    memset(ui, 0, sizeof(*ui));
}
void book_category_ui_suspend(book_category_ui_t *ui) {
    ui_keyboard_hold_cancel(); ui_keyboard_release(); ui->command = CATEGORY_IDLE;
}
bool book_category_ui_back(book_category_ui_t *ui) {
    if (ui->command != CATEGORY_IDLE) return false;
    if (ui->screen == CATEGORY_EDIT) { ui_keyboard_end(); ui->screen = ui->edit_parent; }
    else if (ui->screen == CATEGORY_BOOK_INFO) ui->screen = CATEGORY_BOOKS;
    else if (ui->screen == CATEGORY_FILTER) { ui->screen = CATEGORY_BOOKS; ui->page = ui->book_page; }
    else if (ui->screen == CATEGORY_BOOKS) {
        if (ui->review && !ui->selecting_only) { ui->review = false; ui->page = 0; }
        else return true;
    } else if (ui->screen == CATEGORY_CONFIRM || ui->screen == CATEGORY_ORDER) ui->screen = CATEGORY_ACTIONS;
    else if (ui->screen == CATEGORY_ACTIONS) ui->screen = CATEGORY_LIST;
    else return true;
    ui->notice[0] = 0; return false;
}
void book_category_ui_edit(book_category_ui_t *ui, const book_shelf_categories_t *c, bool rename) {
    ui->edit_parent = ui->screen; ui->renaming = rename;
    copy(ui->draft, sizeof(ui->draft), rename ? name_of(c, ui->source) : "");
    ui_text_edit_init(&ui->input, ui->draft, sizeof(ui->draft));
    ui_keyboard_begin(&ui->input, false); ui->screen = CATEGORY_EDIT; ui->notice[0] = 0;
}
void book_category_ui_books(book_category_ui_t *ui, uint32_t target, bool selecting_only) {
    ui->screen = CATEGORY_BOOKS; ui->destination = target; ui->selecting_only = selecting_only;
    ui->picker = false; ui->review = selecting_only;
    ui->filter = selecting_only ? BOOK_COLLECTION_ALL : 0;
    ui->page = 0; ui->query[0] = 0; ui->notice[0] = 0;
}
static void submit(book_category_ui_t *ui) {
    char clean[BOOK_COLLECTION_NAME_BYTES];
    if (ui_keyboard_pending()) { copy(ui->notice, sizeof(ui->notice), "请先选择候选字"); return; }
    if (!book_collection_name(ui->draft, clean)) { copy(ui->notice, sizeof(ui->notice), "名称须为1至24字，且不能重复系统分类"); return; }
    ui->command = ui->renaming ? CATEGORY_RENAME : CATEGORY_CREATE;
}
static void pager(uint8_t *fb, int page, int count, int y) {
    char value[32]; snprintf(value, sizeof(value), "%d / %d", page + 1, count);
    text(fb, (EpdRect){210, y, 264, 58}, 24, value, true);
    ui_draw_icon(fb, 86, y + 29, 36, UI_ICON_CHEVRON_LEFT, page ? 0 : 0xb0);
    ui_draw_icon(fb, 598, y + 29, 36, UI_ICON_CHEVRON_RIGHT, page + 1 < count ? 0 : 0xb0);
}
static void draw_books(const book_category_ui_t *ui, const book_shelf_categories_t *c, uint8_t *fb) {
    char title[160];
    if (ui->review) copy(title, sizeof(title), "已选图书");
    else snprintf(title, sizeof(title), "添加到「%s」", name_of(c, ui->destination));
    text(fb, (EpdRect){112, 80, 432, 80}, 34, title, false);
    ui_draw_control_frame(fb,(EpdRect){568,80,80,80},8,0x50);
    ui_draw_icon(fb,608,120,34,UI_ICON_SEARCH,0);
    int rows = book_category_ui_rows(true), n = book_category_ui_book_count(ui);
    EpdRect filter=book_selection_rect(BOOK_SELECTION_FILTER);
    if(!ui->review)ui_draw_control_frame(fb,filter,8,0x50);
    text(fb,(EpdRect){52,184,280,76},26,ui->review?"全部已选":name_of(c,ui->filter),false);
    if(!ui->review)ui_draw_icon(fb,360,222,26,UI_ICON_CHEVRON_DOWN,0);
    if(ui->query[0] && !ui->review)text(fb,(EpdRect){416,184,232,76},24,ui->query,false);
    book_selection_button(fb,select_rect(false),22,"全选本页",n>0,false);
    char results[64]; snprintf(results, sizeof(results), "全选结果 (%d)", n);
    book_selection_button(fb,select_rect(true),22,results,n>0,false);
    if(ui->notice[0])text(fb,(EpdRect){36,380,612,56},22,ui->notice,false);
    int page = book_shelf_saved_page(ui->page, n, rows);
    for (int row = 0; row < rows; ++row) {
        int index = book_category_ui_book_at(ui, page * rows + row); if (index < 0) break;
        const book_shelf_entry_t *b = &ui->books[index]; EpdRect r = book_rect(ui,row);
        EpdRect check = {36, r.y + r.height / 2 - 17, 34, 34};
        ui_draw_control_frame(fb, check, 5, 0x50);
        if (b->selected) ui_draw_icon(fb, 53, r.y + r.height / 2, 30, UI_ICON_CHECK, 0);
        EpdRect cover = {92, r.y + (r.height - 88) / 2, 64, 88};
        if (ui->cover) ui->cover(fb, row, index, cover);
        else ui_draw_control_frame(fb, cover, 0, 0x70);
        text(fb, (EpdRect){182, r.y + 4, 400, r.height * 3 / 5 - 4}, 27, b->name, false);
        ui_draw_icon(fb, 620, r.y + r.height / 2, 28, UI_ICON_ELLIPSIS, 0);
        text(fb, (EpdRect){182, r.y + r.height * 3 / 5, 466, r.height * 2 / 5 - 4}, 21, name_of(c, b->category_id), false);
        ui_hairline(fb, r.y + r.height - 1, 36, 612, 0xb0);
    }
    if (!n) {
        text(fb, (EpdRect){36, 492, 612, 80}, 28, ui->review ? "尚未选择图书" : "当前筛选没有可添加图书", true);
        if (!ui->review) button(fb, (EpdRect){180, 610, 324, 76}, "全部书籍");
    }
    unsigned selected = book_shelf_selected_count(ui->books, ui->count);
    char count[64];snprintf(count,sizeof(count),"%d / %d",page+1,n?(n+rows-1)/rows:1);
    book_selection_pager_draw(fb,count,page,n?(n+rows-1)/rows:1);
    snprintf(count,sizeof(count),"%s %u 本",ui->review?"返回选书":"已选",selected);
    book_selection_button(fb,book_selection_rect(BOOK_SELECTION_REVIEW),26,count,true,false);
    snprintf(count,sizeof(count),"%s %u 本",ui->selecting_only?"归类":"添加",selected);
    book_selection_button(fb,book_selection_rect(BOOK_SELECTION_SUBMIT),28,count,selected!=0,true);
}
void book_category_ui_draw(const book_category_ui_t *ui, const book_shelf_categories_t *c, uint8_t *fb) {
    ui_clear_page(fb); ui_nav_status(fb); ui_nav_back(fb, 36, 79);
    if (ui->screen == CATEGORY_BOOKS) { draw_books(ui, c, fb); return; }
    if (ui->screen == CATEGORY_BOOK_INFO) {
        text(fb, (EpdRect){116, 79, 446, 60}, 34, "图书信息", true);
        const book_shelf_entry_t *b = &ui->books[ui->book_info];
        full_text(fb, (EpdRect){36, 210, 612, 590}, 32, b->name);
        full_text(fb, (EpdRect){36, 822, 612, 150}, 26, name_of(c, b->category_id));
        button(fb, (EpdRect){180, 1004, 324, 80}, b->selected ? "取消选择" : "选择图书");
        return;
    }
    const char *title = ui->screen == CATEGORY_EDIT ? (ui->renaming ? "修改分类" : "新增分类") :
        ui->screen == CATEGORY_ACTIONS ? "分类操作" : ui->screen == CATEGORY_CONFIRM ? "删除分类" :
        ui->screen == CATEGORY_ORDER ? "调整顺序" : ui->screen == CATEGORY_FILTER ? "筛选分类" :
        ui->screen == CATEGORY_SELECTED_ACTIONS ? "已选操作" : ui->picker ? "归入分类" : "管理分类";
    text(fb, (EpdRect){116, 79, 446, 60}, 34, title, true);
    ui_hairline(fb, 200, 36, 612, 0x70);
    text(fb, (EpdRect){36, 150, 612, 44}, 22, ui->notice, false);
    if (ui->blocked) { button(fb, (EpdRect){180, 448, 324, 80}, "重试读取"); return; }
    if(ui->screen==CATEGORY_SELECTED_ACTIONS){
        text(fb,(EpdRect){36,222,612,76},34,name_of(c,ui->source),true);
        button(fb,(EpdRect){36,332,612,88},"移出本类");
        button(fb,(EpdRect){36,444,612,88},"归类");
        return;
    }
    if (ui->screen == CATEGORY_EDIT) {
        ui_draw_icon(fb, 620, 108, 36, UI_ICON_CHECK, 0);
        text(fb, (EpdRect){36, 220, 612, 56}, 28, "分类名称", false);
        ui_text_input_draw(fb, &ui->input, field(), 30, false, NULL); ui_keyboard_draw(fb, 560); return;
    }
    if (ui->screen == CATEGORY_ACTIONS || ui->screen == CATEGORY_CONFIRM || ui->screen == CATEGORY_ORDER) {
        text(fb, (EpdRect){36, 222, 612, 76}, 34, name_of(c, ui->source), true);
        if (ui->screen == CATEGORY_ACTIONS) {
            const char *labels[] = {"添加图书", "修改名称", "调整顺序", "删除分类"};
            for (int i = 0; i < 4; ++i) button(fb, (EpdRect){36, 332 + i * 112, 612, 88}, labels[i]);
        } else if (ui->screen == CATEGORY_ORDER) {
            ui_draw_icon(fb, 342, 412, 46, UI_ICON_CHEVRON_UP, 0);
            ui_draw_icon(fb, 342, 572, 46, UI_ICON_CHEVRON_DOWN, 0);
            char position[64]; snprintf(position, sizeof(position), "%d / %d", book_shelf_categories_index(c, ui->source) - 1, book_shelf_categories_available(c) - 2);
            text(fb, (EpdRect){36, 672, 612, 72}, 28, position, true);
        } else {
            char count[80]; snprintf(count, sizeof(count), "其中 %d 本书将变为未分类", book_shelf_categories_count(c, book_shelf_categories_index(c, ui->source)));
            command_text(fb, (EpdRect){36, 352, 612, 80}, 28, count, true);
            command_text(fb, (EpdRect){36, 458, 612, 72}, 26, "原文件与阅读记录不变", true);
            button(fb, (EpdRect){36, 650, 278, 80}, "取消"); button(fb, (EpdRect){338, 650, 310, 80}, "删除分类");
        }
        return;
    }
    if (ui->screen != CATEGORY_FILTER) ui_draw_icon(fb, 620, 108, 36, UI_ICON_PLUS, 0);
    int rows = book_category_ui_rows(false);
    for (int row = 0; row < rows; ++row) {
        int index = at(ui, c, ui->page * rows + row); if (index < 0) break;
        EpdRect r = row_rect(row);
        ui_draw_icon(fb, 60, r.y + r.height / 2, 30, index < 2 ? UI_ICON_LIBRARY_BIG : UI_ICON_FOLDER, 0);
        text(fb, (EpdRect){96, r.y, 402, r.height}, 28, book_shelf_categories_name(c, index), false);
        char count[20]; snprintf(count, sizeof(count), "%d", book_shelf_categories_count(c, index));
        text(fb, (EpdRect){512, r.y, 72, r.height}, 22, count, true);
        if (!ui->picker && ui->screen != CATEGORY_FILTER) ui_draw_icon(fb, 620, r.y + r.height / 2, 28, UI_ICON_ELLIPSIS, 0);
        ui_hairline(fb, r.y + r.height - 1, 36, 612, 0xb0);
    }
    if (!ui->picker && ui->screen == CATEGORY_LIST && book_shelf_categories_available(c) == 2)
        text(fb, (EpdRect){36, 420, 612, 100}, 28, "暂无分类", true);
    pager(fb, ui->page, pages(ui, c), 982);
    if (ui->can_undo && ui->screen == CATEGORY_LIST && !ui->picker) button(fb, (EpdRect){180, 1056, 324, 64}, "撤销上次操作");
}
ui_keyboard_update_t book_category_ui_input_paint(book_category_ui_t *ui, uint8_t *fb) {
    (void)ui; return ui_keyboard_update(fb, 560, field(), 30, false, NULL, true);
}
int book_category_ui_gesture(book_category_ui_t *ui, const book_shelf_categories_t *c, const ui_gesture_event_t *ev, int64_t now) {
    if (ui->command != CATEGORY_IDLE) return 0;
    if (ui->screen == CATEGORY_EDIT) {
        if (ev->type == UI_GESTURE_PRESS) return ui_keyboard_press(ev->x0, ev->y0, 560, now) ? 1 : 0;
        bool released = ui_keyboard_release();
        if (ev->type == UI_GESTURE_TAP) {
            if (ev->y0 < 145 && ev->x0 < 120) return book_category_ui_back(ui) ? 2 : 1;
            if (ev->y0 < 145 && ev->x0 > 568) { submit(ui); return 1; }
            if (ui_text_input_tap(&ui->input, field(), 30, false, NULL, ev->x0, ev->y0)) return 1;
            ui_keyboard_result_t result = ui_keyboard_tap(ev->x0, ev->y0, 560, now);
            if (result == UI_KEYBOARD_DONE) { submit(ui); return 1; }
            return result == UI_KEYBOARD_CHANGED || released ? 1 : 0;
        }
        if (ev->type == UI_GESTURE_SWIPE_L || ev->type == UI_GESTURE_SWIPE_R)
            return ui_keyboard_page(ev->type == UI_GESTURE_SWIPE_L ? 1 : -1) || released ? 1 : 0;
        return released ? 1 : 0;
    }
    if (ev->type == UI_GESTURE_TAP && ev->y0 < 145 && ev->x0 < 120) return book_category_ui_back(ui) ? 2 : 1;
    if (ui->blocked) {
        if (ev->type == UI_GESTURE_TAP && ui_rect_hit((EpdRect){180,448,324,80},ev->x0,ev->y0)) { ui->command = CATEGORY_RECOVER; return 1; }
        return 0;
    }
    if (ui->screen == CATEGORY_BOOK_INFO) {
        if (ev->type == UI_GESTURE_TAP && ui_rect_hit((EpdRect){180,1004,324,80},ev->x0,ev->y0)) {
            ui->books[ui->book_info].selected = !ui->books[ui->book_info].selected;
            ui->screen = CATEGORY_BOOKS; return 1;
        }
        return 0;
    }
    if(ui->screen==CATEGORY_SELECTED_ACTIONS){
        if(ev->type==UI_GESTURE_TAP && book_shelf_selected_count(ui->books,ui->count)){
            if(ui_rect_hit((EpdRect){36,332,612,88},ev->x0,ev->y0)){ui->command=CATEGORY_UNCLASSIFY;return 1;}
            if(ui_rect_hit((EpdRect){36,444,612,88},ev->x0,ev->y0)){ui->screen=CATEGORY_LIST;ui->picker=true;ui->page=0;return 1;}
        }
        return 0;
    }
    if (ui->screen == CATEGORY_ACTIONS && ev->type == UI_GESTURE_TAP) {
        for (int i = 0; i < 4; ++i) if (ui_rect_hit((EpdRect){36,332+i*112,612,88},ev->x0,ev->y0)) {
            if (!i) { ui->destination = ui->source; ui->command = CATEGORY_ADD; }
            if (i == 1) book_category_ui_edit(ui,c,true);
            if (i == 2) ui->screen = CATEGORY_ORDER;
            if (i == 3) ui->screen = CATEGORY_CONFIRM;
            return 1;
        }
    }
    if (ui->screen == CATEGORY_ORDER && ev->type == UI_GESTURE_TAP) {
        if (ui_rect_hit((EpdRect){180,350,324,120},ev->x0,ev->y0)) ui->command = CATEGORY_UP;
        if (ui_rect_hit((EpdRect){180,510,324,120},ev->x0,ev->y0)) ui->command = CATEGORY_DOWN;
        return ui->command != CATEGORY_IDLE;
    }
    if (ui->screen == CATEGORY_CONFIRM && ev->type == UI_GESTURE_TAP) {
        if (ui_rect_hit((EpdRect){36,650,278,80},ev->x0,ev->y0)) { ui->screen = CATEGORY_ACTIONS; return 1; }
        if (ui_rect_hit((EpdRect){338,650,310,80},ev->x0,ev->y0)) { ui->command = CATEGORY_REMOVE; return 1; }
    }
    if (ui->screen == CATEGORY_BOOKS) {
        int n = book_category_ui_book_count(ui), rows = book_category_ui_rows(true);
        ui->page = book_shelf_saved_page(ui->page,n,rows);
        if (ev->type == UI_GESTURE_LONG_PRESS) {
            for (int row = 0; row < rows; ++row) if (ui_rect_hit(book_rect(ui,row), ev->x0, ev->y0)) {
                int i = book_category_ui_book_at(ui, ui->page * rows + row);
                if (i < 0) return 0;
                ui->book_info = i; ui->screen = CATEGORY_BOOK_INFO; return 1;
            }
        }
        if (ev->type == UI_GESTURE_TAP) {
            if (ui_rect_hit((EpdRect){568,80,80,80},ev->x0,ev->y0)) { ui->command = CATEGORY_SEARCH; return 1; }
            if (ui_rect_hit(book_selection_rect(BOOK_SELECTION_FILTER),ev->x0,ev->y0) && !ui->review) { ui->book_page=ui->page; ui->page=0; ui->screen=CATEGORY_FILTER; return 1; }
            bool page_all=ui_rect_hit(select_rect(false),ev->x0,ev->y0);
            if (page_all || ui_rect_hit(select_rect(true),ev->x0,ev->y0)) {
                if(!n)return 0;
                int first=page_all?ui->page*rows:0, end=page_all?first+rows:n;
                bool all = true;
                for(int ordinal=first;ordinal<end;++ordinal){int i=book_category_ui_book_at(ui,ordinal);if(i>=0&&!ui->books[i].selected)all=false;}
                for(int ordinal=end-1;ordinal>=first;--ordinal){int i=book_category_ui_book_at(ui,ordinal);if(i>=0)ui->books[i].selected=!all;}
                return 1;
            }
            if (!n && !ui->review && ui_rect_hit((EpdRect){180,610,324,76},ev->x0,ev->y0)) { ui->filter=BOOK_COLLECTION_ALL; ui->query[0]=0; ui->page=0; return 1; }
            if (ui_rect_hit(book_selection_rect(BOOK_SELECTION_REVIEW),ev->x0,ev->y0)) {
                ui->review=!ui->review;ui->page=0;return 1;
            }
            if (ui_rect_hit(book_selection_rect(BOOK_SELECTION_SUBMIT),ev->x0,ev->y0)) {
                if (book_shelf_selected_count(ui->books,ui->count)) {
                    if (ui->selecting_only) { ui->screen=CATEGORY_LIST; ui->picker=true; ui->page=0; }
                    else ui->command=CATEGORY_MOVE;
                    return 1;
                }
            }
            for (int row=0;row<rows;++row) if (ui_rect_hit(book_rect(ui,row),ev->x0,ev->y0)) {
                int i=book_category_ui_book_at(ui,ui->page*rows+row);if(i<0)return 0;
                if (ev->x0 >= 584) { ui->book_info=i; ui->screen=CATEGORY_BOOK_INFO; }
                else ui->books[i].selected=!ui->books[i].selected;
                return 1;
            }
        }
        int direction=ev->type==UI_GESTURE_SWIPE_U?1:ev->type==UI_GESTURE_SWIPE_D?-1:0;
        if(ev->type==UI_GESTURE_TAP){
            int control=book_selection_pager_hit(ev->x0,ev->y0);
            if(control>=0){int target=control==0?0:control==3?(n?n-1:0)/rows:ui->page+(control==1?-1:1);direction=target-ui->page;}
        }
        int next=ui->page+direction;
        if(direction && next>=0 && next<(n+rows-1)/rows){ui->page=next;return 1;}
        return 0;
    }
    if (ui->screen != CATEGORY_LIST && ui->screen != CATEGORY_FILTER) return 0;
    int direction=ev->type==UI_GESTURE_SWIPE_U?1:ev->type==UI_GESTURE_SWIPE_D?-1:0;
    if (ev->type == UI_GESTURE_TAP) {
        if (ev->y0 < 145 && ev->x0 > 568 && ui->screen != CATEGORY_FILTER) { book_category_ui_edit(ui,c,false); return 1; }
        if (ui->can_undo && ui->screen==CATEGORY_LIST && !ui->picker && ui_rect_hit((EpdRect){180,1056,324,64},ev->x0,ev->y0)) { ui->command=CATEGORY_UNDO;return 1; }
        if (ev->y0 >= 982 && ev->y0 < 1040) direction=ev->x0<180?-1:ev->x0>504?1:0;
        for(int row=0;row<book_category_ui_rows(false);++row)if(ui_rect_hit(row_rect(row),ev->x0,ev->y0)) {
            int index=at(ui,c,ui->page*book_category_ui_rows(false)+row);if(index<0)return 0;
            uint32_t id=book_shelf_categories_id(c,index);
            if(ui->screen==CATEGORY_FILTER){ui->filter=id;ui->screen=CATEGORY_BOOKS;ui->page=0;}
            else if(ui->picker){ui->destination=id;ui->command=CATEGORY_MOVE;}
            else {ui->source=id;if(ev->x0>=584)ui->screen=CATEGORY_ACTIONS;else {ui->destination=id;ui->command=CATEGORY_OPEN;}}
            return 1;
        }
    }
    int next=ui->page+direction;
    if(direction && next>=0 && next<pages(ui,c)){ui->page=next;return 1;}
    return 0;
}
