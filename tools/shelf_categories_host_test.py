"""中文：链接真实分类状态和排序模块，验证分页、目录、菜单及分配失败。
English: Link real category-state and sorting modules to verify pages, folders, menus and allocation failures.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
s = (root / 'main/apps/app_book.c').read_text()


def function(name):
    tokens = r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    scan = re.sub(tokens, lambda m: ' ' * len(m[0]) if m[0].startswith(('//', '/*')) else m[0], s)
    match = re.search(r'^static [^\n]+\b' + name + r'\([^;{}]*\)\s*\{', scan, re.M)
    assert match, name
    at, depth, quote, escape = match.end(), 1, None, False
    while depth:
        c = scan[at]
        if quote:
            if escape:
                escape = False
            elif c == '\\':
                escape = True
            elif c == quote:
                quote = None
        elif c in "\"'":
            quote = c
        elif c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
        at += 1
    return s[match.start():at]


unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "book_shelf_categories.h"
#include "book_shelf_category_state.h"
#define SHELF 0
static bool fail_alloc;
void *heap_caps_realloc(void *p,size_t bytes,int caps){assert(caps==3);return fail_alloc?NULL:realloc(p,bytes);}
void *heap_caps_calloc(size_t count,size_t bytes,int caps){assert(caps==3);return fail_alloc?NULL:calloc(count,bytes);}
static const char *books_root="/sdcard/books";
static const char *app_settings_books_dir(void){return books_root;}
static int style;
static int app_settings_shelf_style(void){return style;}
static int shelf_rows(void){return style==5?4:9;}
static unsigned invalidations;
static void invalidate_covers(void){++invalidations;}
typedef struct{int x,y,width,height;}EpdRect;
typedef struct{int leaf;uint8_t *fb;int requested_tab;}app_ctx_t;
typedef enum{APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE}app_redraw_t;
typedef enum{UI_GESTURE_TAP,UI_GESTURE_SWIPE_U,UI_GESTURE_SWIPE_D,UI_GESTURE_SWIPE_L}ui_gesture_type_t;
typedef struct{ui_gesture_type_t type;int x0,y0;}ui_gesture_event_t;
#define SHELF_CATEGORY_ROWS BOOK_SHELF_CATEGORY_MENU_ROWS
#define MODE_GL16 5
static EpdRect s_area;
static int s_mode,render_count,clear_count;
static void render(app_ctx_t *ctx,uint8_t *fb){(void)ctx;(void)fb;++render_count;}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static int ui_nav_hit(int x,int y){return y>=1135?x/171:-1;}
static void ui_nav_request(app_ctx_t *ctx,int tab){ctx->requested_tab=tab;}
static book_shelf_categories_t s_shelf_categories;
static book_shelf_categories_t s_legacy_categories;
static book_collection_t s_collection;
static bool s_shelf_selecting;
static struct {bool blocked;} s_category_ui;
static void book_category_ui_edit(void *ui,const book_shelf_categories_t *c,bool rename){(void)ui;(void)c;assert(!rename);}
static int s_view=SHELF;
static unsigned manager_entries;
static void category_begin(app_ctx_t *ctx,bool picker,bool bulk){(void)ctx;assert(!picker&&!bulk);++manager_entries;}
static bool s_scan_pending,s_shelf_cache_valid,s_recent_sort;
static int s_count,s_visible_count,s_filter;
static book_shelf_entry_t s_shelf[40];
'''
unit += 'static void clear_selection(void){++clear_count;book_shelf_clear_selection(s_shelf,s_count); }\n'
for name in ('shelf_category_add', 'shelf_category_init', 'shelf_category_name', 'shelf_query',
             'shelf_category_remember', 'shelf_category_restore', 'shelf_categories_recount', 'sort_shelf',
             'shelf_category_at', 'shelf_category_available', 'shelf_category_box', 'shelf_category_gesture'):
    unit += function(name) + '\n'
unit += r'''
int main(int argc,char **argv){
 assert(argc==2);
 char child[288];assert(book_shelf_child("/sdcard/books/","/sdcard/books/文学/深层/a.epub",child,sizeof(child))&&!strcmp(child,"文学"));
 assert(!book_shelf_child("/sdcard/books","/sdcard/books/a.epub",child,sizeof(child)));
 assert(!book_shelf_child("/sdcard/books","/sdcard/books2/文学/a.epub",child,sizeof(child)));
 assert(!book_shelf_child("/sdcard/books","/sdcard/books/.hidden/a.epub",child,sizeof(child)));
 assert(!book_shelf_child("/sdcard/books","/sdcard/book",child,sizeof(child)));
 assert(!book_shelf_child("/sdcard/books","/sdcard/books/文学/a.epub",child,3));
 assert(book_shelf_saved_page(100,10,9)==1&&book_shelf_saved_page(-1,10,9)==0&&book_shelf_saved_page(2,0,9)==0);
 fail_alloc=true;s_shelf_cache_valid=true;shelf_category_init();assert(!s_shelf_cache_valid);
 assert(book_shelf_categories_available(&s_shelf_categories)==0);
 assert(!book_shelf_categories_menu_open(&s_shelf_categories));
 book_shelf_query_t query=shelf_query();assert(book_shelf_category_matches(&query,"anything"));
 assert(!book_shelf_categories_select(&s_shelf_categories,0));
 app_ctx_t ctx={.leaf=9};shelf_category_restore(&ctx);assert(ctx.leaf==0);
 fail_alloc=false;shelf_category_init();assert(book_shelf_categories_available(&s_shelf_categories)==2);
 s_shelf_cache_valid=true;shelf_category_init();assert(s_shelf_cache_valid);
 int literature=shelf_category_add("/sdcard/books/文学");int empty=shelf_category_add("/sdcard/books/空目录");assert(literature==2&&empty==3);
 assert(shelf_category_add("/sdcard/books/文学")==literature);
 for(int i=0;i<24;++i){snprintf(s_shelf[i].path,288,i<18?"/sdcard/books/文学/nested/%d.epub":"/sdcard/books/%d.epub",i);snprintf(s_shelf[i].name,256,"b%02d",i);s_shelf[i].search_match=true;}
 snprintf(s_shelf[23].path,288,"/sdcard/book/legacy.epub");s_count=24;shelf_categories_recount();
 assert(book_shelf_categories_count(&s_shelf_categories,0)==24&&book_shelf_categories_count(&s_shelf_categories,1)==6);
 assert(book_shelf_categories_count(&s_shelf_categories,literature)==18&&book_shelf_categories_count(&s_shelf_categories,empty)==0);
 assert(!strcmp(shelf_category_name(literature),"文学")&&!strcmp(shelf_category_name(1),"未分类"));
 assert(!strcmp(shelf_category_name(-1),"全部书籍")&&book_shelf_categories_count(&s_shelf_categories,129)==0);
 sort_shelf(&ctx);assert(invalidations==1);ctx.leaf=2;shelf_category_remember(&ctx);
 assert(book_shelf_categories_select(&s_shelf_categories,literature));sort_shelf(&ctx);shelf_category_restore(&ctx);assert(s_visible_count==18&&ctx.leaf==0);
 ctx.leaf=1;shelf_category_remember(&ctx);book_shelf_categories_select(&s_shelf_categories,0);sort_shelf(&ctx);shelf_category_restore(&ctx);assert(ctx.leaf==2);
 book_shelf_categories_select(&s_shelf_categories,literature);sort_shelf(&ctx);shelf_category_restore(&ctx);assert(ctx.leaf==1);
 style=5;shelf_category_restore(&ctx);assert(!ctx.leaf);ctx.leaf=3;shelf_category_remember(&ctx);style=0;shelf_category_restore(&ctx);assert(ctx.leaf==1);
 // 扫描或离开书架时不覆盖之前的页码。/ Pending scans and non-shelf views must not overwrite remembered pages.
 s_scan_pending=true;ctx.leaf=0;shelf_category_remember(&ctx);s_scan_pending=false;shelf_category_restore(&ctx);assert(ctx.leaf==1);
 s_view=1;ctx.leaf=0;shelf_category_remember(&ctx);s_view=SHELF;shelf_category_restore(&ctx);assert(ctx.leaf==1);
 book_shelf_categories_select(&s_shelf_categories,empty);sort_shelf(&ctx);shelf_category_restore(&ctx);assert(!s_visible_count&&!ctx.leaf);
 book_shelf_categories_select(&s_shelf_categories,1);sort_shelf(&ctx);assert(s_visible_count==6);
 s_filter=2;sort_shelf(&ctx);assert(!s_visible_count);s_filter=0;
 book_shelf_categories_select(&s_shelf_categories,literature);sort_shelf(&ctx);s_shelf[0].search_match=false;sort_shelf(&ctx);assert(s_visible_count==17);
 // 消失的空目录回退全部；重新登记保留索引和两套页码。/ A missing empty folder falls back to all; re-registration retains its index and layout pages.
 book_shelf_categories_select(&s_shelf_categories,empty);book_shelf_categories_remember(&s_shelf_categories,false,4);
 book_shelf_categories_begin_scan(&s_shelf_categories);shelf_categories_recount();assert(book_shelf_categories_selected(&s_shelf_categories)==0);
 assert(!book_shelf_categories_select(&s_shelf_categories,empty));
 assert(shelf_category_add("/sdcard/books/空目录")==empty);assert(book_shelf_categories_select(&s_shelf_categories,empty));
 assert(book_shelf_categories_saved_page(&s_shelf_categories,false)==4);
 // 书籍数缩减后裁剪，并将裁剪结果记住。/ Shrinking counts clamp and store the resulting page.
 assert(book_shelf_categories_restore(&s_shelf_categories,false,10,9)==1);
 assert(book_shelf_categories_saved_page(&s_shelf_categories,false)==1);
 assert(book_shelf_categories_restore(&s_shelf_categories,true,0,4)==0);
 // 两个上下文的目录、菜单、查询、页码互不串用。/ Two contexts isolate folders, menus, queries and page memory.
 book_shelf_categories_t other={0};assert(book_shelf_categories_init(&other,"/other"));
 assert(book_shelf_categories_add(&other,"/other/folder")==2);assert(book_shelf_categories_select(&other,2));
 book_shelf_categories_remember(&other,false,8);assert(book_shelf_categories_menu_open(&other));
 assert(!book_shelf_categories_menu_is_open(&s_shelf_categories));
 assert(!strcmp(book_shelf_categories_query(&other,2,true).category_path,"/other/folder"));
 assert(book_shelf_categories_selected(&s_shelf_categories)==empty);
 book_shelf_categories_dispose(&other);book_shelf_categories_dispose(&other);
 assert(book_shelf_categories_available(&other)==0&&!book_shelf_categories_menu_is_open(&other));
 assert(book_shelf_categories_saved_page(&s_shelf_categories,false)==1);
 // 在一次增长失败后重试；有界表满后重加旧目录仍有效。/ Retry a failed growth; at the table limit, existing paths still register.
 book_shelf_categories_t many={0};book_shelf_categories_init(&many,"/many");
 for(int i=0;i<14;++i){char path[288];snprintf(path,sizeof(path),"/many/f%d",i);assert(book_shelf_categories_add(&many,path)==i+2);}
 fail_alloc=true;assert(book_shelf_categories_add(&many,"/many/f14")==-1);assert(book_shelf_categories_available(&many)==16);
 fail_alloc=false;assert(book_shelf_categories_add(&many,"/many/f14")==16);
 for(int i=15;i<126;++i){char path[288];snprintf(path,sizeof(path),"/many/f%d",i);assert(book_shelf_categories_add(&many,path)==i+2);}
 assert(book_shelf_categories_available(&many)==128&&book_shelf_categories_add(&many,"/overflow")==-1);
 assert(book_shelf_categories_add(&many,"/many/f0")==2);
 // 菜单定位已选项、跳过不可用目录，末页/首页越界不改变页码。/ Menus locate selection, skip unavailable folders, and reject first/last-page overflow.
 assert(book_shelf_categories_select(&many,127));assert(book_shelf_categories_menu_open(&many));
 assert(book_shelf_categories_menu_page(&many)==18);assert(!book_shelf_categories_menu_move(&many,1));
 for(int page=18;page>0;--page)assert(book_shelf_categories_menu_move(&many,-1));
 assert(!book_shelf_categories_menu_move(&many,-1)&&book_shelf_categories_menu_page(&many)==0);
 assert(book_shelf_categories_at(&many,127)==127&&book_shelf_categories_at(&many,128)==-1&&book_shelf_categories_at(&many,-1)==-1);
 book_shelf_categories_menu_close(&many);assert(!book_shelf_categories_menu_is_open(&many));
 book_shelf_categories_begin_scan(&many);assert(book_shelf_categories_add(&many,"/many/f125")==127);
 assert(book_shelf_categories_available(&many)==3&&book_shelf_categories_at(&many,2)==127);
 book_shelf_categories_menu_open(&many);assert(book_shelf_categories_menu_page(&many)==0);
 assert(book_shelf_categories_select(&many,127)&&!book_shelf_categories_menu_is_open(&many));
 book_shelf_categories_dispose(&many);

 // 页面真实手势保持：选类先记旧页，清选择，排序后恢复新页。/ Actual page gestures retain remember, clear-selection, sort and restore order.
 book_shelf_categories_select(&s_shelf_categories,0);ctx.leaf=2;
 book_shelf_categories_remember(&s_shelf_categories,false,2);
 book_shelf_categories_select(&s_shelf_categories,literature);
 book_shelf_categories_remember(&s_shelf_categories,false,1);
 book_shelf_categories_select(&s_shelf_categories,0);sort_shelf(&ctx);ctx.leaf=2;
 ui_gesture_event_t event={UI_GESTURE_TAP,100,110};
 int paints=render_count;
 assert(shelf_category_gesture(&ctx,&event)==APP_REDRAW_AREA&&render_count==paints+1);
 assert(book_shelf_categories_menu_is_open(&s_shelf_categories)&&s_mode==MODE_GL16);
 EpdRect box=shelf_category_box();assert(box.x==36&&box.y==158&&box.width==400&&box.height==476);
 assert(s_area.x==24&&s_area.y==79&&s_area.width==424&&s_area.height==559);
 event.type=UI_GESTURE_SWIPE_D;paints=render_count;
 assert(shelf_category_gesture(&ctx,&event)==APP_REDRAW_NONE&&render_count==paints);
 event.type=UI_GESTURE_SWIPE_L;assert(shelf_category_gesture(&ctx,&event)==APP_REDRAW_NONE);
 s_shelf[0].selected=s_shelf[23].selected=true;
 event=(ui_gesture_event_t){UI_GESTURE_TAP,100,158+8+2*83+40};
 unsigned resets=invalidations;
 assert(shelf_category_gesture(&ctx,&event)==APP_REDRAW_PAGE&&invalidations==resets+1);
 assert(book_shelf_categories_selected(&s_shelf_categories)==literature&&ctx.leaf==1&&s_visible_count==17);
 assert(!book_shelf_categories_menu_is_open(&s_shelf_categories)&&clear_count==1);
 for(int i=0;i<s_count;++i)assert(!s_shelf[i].selected);
 assert(book_shelf_categories_select(&s_shelf_categories,0));
 assert(book_shelf_categories_saved_page(&s_shelf_categories,false)==2);
 event=(ui_gesture_event_t){UI_GESTURE_TAP,100,110};shelf_category_gesture(&ctx,&event);
 event=(ui_gesture_event_t){UI_GESTURE_TAP,180,1150};paints=render_count;
 assert(shelf_category_gesture(&ctx,&event)==APP_REDRAW_NONE&&ctx.requested_tab==1&&render_count==paints);
 assert(!book_shelf_categories_menu_is_open(&s_shelf_categories));
 event=(ui_gesture_event_t){UI_GESTURE_TAP,100,110};shelf_category_gesture(&ctx,&event);
 event.x0=600;event.y0=300;
 assert(shelf_category_gesture(&ctx,&event)==APP_REDRAW_AREA&&!book_shelf_categories_menu_is_open(&s_shelf_categories));
 // 翻到第二页后点选分类；触摸上下边界和菜单末页不误选。/ Select on the second menu page; taps at padding and page limits do not change categories.
 for(int i=0;i<10;++i){char path[288];snprintf(path,sizeof(path),"/sdcard/books/menu%d",i);shelf_category_add(path);}
 event=(ui_gesture_event_t){UI_GESTURE_TAP,100,110};shelf_category_gesture(&ctx,&event);
 event=(ui_gesture_event_t){UI_GESTURE_SWIPE_U,100,300};
 assert(shelf_category_gesture(&ctx,&event)==APP_REDRAW_AREA&&book_shelf_categories_menu_page(&s_shelf_categories)==1);
 assert(shelf_category_gesture(&ctx,&event)==APP_REDRAW_NONE);
 box=shelf_category_box();event=(ui_gesture_event_t){UI_GESTURE_TAP,100,box.y+box.height-128-25};
 assert(shelf_category_gesture(&ctx,&event)==APP_REDRAW_AREA&&book_shelf_categories_menu_page(&s_shelf_categories)==0);
 event=(ui_gesture_event_t){UI_GESTURE_TAP,350,shelf_category_box().y+shelf_category_box().height-128-25};
 assert(shelf_category_gesture(&ctx,&event)==APP_REDRAW_AREA&&book_shelf_categories_menu_page(&s_shelf_categories)==1);
 event=(ui_gesture_event_t){UI_GESTURE_TAP,100,158+8+40};
 assert(shelf_category_gesture(&ctx,&event)==APP_REDRAW_PAGE&&book_shelf_categories_selected(&s_shelf_categories)==7);
 assert(!ctx.leaf&&!s_visible_count);
 event=(ui_gesture_event_t){UI_GESTURE_TAP,100,110};shelf_category_gesture(&ctx,&event);
 box=shelf_category_box();event=(ui_gesture_event_t){UI_GESTURE_TAP,200,box.y+box.height-20};
 assert(shelf_category_gesture(&ctx,&event)==APP_REDRAW_PAGE&&manager_entries==1);
 assert(!book_shelf_categories_menu_is_open(&s_shelf_categories));
 // 改名保持页码，删除释放槽位并回到未分类。/ Renames retain pages; removals release slots and select uncategorized.
 book_shelf_categories_select(&s_shelf_categories,literature);
 book_shelf_categories_remember(&s_shelf_categories,false,3);
 book_shelf_categories_rebase(&s_shelf_categories,"/sdcard/books/文学","/sdcard/books/小说");
 assert(!strcmp(book_shelf_categories_path(&s_shelf_categories,literature),"/sdcard/books/小说"));
 assert(book_shelf_categories_saved_page(&s_shelf_categories,false)==3);
 book_shelf_categories_rebase(&s_shelf_categories,"/sdcard/books/小说",NULL);
 assert(book_shelf_categories_selected(&s_shelf_categories)==1);
 assert(shelf_category_add("/sdcard/books/新分类")==literature);

 // 根目录变化先释放旧记录，分配失败时仍按新根查询并允许再次初始化。/ Root changes release old records; allocation failure still exposes the new root and permits retry.
 books_root="/sdcard/newbooks";fail_alloc=true;shelf_category_init();assert(!s_shelf_cache_valid);
 assert(book_shelf_categories_available(&s_shelf_categories)==0&&book_shelf_categories_selected(&s_shelf_categories)==0);
 assert(!strcmp(shelf_query().root,books_root));fail_alloc=false;shelf_category_init();
 assert(book_shelf_categories_available(&s_shelf_categories)==2);
 assert(book_shelf_categories_saved_page(&s_shelf_categories,false)==0&&book_shelf_categories_saved_page(&s_shelf_categories,true)==0);
 // 沿用页面原 UTF8 截断边界，不保留半个汉字。/ Retain the page's UTF8 truncation boundary without partial characters.
 book_shelf_categories_t unicode={0};char long_path[300];
 memset(long_path,'a',286);memcpy(long_path+286,"汉字",7);
 book_shelf_categories_init(&unicode,long_path);
 assert(strlen(book_shelf_categories_query(&unicode,0,false).root)==286);
 assert(book_shelf_categories_add(&unicode,long_path)==2);
 assert(strlen(book_shelf_categories_name(&unicode,2))==286);
 book_shelf_categories_dispose(&unicode);
 // 逻辑绑定保留首次旧目录选择和两套页码，后续改名/排序只认稳定ID。/ Logical binding preserves migrated selection/pages, then follows stable IDs through rename/order.
 char prefix[288];snprintf(prefix,sizeof(prefix),"%s/library",argv[1]);
 book_collection_t library={0};assert(book_collection_open(&library,prefix)==ESP_OK);
 book_shelf_categories_t logical={0};book_shelf_categories_init(&logical,"/library");
 int old=book_shelf_categories_add(&logical,"/library/文学");
 const char *folders[]={"/library/文学","/library/未分类"};
 int reserved=book_shelf_categories_add(&logical,folders[1]);
 assert(book_shelf_categories_select(&logical,old));book_shelf_categories_remember(&logical,false,3);book_shelf_categories_remember(&logical,true,6);
 book_shelf_categories_set_sort(&logical,true);
 book_shelf_entry_t members[3]={0};
 snprintf(members[0].path,288,"/library/文学/a.txt");snprintf(members[1].path,288,"/library/root.txt");snprintf(members[2].path,288,"/library/未分类/b.txt");
 for(int i=0;i<3;++i)members[i].search_match=true;
 assert(book_collection_sync(&library,"/library",folders,2,members,3,true)==ESP_OK);
 assert(book_shelf_categories_bind(&logical,&library)&&book_shelf_categories_logical(&logical));
 uint32_t stable=members[0].category_id;
 assert(book_shelf_categories_id(&logical,book_shelf_categories_selected(&logical))==stable);
 assert(book_shelf_categories_saved_page(&logical,false)==3&&book_shelf_categories_saved_page(&logical,true)==6);
 assert(book_shelf_categories_query(&logical,0,false).recent_sort);
 assert(book_shelf_categories_select(&logical,0));book_shelf_categories_set_sort(&logical,false);
 assert(!book_shelf_categories_query(&logical,0,true).recent_sort);
 assert(book_shelf_categories_select(&logical,book_shelf_categories_index(&logical,stable))&&book_shelf_categories_query(&logical,0,false).recent_sort);
 book_shelf_categories_recount(&logical,members,3);assert(book_shelf_categories_count(&logical,0)==3&&book_shelf_categories_count(&logical,1)==1);
 assert(book_shelf_categories_count(&logical,book_shelf_categories_index(&logical,stable))==1);
 book_shelf_categories_t alias={0};book_shelf_categories_init(&alias,"/library");book_shelf_categories_add(&alias,folders[0]);
 assert(book_shelf_categories_add(&alias,folders[1])==reserved&&book_shelf_categories_select(&alias,reserved));
 book_shelf_categories_remember(&alias,false,9);assert(book_shelf_categories_bind(&alias,&library));
 assert(book_shelf_categories_id(&alias,book_shelf_categories_selected(&alias))==members[2].category_id&&book_shelf_categories_saved_page(&alias,false)==9);
 book_shelf_categories_dispose(&alias);
 assert(book_collection_rename(&library,stable,"改名后") == ESP_OK&&book_collection_order(&library,stable,1)==ESP_OK);
 fail_alloc=true;assert(!book_shelf_categories_bind(&logical,&library));fail_alloc=false;
 assert(book_shelf_categories_id(&logical,book_shelf_categories_selected(&logical))==stable&&!strcmp(book_shelf_categories_name(&logical,2),"文学"));
 assert(book_shelf_categories_bind(&logical,&library));
 assert(book_shelf_categories_id(&logical,book_shelf_categories_selected(&logical))==stable&&book_shelf_categories_selected(&logical)==3);
 assert(!strcmp(book_shelf_categories_name(&logical,3),"改名后")&&book_shelf_categories_saved_page(&logical,true)==6);
 assert(book_shelf_categories_query(&logical,0,false).recent_sort);
 book_shelf_query_t logical_query=book_shelf_categories_query(&logical,0,false);assert(logical_query.logical_categories&&logical_query.category_id==stable);
 members[0].removed=true;book_shelf_categories_recount(&logical,members,3);assert(book_shelf_categories_count(&logical,0)==2&&book_shelf_categories_count(&logical,3)==0);
 assert(book_collection_remove(&library,stable)==ESP_OK&&book_shelf_categories_bind(&logical,&library));
 assert(book_shelf_categories_selected(&logical)==1);
 book_shelf_categories_dispose(&logical);book_collection_close(&library);
 for(int slot=0;slot<2;++slot){char path[300];snprintf(path,sizeof(path),"%s.%d",prefix,slot);unlink(path);}
 book_shelf_categories_dispose(&s_shelf_categories);
 book_shelf_categories_dispose(&s_legacy_categories);
 puts("PASS: real category module and page adapters; folders, pages, filters, OOM/retry/limits, menu navigation, independent contexts and teardown");
}
'''
with tempfile.TemporaryDirectory() as directory:
    c = Path(directory) / 'shelf.c'
    binary = c.with_suffix('')
    c.write_text(unit)
    subprocess.run(['cc', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I' + str(root / 'tools/shelf_categories_host_stubs'), '-I' + str(root / 'main/book'),
                    '-I' + str(root / 'tools/book_storage_stubs'),
                    str(root / 'main/book/book_shelf_model.c'), str(root / 'main/book/book_shelf_category_state.c'),
                    str(root / 'main/book/book_collection.c'),
                    str(c), '-o', str(binary)], check=True)
    subprocess.run([str(binary), directory], check=True)
