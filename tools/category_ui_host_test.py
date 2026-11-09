#!/usr/bin/env python3
"""中文：分类真实控制器、键盘、内置字体及4bpp截图。/ English: Real category controller, keyboard, builtin font and 4bpp screenshots."""
import argparse
import platform
import re
import subprocess
import tempfile
from pathlib import Path

from PIL import Image
import reader_title_style_preview as host
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
KIT = ROOT / "main/ui/ui_kit.c"
BOOK = ROOT / "main/apps/app_book.c"
NAV = ROOT / "main/ui/ui_nav.c"


def adapter():
    code = r'''
typedef enum { SHELF, MANAGE, BULK, CATEGORIES, EDIT, SEARCH, IMPORT } book_view_t;
typedef book_shelf_entry_t shelf_entry_t;
static book_category_ui_t s_category_ui;
static book_shelf_categories_t s_shelf_categories;
static book_collection_t s_collection;
static uint32_t s_collection_bound_generation;
static bool binding_fail;
static book_view_t s_view,s_category_parent;
static bool s_category_bulk,s_category_quick,s_scan_pending;
static int s_pressed_control,s_count,s_visible_count;
static shelf_entry_t s_shelf[20],s_managed;
static char s_batch_message[128],s_category_result[128],s_cache_library[40]="card-a";
static bool pending,usb_active,sd_present=true,sd_mounted=true;
static char sd_identity[40]="card-a";
typedef struct {bool present,mounted;char identity[40];} board_sd_info_t;
static void board_sd_get_info(board_sd_info_t *sd){sd->present=sd_present;sd->mounted=sd_mounted;copy(sd->identity,40,sd_identity);}
static bool usb_storage_active(void){return usb_active;}
static bool book_category_pending(const char *root){(void)root;return pending;}
static const char *app_settings_books_dir(void){return "/sdcard/books";}
static int shelf_style;
static int app_settings_shelf_style(void){return shelf_style;}
static int shelf_rows(void){return shelf_style==5?4:9;}
#include "book_shelf_covers.h"
#include "book_cover_auto.h"
#include "ui_image_dither.h"
static book_shelf_covers_t s_shelf_covers,s_category_covers;
static uint32_t s_category_cover_key;
#define BOOK_COVER_W 176
#define BOOK_COVER_H 240
static book_cover_job_t cover_request;
unsigned book_cover_jobs_begin(void){static unsigned generation;return ++generation;}
void book_cover_jobs_cancel(unsigned generation){(void)generation;}
bool book_cover_jobs_submit(unsigned generation,unsigned row,const book_cover_job_t *request){(void)generation;(void)row;cover_request=*request;return true;}
int book_cover_jobs_take(unsigned generation,unsigned row,book_cover_result_t *result){
    (void)generation;(void)row;unsigned w=BOOK_COVER_W,h=BOOK_COVER_H;
    result->pixels=malloc(w*h);assert(result->pixels);
    assert(book_auto_cover_render(cover_request.path,cover_request.title,cover_request.author,w,h,result->pixels));
    result->width=w;result->height=h;return 1;
}
static bool category_prepare_covers(app_ctx_t *ctx){(void)ctx;return false;}
static void copy_text(char *out,size_t cap,const char *s){copy(out,cap,s);}
static void clear_selection(void){book_shelf_clear_selection(s_shelf,s_count);}
static unsigned selected_count(void){return book_shelf_selected_count(s_shelf,s_count);}
static bool s_recent_sort;
static book_shelf_query_t shelf_query(void){return book_shelf_categories_query(&s_shelf_categories,0,s_recent_sort);}
static void invalidate_covers(void){book_shelf_covers_cancel(&s_shelf_covers);book_shelf_covers_release(&s_shelf_covers);}
static bool shelf_collection_load(bool retry){(void)retry;return s_collection.ready;}
static bool shelf_collection_sync(bool complete){(void)complete;if(binding_fail)return false;
    bool ok=book_shelf_categories_bind(&s_shelf_categories,&s_collection);if(ok)s_collection_bound_generation=s_collection.generation;return ok;}
static void shelf_categories_recount(void){book_collection_apply(&s_collection,s_shelf,s_count);book_shelf_categories_recount(&s_shelf_categories,s_shelf,s_count);}
static bool transfer_idle=true,cover_idle=true;
static esp_err_t recovery_error=ESP_OK;
static unsigned recovery_calls,recovery_invalidations;
static bool wifi_transfer_try_stop_if_idle(void){return transfer_idle;}
bool book_cover_jobs_quiesce(unsigned timeout){assert(timeout==2000);return cover_idle;}
static esp_err_t category_extra_state(const char *old,const char *next,uint32_t size){(void)old;(void)next;(void)size;return ESP_OK;}
static esp_err_t book_category_recover(const char *root,esp_err_t (*extra)(const char *,const char *,uint32_t)){
    assert(!strcmp(root,"/sdcard/books")&&extra==category_extra_state);++recovery_calls;if(recovery_error==ESP_OK)pending=false;return recovery_error;
}
static void pending_drop_invalidated(void){++recovery_invalidations;}
static void book_store_notify_changed(void){++recovery_invalidations;}
static void book_lock_collage_invalidate(void){++recovery_invalidations;}
static bool legacy_category_recover(void);
static void scan_shelf(app_ctx_t *ctx){(void)ctx;assert(shelf_collection_sync(false));shelf_categories_recount();}
static void search_begin(void){s_view=SEARCH;}
static void category_begin(app_ctx_t *,bool,bool);
static void category_start_books(app_ctx_t *,uint32_t,bool);
static void category_close(app_ctx_t *);
enum { MODE_DU=1, MODE_GL16=5 };
enum { DISPLAY_REFRESH_DEFAULT, DISPLAY_REFRESH_FEEDBACK, DISPLAY_REFRESH_DETAIL, DISPLAY_REFRESH_SETTLE };
static EpdRect s_area;
static int s_mode,s_du_count;
static bool s_input_settle,s_reader_tools_toggle,s_shelf_key_paint_pending;
static bool s_shelf_key_mode;
static int s_shelf_key_pressed;
static unsigned input_refreshes;
static int input_intent;
static bool reader_indent_present(app_ctx_t *ctx,app_redraw_t redraw){(void)ctx;(void)redraw;return false;}
static int display_refresh_area(display_state_t *hl,int intent,EpdRect area,unsigned flags){
    (void)hl;assert(flags==DISPLAY_REFRESH_DEFAULT);assert(area.width&&area.height);
    ++input_refreshes;input_intent=intent;return ESP_OK;
}
static void guard_draw_result(display_state_t *hl,int error){(void)hl;assert(error==ESP_OK);}
'''
    for name in ("sort_shelf", "legacy_category_recover", "category_cover_draw", "shelf_category_remember", "shelf_category_restore", "category_reset", "category_begin",
                 "category_start_books", "category_select", "category_close", "category_back",
                 "category_input_paint", "category_gesture", "category_storage_ready", "category_error", "category_tick"):
        code += host.function(name, ROOT / "main/apps/app_book.c") + "\n"
    # 真实输入呈现出口须在通用阅读出口之前接住分类键盘。/ The real input-present branch must catch category keys before reader routing.
    prefix = host.function("present", ROOT / "main/apps/app_book.c").split(
        "    if (redraw == APP_REDRAW_NONE", 1)[0]
    code += prefix.replace("static bool present(", "static bool input_present(", 1) + "return false; }\n"
    return code + r'''
static void clean_library(book_collection_t *s){
    char prefix[288];copy(prefix,sizeof(prefix),s->prefix);book_collection_close(s);
    for(int i=0;i<2;++i){char path[300];snprintf(path,sizeof(path),"%s.%d",prefix,i);unlink(path);}
}
static uint32_t new_category(book_collection_t *s,const char *name){uint32_t id=0;unsigned n=0;assert(book_collection_create(s,name,NULL,0,false,&id,&n)==ESP_OK);return id;}
static void adapter_setup(const char *directory){
    category_reset();book_shelf_categories_dispose(&s_shelf_categories);
    if(s_collection.ready)clean_library(&s_collection);
    char prefix[288];snprintf(prefix,sizeof(prefix),"%s/adapter",directory);
    assert(book_collection_open(&s_collection,prefix)==ESP_OK);
    memset(s_shelf,0,sizeof(s_shelf));s_count=3;
    for(int i=0;i<s_count;++i){snprintf(s_shelf[i].path,288,"/sdcard/books/book%d.txt",i);snprintf(s_shelf[i].name,256,"图书 %d",i);s_shelf[i].search_match=true;}
    assert(book_collection_sync(&s_collection,"/sdcard/books",NULL,0,s_shelf,s_count,true)==ESP_OK);
    (void)new_category(&s_collection,"已有分类");
    book_shelf_categories_init(&s_shelf_categories,"/sdcard/books");assert(shelf_collection_sync(false));shelf_categories_recount();
    pending=usb_active=s_shelf_selecting=s_recent_sort=false;sd_present=sd_mounted=true;shelf_style=0;copy(sd_identity,40,"card-a");
    s_category_result[0]=0;s_view=SHELF;s_managed=s_shelf[0];
}
static void adapter_keyboard_click(app_ctx_t *ctx,int x,int y){
    ui_gesture_t gesture={0};ui_gesture_event_t e;
    app_touch_t touch={.touched=true,.count=1,.x=x,.y=y};
    ctx->touch=&touch;ctx->pressed=true;ctx->released=ctx->consumed=false;
    assert(ui_gesture_feed(&gesture,ctx,&e)&&e.type==UI_GESTURE_PRESS);
    unsigned before=input_refreshes;app_redraw_t redraw=category_gesture(ctx,&e);
    assert(redraw==APP_REDRAW_AREA&&input_present(ctx,redraw));
    assert(input_refreshes==before+1&&input_intent==DISPLAY_REFRESH_FEEDBACK);
    ctx->consumed=true;assert(category_tick(ctx)==APP_REDRAW_NONE);
    touch.touched=false;touch.count=0;ctx->pressed=ctx->consumed=false;ctx->released=true;ctx->now_ms+=40;
    assert(ui_gesture_feed(&gesture,ctx,&e)&&e.type==UI_GESTURE_TAP&&!gesture.active);
    redraw=category_gesture(ctx,&e);
    assert(redraw==APP_REDRAW_AREA&&input_present(ctx,redraw));
    assert(input_refreshes==before+2);
    ctx->touch=NULL;ctx->released=false;ctx->now_ms+=40;
}
static void adapter_keyboard_checks(uint8_t *fb){
    app_ctx_t ctx={.fb=fb,.now_ms=1000};
    book_shelf_categories_init(&s_shelf_categories,"/sdcard/books");
    for(int rename=0;rename<2;++rename){
        s_view=SHELF;category_begin(&ctx,false,false);
        if(rename){s_category_ui.source=book_collection_at(&s_collection,0)->id;s_category_ui.screen=CATEGORY_ACTIONS;}
        ui_gesture_event_t e={.type=UI_GESTURE_TAP,.x0=rename?200:620,.y0=rename?488:108};
        assert(category_gesture(&ctx,&e)==APP_REDRAW_PAGE&&s_category_ui.screen==CATEGORY_EDIT);
        if(rename){s_category_ui.draft[0]=0;ui_text_edit_place(&s_category_ui.input,0);}
        adapter_keyboard_click(&ctx,60,1026);assert(input_intent==DISPLAY_REFRESH_DETAIL);
        adapter_keyboard_click(&ctx,52,774);assert(!strcmp(s_category_ui.draft,"1"));
        adapter_keyboard_click(&ctx,628,930);assert(!s_category_ui.draft[0]);
        adapter_keyboard_click(&ctx,60,1026);assert(input_intent==DISPLAY_REFRESH_DETAIL);
        adapter_keyboard_click(&ctx,486,852);adapter_keyboard_click(&ctx,186,852);
        assert(ui_keyboard_pending());adapter_keyboard_click(&ctx,316,1026);
        assert(s_category_ui.draft[0]&&!ui_keyboard_pending());
        adapter_keyboard_click(&ctx,248,582);assert(input_intent==DISPLAY_REFRESH_DETAIL);
        adapter_keyboard_click(&ctx,480,1026);assert(input_intent==DISPLAY_REFRESH_DETAIL);
        adapter_keyboard_click(&ctx,52,774);
        assert(s_category_ui.draft[strlen(s_category_ui.draft)-1]=='q');
        s_input_settle=true;s_du_count=7;unsigned before=input_refreshes;
        assert(input_present(&ctx,APP_REDRAW_AREA));
        assert(input_intent==DISPLAY_REFRESH_SETTLE&&!s_input_settle&&!s_du_count&&input_refreshes==before+1);
        category_reset();
    }
    assert(!input_present(&ctx,APP_REDRAW_PAGE));
    for(int view=EDIT;view<=SEARCH;++view){
        s_view=view;s_mode=MODE_DU;assert(input_present(&ctx,APP_REDRAW_AREA));
        assert(input_intent==DISPLAY_REFRESH_FEEDBACK);
    }
    s_view=SHELF;assert(!input_present(&ctx,APP_REDRAW_AREA));
    book_shelf_categories_dispose(&s_shelf_categories);
    puts("category keyboard: PASS (actual create/rename gesture recognition, painting and presentation, numeric, Chinese and English input)");
}

static void adapter_checks(uint8_t *fb,const char *directory){
    app_ctx_t ctx={.fb=fb,.now_ms=1000};font_scale=100;adapter_setup(directory);
    adapter_keyboard_checks(fb);book_shelf_categories_init(&s_shelf_categories,"/sdcard/books");assert(shelf_collection_sync(false));
    // 建类后立即选书，写失败保留归属与勾选。/ Create straight into picking; failed writes retain membership and selection.
    s_view=SHELF;category_begin(&ctx,false,false);book_category_ui_edit(&s_category_ui,&s_shelf_categories,false);
    copy(s_category_ui.draft,sizeof(s_category_ui.draft),"新建分类");s_category_ui.command=CATEGORY_CREATE;
    app_touch_t held={.touched=true,.count=1};ctx.touch=&held;
    assert(category_tick(&ctx)==APP_REDRAW_NONE&&s_category_ui.command==CATEGORY_CREATE);ctx.touch=NULL;
    assert(category_tick(&ctx)==APP_REDRAW_PAGE&&s_category_ui.screen==CATEGORY_BOOKS);
    uint32_t target=s_category_ui.destination;assert(book_collection_find(&s_collection,target));
    char path_a[288],path_c[288];copy(path_a,sizeof(path_a),s_shelf[0].path);copy(path_c,sizeof(path_c),s_shelf[2].path);
    s_shelf[0].selected=s_shelf[1].selected=true;s_shelf[0].new_book=true;s_category_ui.command=CATEGORY_MOVE;
    char prefix[288];copy(prefix,sizeof(prefix),s_collection.prefix);copy(s_collection.prefix,sizeof(s_collection.prefix),"/missing-kimiko-test/library");
    category_tick(&ctx);assert(s_view==CATEGORIES&&selected_count()==2&&!book_collection_assignment(&s_collection,s_shelf[0].path));
    assert(s_category_ui.notice[0]);copy(s_collection.prefix,sizeof(s_collection.prefix),prefix);s_category_ui.command=CATEGORY_MOVE;
    category_tick(&ctx);assert(s_view==SHELF&&!selected_count()&&!s_shelf[0].new_book&&book_collection_assignment(&s_collection,s_shelf[0].path)==target);
    assert(book_shelf_categories_id(&s_shelf_categories,book_shelf_categories_selected(&s_shelf_categories))==target);
    // 选择后建类为单次事务；返回不丢选择。/ Creating from a selection is one transaction; Back retains that selection.
    s_shelf_selecting=true;s_shelf[2].selected=true;category_begin(&ctx,true,true);
    assert(category_back(&ctx)==APP_REDRAW_PAGE&&s_view==SHELF&&selected_count()==1);
    category_begin(&ctx,true,true);book_category_ui_edit(&s_category_ui,&s_shelf_categories,false);
    copy(s_category_ui.draft,sizeof(s_category_ui.draft),"另一个分类");s_category_ui.command=CATEGORY_CREATE;
    uint32_t generation=s_collection.generation;category_tick(&ctx);assert(s_view==SHELF&&s_collection.generation==generation+1&&!selected_count());
    uint32_t second=book_collection_assignment(&s_collection,path_c);assert(second&&second!=target);
    category_begin(&ctx,false,false);s_category_quick=true;s_category_ui.command=CATEGORY_UNDO;category_tick(&ctx);
    assert(s_view==SHELF&&!book_collection_assignment(&s_collection,path_c)&&!book_collection_find(&s_collection,second));
    // 来源内移出不影响另一真实分类的选择。/ Source-scoped removal ignores selected books in a different real category.
    second=new_category(&s_collection,"保留归属");
    clear_selection();for(int i=0;i<s_count;++i)if(!strcmp(s_shelf[i].path,path_c))s_shelf[i].selected=true;
    unsigned other_changed=0;assert(book_collection_assign(&s_collection,s_shelf,s_count,true,second,&other_changed)==ESP_OK&&other_changed==1);
    assert(shelf_collection_sync(false));shelf_categories_recount();
    for(int i=0;i<s_count;++i)s_shelf[i].selected=true;
    category_begin(&ctx,true,true);s_category_ui.source=target;s_category_ui.command=CATEGORY_UNCLASSIFY;category_tick(&ctx);
    assert(s_view==SHELF&&!book_collection_assignment(&s_collection,path_a)&&book_collection_assignment(&s_collection,path_c)==second);
    category_begin(&ctx,false,false);s_category_ui.command=CATEGORY_UNDO;category_tick(&ctx);
    assert(book_collection_assignment(&s_collection,path_a)==target);
    s_category_ui.source=target;s_category_ui.command=CATEGORY_REMOVE;category_tick(&ctx);
    assert(!book_collection_find(&s_collection,target)&&!book_collection_assignment(&s_collection,path_a));
    s_category_ui.command=CATEGORY_UNDO;category_tick(&ctx);assert(book_collection_assignment(&s_collection,path_a)==target);
    usb_active=true;assert(!category_storage_ready());usb_active=false;
    copy(sd_identity,40,"card-b");assert(!category_storage_ready());copy(sd_identity,40,"card-a");
    sd_mounted=false;assert(!category_storage_ready());sd_mounted=true;pending=true;assert(!category_storage_ready());pending=false;
    assert(category_storage_ready());s_category_ui.command=CATEGORY_REMOVE;book_category_ui_suspend(&s_category_ui);
    assert(s_category_ui.command==CATEGORY_IDLE);
    // 提交已成功但显示表分配失败，必须报告已保存并阻断重复提交。/ A post-commit binding failure reports the saved result and blocks duplicate submission.
    category_reset();s_view=SHELF;category_begin(&ctx,false,false);book_category_ui_edit(&s_category_ui,&s_shelf_categories,false);
    copy(s_category_ui.draft,sizeof(s_category_ui.draft),"已保存待重读");s_category_ui.command=CATEGORY_CREATE;
    generation=s_collection.generation;binding_fail=true;
    category_tick(&ctx);assert(s_collection.generation==generation+1&&s_view==CATEGORIES&&s_category_ui.blocked&&s_category_ui.screen==CATEGORY_LIST);
    assert(strstr(s_category_ui.notice,"已保存")&&strstr(s_category_ui.notice,"重试读取"));
    assert(book_collection_count(&s_collection)>book_shelf_categories_available(&s_shelf_categories)-2);
    binding_fail=false;s_category_ui.command=CATEGORY_RECOVER;category_tick(&ctx);
    assert(!s_category_ui.blocked&&s_collection.generation==generation+1&&s_collection_bound_generation==s_collection.generation);
    category_reset();book_shelf_categories_dispose(&s_shelf_categories);clean_library(&s_collection);
    puts("category adapter: PASS (real storage, create/pick/assign, rollback/retry, selection/create transaction, unclassify/delete/undo, card/USB guards)");
}
'''


def shelf_fixture():
    code = r'''
#define row_rect shelf_row_rect
#define BOOK_GRID_ROWS 9
#define BOOK_BULK_ROWS 6
#define SHELF_BOOK_LIFT_PX 16
#define SHELF_LIST_BOUNCE_PX 8
#define SHELF_FAST_COVER_WIDTH 164
#define SHELF_FAST_COVER_STRIDE 21
#define SHELF_CATEGORY_ROWS BOOK_SHELF_CATEGORY_MENU_ROWS
#define UI_NAV_TAB_ICON_PX 44
enum {APP_MAIN_REFRESH_NORMAL};
static int app_settings_main_refresh_mode(void){return APP_MAIN_REFRESH_NORMAL;}
static bool app_settings_main_fast_refresh(void){return false;}
static void display_main_transition_gray_bands(const EpdRect *r,int n){assert(n==1&&r->y==(s_shelf_selecting?272:192));}
static void unlock_draw(void){}
static void render(app_ctx_t *,uint8_t *);
static bool fast_cover_matches(int row,EpdRect r){(void)row;(void)r;return false;}
static void draw_shelf_guard(uint8_t *fb,EpdRect r,int row){(void)row;ui_draw_acrylic_guard(fb,r);}
static int leaves(void){int rows=shelf_rows();return s_visible_count?1+(s_visible_count-1)/rows:1;}
static const char *shelf_category_name(int i){return book_shelf_categories_name(&s_shelf_categories,i);}
static int shelf_category_at(int n){return book_shelf_categories_at(&s_shelf_categories,n);}
static int shelf_category_available(void){return book_shelf_categories_available(&s_shelf_categories);}
static unsigned opened_books,requested_nav;
void ui_nav_request(app_ctx_t *ctx,int tab){(void)ctx;requested_nav=(unsigned)tab+1;}
static char s_message[128],s_shelf_warning[128],s_manage_message[128];
static bool s_save_failed,s_batch_confirm,s_file_removed,s_delete_confirm,s_clear_confirm;
static void retry_progress(void){}
static bool open_book(app_ctx_t *ctx,const char *path){(void)ctx;assert(path[0]);++opened_books;return true;}
static app_redraw_t shelf_turn_page(app_ctx_t *ctx,int delta){
    int next=ctx->leaf+delta;if(next<0)next=0;if(next>=leaves())next=leaves()-1;ctx->leaf=next;return APP_REDRAW_PAGE;
}
'''
    for name in ("ui_content_width", "ui_text_box", "ui_pager_area", "ui_pager_rect", "ui_pager_hit", "ui_pager_target", "ui_pager_draw"):
        code += host.function(name, KIT) + "\n"
    code += host.function("book_cover_crop", ROOT / "main/image/image_decode.c") + "\n"
    code += host.function("icon", NAV) + host.function("ui_nav_draw", NAV) + host.function("ui_nav_hit", NAV)
    for name in ("fit_text", "shelf_label", "row_rect", "shelf_cover_image", "shelf_cover_frame_ready", "draw_favorite_icon",
                 "draw_shelf_favorite_icon", "cover_favorite_needs_white_edge", "draw_shelf_cover", "draw_list_title", "draw_list_row",
                 "draw_shelf_furniture", "shelf_header_action_rect", "shelf_category_title_rect", "shelf_manage_rect", "shelf_import_rect", "draw_shelf_header_button", "draw_shelf_pager", "shelf_page_arrow_rect",
                 "shelf_category_box", "draw_category_menu", "shelf_category_gesture", "toggle_selection", "main_page_visible"):
        code += host.function(name, BOOK) + "\n"
    tail = host.function("render", BOOK)
    tail = tail[tail.index("    ui_clear_page(fb);\n    // 留白保持纯白"):]
    code += "static void render(app_ctx_t *ctx,uint8_t *fb){\n" + tail + "\n"
    action = host.function("action_at", BOOK)
    action = action[action.index("    if (s_view == SHELF) {\n        if(s_shelf_selecting"):]
    code += "static app_redraw_t action_at(app_ctx_t *ctx,uint16_t x,uint16_t y){\n" + action + "\n"
    control = host.function("control_at", BOOK)
    guards = control[control.index("\n") + 1:control.index("    if (s_view == BULK)")]
    control = control[control.index("    if (s_view == SHELF && ui_rect_hit(shelf_manage_rect()"):]
    code += "static int control_at(app_ctx_t *ctx,uint16_t x,uint16_t y,EpdRect *rect){\n" + guards + control + "\n"
    gesture = host.function("gesture_event", BOOK).split("    bool indent_handled=false;", 1)[0]
    code += gesture + "return APP_REDRAW_NONE;}\n"
    return code + r'''
static void shelf_snapshot(app_ctx_t *ctx,const char *dir,const char *name){
    book_shelf_covers_cancel(&s_shelf_covers);book_shelf_covers_release(&s_shelf_covers);
    for(int row=0;row<shelf_rows()&&ctx->leaf*shelf_rows()+row<s_visible_count;++row){
        int i=ctx->leaf*shelf_rows()+row;book_cover_job_t request={0};copy(request.path,288,s_shelf[i].path);copy(request.title,256,s_shelf[i].name);copy(request.author,128,s_shelf[i].author);
        book_shelf_covers_prepare(&s_shelf_covers,row,i,true,false,0,0,ctx->now_ms);
        assert(book_shelf_covers_collect(&s_shelf_covers,row,&request));
    }
    int page=ctx->leaf,selected=book_shelf_categories_selected(&s_shelf_categories);unsigned n=selected_count();render(ctx,ctx->fb);
    assert(ctx->leaf==page&&book_shelf_categories_selected(&s_shelf_categories)==selected&&selected_count()==n);
    char path[1024];snprintf(path,sizeof(path),"%s/%s-%u.raw",dir,name,font_scale);save(path,ctx->fb);
}
static void shelf_checks(uint8_t *fb,const char *directory,const char *output){
    app_ctx_t ctx={.fb=fb,.now_ms=1000};font_scale=100;adapter_setup(directory);
    // 浏览过的分类保留初始排序，不受其他分类后来改动默认值影响。/ A visited category keeps its initial sort when another changes the default.
    sort_shelf(&ctx);int custom=2;assert(book_shelf_categories_select(&s_shelf_categories,custom));s_recent_sort=true;sort_shelf(&ctx);
    assert(book_shelf_categories_select(&s_shelf_categories,0)&&!shelf_query().recent_sort);
    pending=true;sd_mounted=false;assert(!legacy_category_recover()&&!recovery_calls);sd_mounted=true;
    usb_active=true;assert(!legacy_category_recover()&&!recovery_calls);usb_active=false;
    transfer_idle=false;assert(!legacy_category_recover()&&!recovery_calls);transfer_idle=true;
    cover_idle=false;assert(!legacy_category_recover()&&!recovery_calls);cover_idle=true;
    recovery_error=ESP_FAIL;assert(!legacy_category_recover()&&recovery_calls==1&&!recovery_invalidations);
    recovery_error=ESP_OK;assert(legacy_category_recover()&&recovery_calls==2&&recovery_invalidations==3);
    assert(legacy_category_recover()&&recovery_calls==2);
    s_count=18;
    const char *titles[]={"山海经","朝花夕拾","人间词话","小王子","论语","围城","边城","诗经","红楼梦"};
    for(int i=0;i<s_count;++i){memset(&s_shelf[i],0,sizeof(s_shelf[i]));snprintf(s_shelf[i].path,288,"/sdcard/books/book%d.txt",i);
        copy(s_shelf[i].name,256,titles[i%9]);copy(s_shelf[i].author,128,"作者未知");s_shelf[i].search_match=true;s_shelf[i].favorite=i==1;}
    assert(book_collection_sync(&s_collection,"/sdcard/books",NULL,0,s_shelf,s_count,true)==ESP_OK);
    uint32_t target=book_collection_at(&s_collection,0)->id;unsigned changed=0;
    for(int i=0;i<12;++i)s_shelf[i].selected=true;
    assert(book_collection_assign(&s_collection,s_shelf,s_count,true,target,&changed)==ESP_OK&&changed==12);
    assert(shelf_collection_sync(false));shelf_categories_recount();clear_selection();
    for(unsigned scale=100;scale<=200;scale+=25){
        font_scale=scale;shelf_style=2;s_view=SHELF;s_shelf_selecting=false;category_select(&ctx,target);ctx.leaf=0;clear_selection();
        shelf_snapshot(&ctx,output,"shelf-grid");
        EpdRect hit,title=shelf_category_title_rect(),previous={0};
        for(int action=0;action<4;++action){
            EpdRect r=shelf_header_action_rect(action);
            assert(r.y==80&&r.width==80&&r.height==80&&r.x+r.width<=648);
            assert(!ui_rect_hit(title,r.x+r.width/2,r.y+r.height/2));
            if(action)assert(r.x-previous.x-previous.width==12);
            int control=control_at(&ctx,r.x+r.width/2,r.y+r.height/2,&hit);
            assert(control==(action<2?114+action:118+action)&&!memcmp(&hit,&r,sizeof(r)));
            assert(control_at(&ctx,r.x+r.width/2,159,&hit)==control);
            if(action<3)assert(control_at(&ctx,r.x+r.width+6,120,&hit)==-1);
            uint8_t *before=malloc(684*1216/2);assert(before);memcpy(before,fb,684*1216/2);
            s_pressed_control=control;draw_shelf_header_button(fb,r,action);
            for(int y=0;y<1216;++y)for(int x=0;x<684;++x)
                if(!ui_rect_hit(r,x,y))assert(ui_read_pixel(fb,x,y)==ui_read_pixel(before,x,y));
            s_pressed_control=-1;draw_shelf_header_button(fb,r,action);
            assert(!memcmp(before,fb,684*1216/2));free(before);
            previous=r;
        }
        assert(action_at(&ctx,516,120)==APP_REDRAW_PAGE&&s_view==BULK);s_view=SHELF;
        assert(action_at(&ctx,608,120)==APP_REDRAW_PAGE&&s_view==IMPORT);s_view=SHELF;
        assert(action_at(&ctx,516,168)==APP_REDRAW_PAGE&&s_view==CATEGORIES&&s_category_ui.destination==target);
        category_back(&ctx);assert(s_view==SHELF&&!s_shelf_selecting);
        assert(action_at(&ctx,424,120)==APP_REDRAW_PAGE&&s_shelf_selecting&&!selected_count());
        assert(!main_page_visible(&ctx));
        assert(action_at(&ctx,500,1148)==APP_REDRAW_NONE&&!requested_nav&&s_view==SHELF);
        assert(action_at(&ctx,320,1148)==APP_REDRAW_NONE&&!requested_nav&&s_view==SHELF);
        assert(action_at(&ctx,100,1204)==APP_REDRAW_NONE&&!requested_nav&&s_view==SHELF);
        EpdRect first=row_rect(0);assert(gesture_event(&ctx,&(ui_gesture_event_t){.type=UI_GESTURE_TAP,.x0=first.x+80,.y0=first.y+80})==APP_REDRAW_PAGE&&selected_count()==1&&!opened_books);
        shelf_snapshot(&ctx,output,"shelf-grid-selected");
        assert(gesture_event(&ctx,&(ui_gesture_event_t){.type=UI_GESTURE_TAP,.x0=160,.y0=110})==APP_REDRAW_NONE&&selected_count()==1&&!book_shelf_categories_menu_is_open(&s_shelf_categories));
        assert(gesture_event(&ctx,&(ui_gesture_event_t){.type=UI_GESTURE_TAP,.x0=160,.y0=222})==APP_REDRAW_AREA&&book_shelf_categories_menu_is_open(&s_shelf_categories));
        shelf_snapshot(&ctx,output,"shelf-category-switch");book_shelf_categories_menu_close(&s_shelf_categories);
        assert(action_at(&ctx,356,120)==APP_REDRAW_PAGE&&s_view==CATEGORIES&&s_category_ui.screen==CATEGORY_SELECTED_ACTIONS&&s_category_ui.source==target);
        book_category_ui_draw(&s_category_ui,&s_shelf_categories,fb);
        ui_gesture_event_t remove={.type=UI_GESTURE_TAP,.x0=200,.y0=376};
        assert(category_gesture(&ctx,&remove)==APP_REDRAW_PAGE&&s_category_ui.command==CATEGORY_UNCLASSIFY&&selected_count()==1);
        s_category_ui.command=CATEGORY_IDLE;category_back(&ctx);assert(s_view==SHELF&&s_shelf_selecting&&selected_count()==1);
        assert(gesture_event(&ctx,&(ui_gesture_event_t){.type=UI_GESTURE_SWIPE_L})==APP_REDRAW_PAGE&&ctx.leaf==1&&selected_count()==1);
        assert(action_at(&ctx,140,1148)==APP_REDRAW_PAGE&&s_view==CATEGORIES&&s_category_ui.review&&selected_count()==1&&!requested_nav);
        category_back(&ctx);assert(s_view==SHELF&&s_shelf_selecting&&selected_count()==1);
        assert(action_at(&ctx,600,120)==APP_REDRAW_PAGE&&!s_shelf_selecting&&!selected_count());
        assert(main_page_visible(&ctx));
        assert(action_at(&ctx,500,168)==APP_REDRAW_PAGE&&s_view==CATEGORIES&&s_category_ui.destination==target&&s_category_ui.screen==CATEGORY_BOOKS);
        category_back(&ctx);assert(s_view==SHELF&&!s_shelf_selecting);
        assert(action_at(&ctx,332,120)==APP_REDRAW_PAGE&&s_view==SEARCH);s_view=SHELF;
        shelf_style=5;category_select(&ctx,target);shelf_snapshot(&ctx,output,"shelf-list");
        assert(action_at(&ctx,424,120)==APP_REDRAW_PAGE&&s_shelf_selecting);assert(action_at(&ctx,100,340)==APP_REDRAW_PAGE&&selected_count()==1);
        shelf_snapshot(&ctx,output,"shelf-list-selected");clear_selection();s_shelf_selecting=false;
        category_select(&ctx,BOOK_COLLECTION_ALL);shelf_style=2;ctx.leaf=0;
        for(int i=0;i<s_count;++i)s_shelf[i].new_book=false;
        shelf_snapshot(&ctx,output,"shelf-all");
        for(int y=160;y<192;++y)for(int x=36;x<648;++x)assert(ui_read_pixel(fb,x,y)==15);
        assert(action_at(&ctx,608,168)==APP_REDRAW_NONE&&s_view==SHELF);
        assert(action_at(&ctx,516,168)==APP_REDRAW_NONE&&s_view==SHELF);
        s_shelf[0].new_book=true;shelf_snapshot(&ctx,output,"shelf-new-books");
        assert(action_at(&ctx,160,176)==APP_REDRAW_PAGE&&s_view==CATEGORIES&&selected_count()==1);
        category_back(&ctx);clear_selection();s_shelf[0].new_book=false;
        copy(s_shelf_warning,sizeof(s_shelf_warning),"分类资料暂不可用，仍可浏览全部书籍");
        shelf_snapshot(&ctx,output,"shelf-warning");
        s_shelf_selecting=true;shelf_snapshot(&ctx,output,"shelf-grid-selected-warning");
        shelf_style=5;shelf_snapshot(&ctx,output,"shelf-list-selected-warning");
        s_shelf_selecting=false;s_shelf_warning[0]=0;
    }
    book_shelf_covers_cancel(&s_shelf_covers);book_shelf_covers_release(&s_shelf_covers);
    book_shelf_categories_dispose(&s_shelf_categories);clean_library(&s_collection);
    puts("category shelf: PASS (actual grid/list render and tap routing, covers, selection/review/filter/page retention, per-category sort, legacy recovery guards)");
}
#undef row_rect
'''


def unit():
    code = host.host_preamble()
    code = code.replace("#define UI_GRAY_LIGHT 224", "#define UI_GRAY_LIGHT 0x90")
    begin, end = code.index("static struct {void *p;"), code.index("static enum EpdRotation")
    code = code[:begin] + r'''
void *heap_caps_malloc(size_t n,int c){(void)c;return malloc(n);}
void *heap_caps_calloc(size_t n,size_t z,int c){(void)c;return calloc(n,z);}
void *heap_caps_realloc(void *p,size_t n,int c){(void)c;return realloc(p,n);}
void heap_caps_free(void *p){free(p);}
size_t heap_caps_get_free_size(int c){(void)c;return 16*1024*1024;}
size_t heap_caps_get_largest_free_block(int c){(void)c;return 16*1024*1024;}
void lock_pin_wipe(void *p,unsigned n){memset(p,0,n);}
''' + code[end:]
    code = re.sub(r"^static [^\n]*\bui_font_\w+\([^\n]*\n", "", code, flags=re.M)
    code += r'''
#undef UI_ICON_CHEVRON_RIGHT
#undef UI_ICON_CHEVRON_LEFT
#undef UI_ICON_ANIMATE_CIRCLE_CHEVRON_LEFT
#include <math.h>
#include <unistd.h>
#include "book_shelf_categories.h"
#include "book_search.h"
#include "ui_font.h"
#include "esp_heap_caps.h"
#include "assets/ui_icons.h"
#include "book_category_ui.h"
#include "ui_nav_layout.h"
#define UI_GAP 12
#define UI_BTN_H 84
#define UI_PX_CAPTION 28
#define UI_CONTENT_BOTTOM 1076
enum {UI_PAGER_NONE=-1,UI_PAGER_FIRST,UI_PAGER_PREVIOUS,UI_PAGER_NEXT,UI_PAGER_LAST};
static unsigned font_scale=100;
static bool s_shelf_selecting;
static unsigned app_settings_system_font_size(void){return font_scale;}
uint8_t ui_read_pixel(const uint8_t *fb,int x,int y);
EpdRect ui_rotate_rect_to_fb(EpdRect rect);
void ui_clear_page(uint8_t *f){memset(f,255,684*1216/2);}
'''
    code += host.epdiy_slices()
    code += host.function("ui_control_gray", ROOT / "main/ui/ui_kit.h")
    for name in ("clamp_radius", "draw_arc", "ui_draw_round_rect_raw", "ui_draw_round_rect",
                 "ui_fill_round_rect_raw", "ui_fill_round_rect", "ui_inset_rect", "ui_draw_control_frame",
                 "ui_draw_separator", "ui_hairline", "icon_nibble", "ui_draw_icon", "ui_read_pixel",
                 "ui_text_effective_px", "ui_text_fixed_context_width_px", "ui_text_fixed_width_px",
                 "ui_text_fixed_context_vc", "ui_text_fixed_vc", "ui_text_fixed_ink_vc", "ui_text_vc", "ui_blit_mono_bitmap", "draw_frosted", "ui_draw_acrylic_guard",
                 "ui_rotate_rect_to_fb", "clip_refresh_rect", "ui_clear_rect_fast", "ui_rect_union"):
        code += host.function(name, KIT) + "\n"
    code += r'''
void ui_nav_status(uint8_t *fb){ui_text_fixed_vc(fb,36,38,26,"08:30",EPD_DRAW_ALIGN_LEFT,false);}
void ui_nav_back(uint8_t *fb,int x,int y){ui_draw_icon(fb,x+26,y+28,36,UI_ICON_CHEVRON_LEFT,0);}
bool ui_rect_hit(EpdRect r,uint16_t x,uint16_t y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static unsigned checks;
static void checked_text(uint8_t *fb,int x,int y,int px,const char *s,enum EpdFontFlags align,bool white){
    if(!ui_font_has_text(s)){fprintf(stderr,"Missing category glyph: %s\n",s);abort();}
    int w=ui_text_fixed_width_px(px,s),left=x,above=0,below=0;ui_font_measure_line_px(px,s,&above,&below);
    if(align==EPD_DRAW_ALIGN_CENTER)left-=w/2;else if(align==EPD_DRAW_ALIGN_RIGHT)left-=w;
    int base=y+(above-below)/2;
    assert(left>=24&&left+w<=660&&base-above>=60&&base+below<1196);++checks;
    if(strstr(s,"分类资料暂不可用"))assert(base-above>=(s_shelf_selecting?1000:984)&&base+below<(s_shelf_selecting?1020:1006));
    ui_text_fixed_vc(fb,x,y,px,s,align,white);
}
#define ui_text_fixed_vc checked_text
'''
    code += re.sub(r"^#(?:include[^\n]*|pragma once)\n", "", (ROOT / "main/apps/book_selection_ui.h").read_text(), flags=re.M)
    code += re.sub(r"^#include[^\n]*\n", "", (ROOT / "main/apps/book_category_ui.c").read_text(), flags=re.M)
    code += adapter()
    code += shelf_fixture()
    code += r'''
#undef ui_text_fixed_vc
static int tap(book_category_ui_t *u,book_shelf_categories_t *c,int x,int y){ui_gesture_event_t e={.type=UI_GESTURE_TAP,.x0=x,.y0=y};return book_category_ui_gesture(u,c,&e,1000);}
static int tap_selection(book_category_ui_t *u,book_shelf_categories_t *c,book_selection_control_t control){EpdRect r=book_selection_rect(control);return tap(u,c,r.x+r.width/2,r.y+r.height/2);}
static void disabled_button_checks(uint8_t *fb,EpdRect r){
    unsigned ink=0,white=0,total=0;
    for(int y=r.y+12;y<r.y+r.height-12;++y)for(int x=r.x+16;x<r.x+r.width-16;++x){
        unsigned gray=ui_read_pixel(fb,x,y);assert(gray>=9);++total;if(gray<15)++ink;else ++white;
    }
    assert(ink>20&&white>total*2/3);
}
static void keyboard_click(book_category_ui_t *u,book_shelf_categories_t *c,uint8_t *fb,int x,int y){
    ui_gesture_event_t e={.type=UI_GESTURE_PRESS,.x0=x,.y0=y,.x=x,.y=y};
    assert(book_category_ui_gesture(u,c,&e,1000)==1);
    assert(book_category_ui_input_paint(u,fb).area.width);
    e.type=UI_GESTURE_TAP;assert(book_category_ui_gesture(u,c,&e,1040)==1);
    assert(book_category_ui_input_paint(u,fb).area.width);
}
static void snapshot(book_category_ui_t *u,book_shelf_categories_t *c,uint8_t *fb,const char *dir,const char *name){
    if(u->screen==CATEGORY_BOOKS){
        book_shelf_covers_cancel(&s_category_covers);book_shelf_covers_release(&s_category_covers);
        for(int row=0;row<book_category_ui_rows(true);++row){
            int i=book_category_ui_book_at(u,u->page*book_category_ui_rows(true)+row);if(i<0)break;
            book_cover_job_t request={0};copy(request.path,288,u->books[i].path);copy(request.title,256,u->books[i].name);
            book_shelf_covers_prepare(&s_category_covers,row,i,true,false,0,0,1000);assert(book_shelf_covers_collect(&s_category_covers,row,&request));
        }
    }
    book_category_ui_t before=*u;book_category_ui_draw(u,c,fb);assert(!memcmp(&before,u,sizeof(before)));
    char path[1024];snprintf(path,sizeof(path),"%s/%s-%u.raw",dir,name,font_scale);save(path,fb);
}
int main(int argc,char **argv){
    assert(argc==3&&ttf_font_open_builtin()==ESP_OK);uint8_t *fb=malloc(684*1216/2);assert(fb);
    ui_clear_page(fb);command_text(fb,(EpdRect){36,1004,612,80},28,"添加 0 本",false);
    assert(ui_read_pixel(fb,36,1004)==15);
    unsigned disabled_ink=0;
    for(int y=1016;y<1072;++y)for(int x=48;x<636;++x)if(ui_read_pixel(fb,x,y)<15)++disabled_ink;
    assert(disabled_ink>20&&disabled_ink<612*80/3);
    book_collection_t library={0};char prefix[288];snprintf(prefix,sizeof(prefix),"%s/ui-library",argv[2]);
    assert(book_collection_open(&library,prefix)==ESP_OK);
    book_shelf_entry_t books[18]={0};
    for(int i=0;i<18;++i){snprintf(books[i].path,288,"/sdcard/books/book%d.txt",i);snprintf(books[i].name,256,"特别图书 %02d",i);books[i].search_match=true;}
    copy(books[3].name,256,"一本有很长很长名字的图书用于确认完整书名不会在选书页面被隐藏并且可以打开查看详细名称");
    assert(book_collection_sync(&library,"/sdcard/books",NULL,0,books,18,true)==ESP_OK);
    uint32_t a=new_category(&library,"文学"),b=new_category(&library,"科学");unsigned changed=0;
    books[0].selected=books[1].selected=true;assert(book_collection_assign(&library,books,18,true,a,&changed)==ESP_OK);
    for(int i=0;i<14;++i){char name[97];snprintf(name,sizeof(name),"分类 %d",i);(void)new_category(&library,name);}
    (void)new_category(&library,"一二三四五六七八九十一二三四五六七八九十一二三四");
    book_collection_apply(&library,books,18);
    book_shelf_categories_t categories={0};book_shelf_categories_init(&categories,"/sdcard/books");
    assert(book_shelf_categories_bind(&categories,&library));book_shelf_categories_recount(&categories,books,18);
    for(unsigned scale=100;scale<=200;scale+=25){
        font_scale=scale;book_shelf_clear_selection(books,18);book_category_ui_t u;
        book_category_ui_begin(&u,false);u.can_undo=true;snapshot(&u,&categories,fb,argv[1],"manager");
        int center=224+728/book_category_ui_rows(false)/2;
        assert(tap(&u,&categories,620,center)==1&&u.screen==CATEGORY_ACTIONS&&u.source==a);
        snapshot(&u,&categories,fb,argv[1],"actions");
        assert(tap(&u,&categories,200,488)==1&&u.screen==CATEGORY_EDIT&&!strcmp(u.draft,"文学"));
        snapshot(&u,&categories,fb,argv[1],"rename");assert(tap(&u,&categories,620,105)==1&&u.command==CATEGORY_RENAME);
        book_category_ui_suspend(&u);assert(u.command==CATEGORY_IDLE);assert(!book_category_ui_back(&u)&&u.screen==CATEGORY_ACTIONS);
        assert(tap(&u,&categories,200,600)==1&&u.screen==CATEGORY_ORDER);snapshot(&u,&categories,fb,argv[1],"order");
        assert(tap(&u,&categories,342,572)==1&&u.command==CATEGORY_DOWN);u.command=CATEGORY_IDLE;assert(!book_category_ui_back(&u));
        assert(tap(&u,&categories,200,712)==1&&u.screen==CATEGORY_CONFIRM);snapshot(&u,&categories,fb,argv[1],"delete-confirm");
        assert(tap(&u,&categories,500,690)==1&&u.command==CATEGORY_REMOVE);u.command=CATEGORY_IDLE;
        book_category_ui_end(&u);book_category_ui_begin(&u,true);snapshot(&u,&categories,fb,argv[1],"target");
        assert(tap(&u,&categories,200,center)==1&&u.command==CATEGORY_MOVE&&u.destination==0);u.command=CATEGORY_IDLE;
        assert(tap(&u,&categories,620,108)==1&&u.screen==CATEGORY_EDIT);snapshot(&u,&categories,fb,argv[1],"new-category");
        assert(tap(&u,&categories,620,108)==1&&u.command==CATEGORY_IDLE&&u.notice[0]);
        assert(ui_text_edit_insert(&u.input,"新建分类"));assert(tap(&u,&categories,620,108)==1&&u.command==CATEGORY_CREATE);u.command=CATEGORY_IDLE;
        book_category_ui_end(&u);book_category_ui_begin(&u,false);u.books=books;u.count=18;u.cover=category_cover_draw;book_category_ui_books(&u,a,false);
        assert(u.filter==0&&book_category_ui_book_count(&u)==16);snapshot(&u,&categories,fb,argv[1],"add-empty-selection");
        disabled_button_checks(fb,book_selection_rect(BOOK_SELECTION_SUBMIT));
        assert(book_selection_rect(BOOK_SELECTION_SUBMIT).y+book_selection_rect(BOOK_SELECTION_SUBMIT).height==1196);
        assert(!tap_selection(&u,&categories,BOOK_SELECTION_SUBMIT)&&u.command==CATEGORY_IDLE);
        assert(!tap(&u,&categories,320,1148)&&u.command==CATEGORY_IDLE);
        assert(!tap(&u,&categories,320,322)&&!book_shelf_selected_count(books,18));
        assert(tap(&u,&categories,200,book_rect(&u,0).y+book_rect(&u,0).height/2)==1&&book_shelf_selected_count(books,18)==1);
        ui_gesture_event_t swipe={.type=UI_GESTURE_SWIPE_U};assert(book_category_ui_gesture(&u,&categories,&swipe,1000)==1);
        assert(tap(&u,&categories,200,book_rect(&u,0).y+book_rect(&u,0).height/2)==1&&book_shelf_selected_count(books,18)==2);
        copy(u.query,sizeof(u.query),"no-match");u.page=0;assert(!book_category_ui_book_count(&u));snapshot(&u,&categories,fb,argv[1],"search-empty");
        assert(tap_selection(&u,&categories,BOOK_SELECTION_REVIEW)==1&&u.review&&book_category_ui_book_count(&u)==2);snapshot(&u,&categories,fb,argv[1],"review");
        assert(tap(&u,&categories,480,322)==1&&!book_shelf_selected_count(books,18));
        u.review=false;u.query[0]=0;u.filter=b;snapshot(&u,&categories,fb,argv[1],"filter-empty");
        assert(tap(&u,&categories,300,648)==1&&u.filter==BOOK_COLLECTION_ALL);
        assert(tap(&u,&categories,200,222)==1&&u.screen==CATEGORY_FILTER);snapshot(&u,&categories,fb,argv[1],"filter");
        assert(!book_category_ui_back(&u)&&u.screen==CATEGORY_BOOKS);
        u.filter=0;u.page=0;assert(tap(&u,&categories,620,book_rect(&u,0).y+book_rect(&u,0).height/2)==1&&u.screen==CATEGORY_BOOK_INFO);u.book_info=3;
        snapshot(&u,&categories,fb,argv[1],"full-book-name");
        char original[256];copy(original,sizeof(original),books[3].name);memset(books[3].name,'W',255);books[3].name[255]=0;
        books[3].category_id=book_collection_at(&library,book_collection_count(&library)-1)->id;
        snapshot(&u,&categories,fb,argv[1],"full-book-name-wide");copy(books[3].name,sizeof(books[3].name),original);books[3].category_id=0;
        assert(!book_category_ui_back(&u));
        assert(tap(&u,&categories,480,322)==1&&book_shelf_selected_count(books,18)==16);
        snapshot(&u,&categories,fb,argv[1],"add-selected");
        assert(tap_selection(&u,&categories,BOOK_SELECTION_SUBMIT)==1&&u.command==CATEGORY_MOVE);u.command=CATEGORY_IDLE;
        book_category_ui_books(&u,BOOK_COLLECTION_ALL,true);assert(u.review&&u.selecting_only);snapshot(&u,&categories,fb,argv[1],"review-before-target");
        assert(tap_selection(&u,&categories,BOOK_SELECTION_SUBMIT)==1&&u.screen==CATEGORY_LIST&&u.picker);
        book_category_ui_refresh(&u,&categories,book_collection_at(&library,book_collection_count(&library)-1)->id);
        snapshot(&u,&categories,fb,argv[1],"last-page-long-name");u.blocked=true;copy(u.notice,sizeof(u.notice),"分类资料不可用，原书籍不受影响");
        snapshot(&u,&categories,fb,argv[1],"recovery");assert(tap(&u,&categories,300,480)==1&&u.command==CATEGORY_RECOVER);book_category_ui_end(&u);
    }
    book_shelf_categories_dispose(&categories);clean_library(&library);adapter_checks(fb,argv[2]);shelf_checks(fb,argv[2],argv[1]);free(fb);
    printf("category UI: PASS (%u real-font bounds/glyph checks, 100/125/150/175/200%%, disabled ink, pure render, cross-page/search/filter selection, review, explicit scopes and full names)\n",checks);
}
'''
    return code


def run(output):
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="category-ui-") as directory:
        work = Path(directory)
        host.write_shims(work)
        (work / "esp_err.h").write_text((ROOT / "tools/book_storage_stubs/esp_err.h").read_text())
        (work / "esp_heap_caps.h").write_text((work / "esp_heap_caps.h").read_text() + "\nvoid *heap_caps_realloc(void *,size_t,int);\n")
        (work / "epdiy.h").write_text((work / "epdiy.h").read_text().replace(
            "EPD_DRAW_ALIGN_LEFT, EPD_DRAW_ALIGN_CENTER, EPD_DRAW_ALIGN_RIGHT", "EPD_DRAW_ALIGN_LEFT=2, EPD_DRAW_ALIGN_CENTER=8, EPD_DRAW_ALIGN_RIGHT=4"))
        (work / "miniz.h").write_text('''#pragma once
#include <stddef.h>
#include <zlib.h>
typedef struct {int state;} tinfl_decompressor;
typedef int tinfl_status;
#define TINFL_STATUS_DONE 0
#define TINFL_FLAG_PARSE_ZLIB_HEADER 1
#define TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF 4
#define tinfl_init(s) ((s)->state=0)
static inline tinfl_status tinfl_decompress(tinfl_decompressor *s,const unsigned char *in,size_t *n,unsigned char *base,unsigned char *out,size_t *m,int flags){(void)s;(void)base;(void)flags;uLongf size=*m;int result=uncompress(out,&size,in,*n);*m=size;return result==Z_OK?0:-1;}
''')
        payload = (ROOT / "main/assets/ui-hanzi.bin").read_bytes()
        with (work / "assets.c").open("a") as assets:
            assets.write('const uint8_t hanzi[] asm("_binary_ui_hanzi_bin_start")={' + ",".join(map(str, payload)) + '};\n')
            assets.write(f'asm(".globl _binary_ui_hanzi_bin_end\\n.set _binary_ui_hanzi_bin_end, _binary_ui_hanzi_bin_start + {len(payload)}");\n')
        (work / "test.c").write_text(unit())
        env, compiler = toolchain(work)
        flags = compiler + ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable", "-Wno-unused-function", "-Wno-deprecated-declarations", "-fsanitize=address,undefined", "-g"]
        for folder in (work, ROOT / "main", ROOT / "main/ui", ROOT / "main/font", ROOT / "main/apps", ROOT / "main/book", ROOT / "main/diagnostics", ROOT / "main/display", ROOT / "main/settings", ROOT / "main/app", ROOT / "main/boot", ROOT / "main/services", ROOT / "components/book_search/include"):
            flags += ["-I" + str(folder)]
        units = [work / "test.c", work / "assets.c"] + [ROOT / "main/font" / name for name in ("ttf_font.c", "ttf_font_source.c", "ttf_block_cache.c", "ttf_glyph_cache.c", "ui_font.c", "ui_hanzi.c")]
        units += [ROOT / name for name in ("main/ui/ui_text_edit.c", "main/ui/ui_text_input.c", "main/ui/ui_keyboard.c", "main/ui/ui_ime.c", "main/ui/ui_gesture.c", "main/ui/ui_image_dither.c", "components/book_search/book_search.c", "main/book/book_shelf_category_state.c", "main/book/book_shelf_model.c", "main/book/book_collection.c", "main/book/book_shelf_covers.c", "main/book/book_cover_auto.c")]
        flags += ["-ffunction-sections", "-fdata-sections"]
        linker = "-Wl,-dead_strip" if platform.system() == "Darwin" else "-Wl,--gc-sections"
        subprocess.run(flags + list(map(str, units)) + [linker, "-lm", "-lz", "-o", str(work / "test")], env=env, check=True)
        subprocess.run([str(work / "test"), str(output), str(work)], env=env, check=True)
    for raw in output.glob("*.raw"):
        Image.frombytes("L", (684, 1216), raw.read_bytes()).point(lambda v: v * 17 // 16).save(raw.with_suffix(".png"))
        raw.unlink()
    print(f"category screenshots: {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/category-ui")
    run(parser.parse_args().output.resolve())
