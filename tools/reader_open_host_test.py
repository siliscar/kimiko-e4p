#!/usr/bin/env python3
"""中文：执行真实开书适配、进度队列及会话，验证交还顺序和恢复失败边界。
English: Run actual open adapters, pending progress and sessions to verify handoff order and resume failure boundaries.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def extract(source, name):
    match = re.search(r'^static [^\n]+\b' + name + r'\([^;{}]*\)\s*\{', source, re.M)
    assert match, name
    cleaned = re.sub(r'/\*.*?\*/|//[^\n]*', lambda m: ' ' * len(m[0]), source, flags=re.S)
    at, depth, quote, escaped = match.end(), 1, None, False
    while depth:
        char = cleaned[at]
        if quote:
            if escaped:
                escaped = False
            elif char == '\\':
                escaped = True
            elif char == quote:
                quote = None
        elif char in ('"', "'"):
            quote = char
        elif char == '{':
            depth += 1
        elif char == '}':
            depth -= 1
        at += 1
    return source[match.start():at] + '\n'


HARNESS = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "reader_session.h"
#include "book_source.h"
#include "book_layout.h"
#include "book_progress.h"
#include "book_font_size.h"
#define CONFIG_READER_PERF 0
#define CONFIG_READER_MEMORY 0
#define TAG "test"
#define BOOK_PX_MIN BOOK_READER_PX_MIN
#define BOOK_PX_MAX BOOK_READER_PX_MAX
#define BOOK_STORE_FLASH_FILE_MAX 1048576
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
typedef struct {unsigned now_ms;} app_ctx_t;
typedef struct {bool present,mounted;} board_sd_info_t;
typedef struct {bool network_ready;} wifi_transfer_status_t;
typedef struct pending_progress {
    char path[BOOK_STORE_PATH_MAX]; book_progress_t value;
    bool dirty,progress_saved; book_progress_watch_t *watch; struct pending_progress *next;
} pending_progress_t;
struct book_progress_watch {int unused;};
typedef struct {int unused;} reader_dims_ctx_t;
enum {SHELF,READING,READER_PANEL_NONE=0};
static pending_progress_t *s_pending;
static reader_session_t s_reader_session;
static char s_message[256],s_latest_path[BOOK_STORE_PATH_MAX],s_reader_notice[128];
static char s_chapter_heading_title[128],s_chapter_heading_label[32];
static bool s_reader_return_home,s_reader_fullscreen,s_requested_fullscreen,s_book_fonts_applied;
static bool s_shelf_key_mode,s_shelf_key_paint_pending,s_save_failed;
static bool s_water_turn_pending,s_reader_turn_pending,s_reader_text_frame,s_reader_stable_frame;
static bool s_reader_entry_full,s_reader_image_refresh_pending,s_clear_confirm,s_batch_confirm;
static int s_shelf_key_delta,s_shelf_key_pressed,s_px,s_view,s_reader_panel;
static unsigned s_stats_last_ms,s_stats_activity_ms,s_session_read_ms,s_session_turns,s_turns,s_unsaved;
static size_t s_selected_toc,s_jump_offset,s_chapter_lead_skip,s_chapter_lead_height,s_chapter_lead_width;
static reader_dims_ctx_t s_reader_dims_ctx;
static const char *s_text;
static void *s_page_images;
static bool supported=true,sd_present=true,sd_mounted=true,deleted,unreadable,regular=true;
static bool network_ready,embedded=true,hide_images,hidden,save_fails,load_fails,skip_loses;
static bool fail_alloc,fail_watch,locked,prefetch_borrowed;
static int global_px=59,fail_reserve,source_error;
static off_t file_size=2000;
static unsigned reserve_calls,persist_calls,source_opens,source_closes,watch_count;
static size_t chapters=12,loaded_chapter,loaded_offset;
static book_progress_t persisted;
static bool has_persisted;
static char events[2048];
static void event(char c){size_t n=strlen(events);assert(n+1<sizeof(events));events[n]=c;events[n+1]=0;}
static void log_event(const char *tag,const char *format,...){(void)tag;(void)format;}
#define ESP_LOGI(...) log_event(__VA_ARGS__)
#define ESP_LOGW(...) log_event(__VA_ARGS__)
static const char *esp_err_to_name(int err){(void)err;return "test";}
static bool book_file_path_supported(const char *path){(void)path;return supported;}
static void board_sd_get_info(board_sd_info_t *sd){*sd=(board_sd_info_t){sd_present,sd_mounted};}
static bool delete_retry_find(const char *path){(void)path;return deleted;}
static int test_stat(const char *path,struct stat *st){(void)path;memset(st,0,sizeof(*st));st->st_mode=regular?S_IFREG:S_IFDIR;st->st_size=file_size;return unreadable?-1:0;}
#define stat(path,st) test_stat(path,st)
static void wifi_transfer_get_status(wifi_transfer_status_t *s){s->network_ready=network_ready;}
static void invalidate_covers(void){event('c');}
static void lock_draw(void){assert(!locked);locked=true;event('[');}
static void unlock_draw(void){assert(locked);locked=false;event(']');}
static void reader_prefetch_discard(void){assert(locked);prefetch_borrowed=false;event('p');}
static void ttf_font_cache_clear(void){assert(!prefetch_borrowed);event('w');}
static size_t ttf_font_cache_clear_all(void){assert(!prefetch_borrowed);event('g');return 4096;}
static size_t heap_caps_get_free_size(int caps){(void)caps;return 10000;}
static size_t heap_caps_get_largest_free_block(int caps){(void)caps;return 8000;}
static void *test_malloc(size_t n){return fail_alloc?NULL:malloc(n);}
static void *heap_caps_malloc(size_t n,int caps){(void)caps;return test_malloc(n);}
book_progress_watch_t *book_progress_watch_create(const char *path){(void)path;if(fail_watch)return NULL;++watch_count;return calloc(1,sizeof(book_progress_watch_t));}
void book_progress_watch_destroy(book_progress_watch_t *w){assert(w&&watch_count);--watch_count;free(w);}
static void app_font_activate_system(void){event('Y');}
static void app_font_activate_reading(void){assert(s_reader_fullscreen==s_requested_fullscreen);event('A');}
static bool app_settings_book_fonts(void){return embedded;}
int app_settings_book_px(void){event('X');return global_px;}
static bool app_settings_reader_hide_images(void){return hide_images;}
void book_set_embedded_fonts(bool on){assert(reader_session_has_source(&s_reader_session)&&on==embedded);event('E');}
static bool book_title_from_path(const char *path,char *out,size_t cap){(void)path;event('T');snprintf(out,cap,"indexed title");return false;}
esp_err_t book_open(const char *path){(void)path;assert(!prefetch_borrowed);++source_opens;event('O');return source_error;}
void book_close(void){++source_closes;event('C');}
book_kind_t book_kind(void){return BOOK_KIND_EPUB;}
size_t book_chapter_count(void){return chapters;}
size_t book_layout_page_count(void){return 9;}
int book_layout_page_image_count(size_t page){(void)page;return 1;}
void book_layout_set_image_dims(book_layout_image_dims_fn fn,void *ctx){(void)fn;(void)ctx;}
void book_layout_free(void){}
void book_layout_set_runs(const html_run_t *r,size_t n){(void)r;(void)n;}
void book_layout_set_links(const html_link_t *r,size_t n){(void)r;(void)n;}
void book_layout_set_chapter_lead(size_t skip,unsigned height){(void)skip;(void)height;}
void book_layout_set_chapter_lead_width(unsigned width){(void)width;}
bool book_layout_build_blocks(const char *t,size_t len,const blk_t *b,size_t n,EpdRect r,int px){(void)t;(void)len;(void)b;(void)n;(void)r;(void)px;return true;}
void html_text_free(html_text_t *t){free(t->utf8);*t=(html_text_t){0};}
static void flush_ticket_stats(void){event('F');}
static bool app_reading_journal_flush(void){return true;}
static unsigned journal_records;
static void app_reading_journal_record(const char *path,const char *title,uint32_t size,unsigned chapter,uint32_t ms,uint32_t turns){
    assert(path[0]&&title[0]&&size==(uint32_t)file_size&&chapter==s_reader_session.position.chapter&&!ms&&!turns);
    assert(!strcmp(path,s_reader_session.book.path)&&!strcmp(title,s_reader_session.book.title));++journal_records;
}
static void e0470_page_turn_release(void){}
static void invalidate_prep(void){assert(!prefetch_borrowed);}
static void reader_bind_chapter(void){s_text=s_reader_session.content.utf8;}
static void release_page_images(void){s_page_images=NULL;}
bool book_progress_load(const char *path,uint32_t size,book_progress_t *out){(void)path;++persist_calls;event('P');if(!has_persisted||persisted.file_size!=size)return false;*out=persisted;return true;}
static bool load_chapter(app_ctx_t *ctx,size_t chapter,size_t offset,bool end){(void)ctx;assert(!end);event('L');loaded_chapter=chapter;loaded_offset=offset;if(load_fails)return false;s_reader_session.content.utf8=malloc(8);strcpy(s_reader_session.content.utf8,"text");reader_bind_chapter();s_reader_session.position.chapter=chapter;s_reader_session.position.page=2;s_page_images=(void*)1;return true;}
static void skip_hidden_image_pages(app_ctx_t *ctx,int direction){(void)ctx;assert(direction==1);event('H');if(skip_loses){reader_session_release_chapter(&s_reader_session);reader_bind_chapter();}}
static bool shelf_hidden_load(const char *path){assert(!strcmp(path,s_reader_session.book.path));event('B');return hidden;}
static bool shelf_hidden_save(const char *path,bool hide){(void)path;assert(!hide);hidden=false;event('b');return true;}
static void copy_text(char *dst,size_t cap,const char *src);
static pending_progress_t *pending_find(const char *path);
static void pending_discard(const char *path);
static bool pending_reserve(const char *path);
static void free_book(void);
static void save_progress(void){event('S');if(!*s_reader_session.book.path)return;pending_progress_t *p=pending_find(s_reader_session.book.path);assert(p);if(save_fails){p->dirty=true;p->value=(book_progress_t){.file_size=s_reader_session.book.file_size,.chapter=4,.byte_off=321,.px=20};}else p->dirty=false;}
'''

CASES = r'''
static bool open_book_impl(app_ctx_t *ctx,const char *path){
    unsigned before=journal_records;bool opened=open_book_impl_actual(ctx,path);
    assert(journal_records-before==(unsigned)opened);return opened;
}
static bool pending_reserve(const char *path){++reserve_calls;event('R');fail_alloc=fail_reserve==(int)reserve_calls;bool ok=pending_reserve_actual(path);fail_alloc=false;return ok;}
static void reset(void){
    prefetch_borrowed=false;free_book();while(s_pending)pending_discard(s_pending->path);
    assert(!watch_count&&!reader_session_has_source(&s_reader_session));
    s_reader_session=(reader_session_t){0};s_text=NULL;s_message[0]=s_latest_path[0]=0;
    supported=sd_present=sd_mounted=regular=true;deleted=unreadable=network_ready=false;
    embedded=true;hide_images=hidden=save_fails=load_fails=skip_loses=fail_watch=false;
    global_px=59;fail_reserve=source_error=0;file_size=2000;chapters=12;
    reserve_calls=persist_calls=source_opens=source_closes=0;has_persisted=false;
    s_view=SHELF;s_reader_return_home=true;s_requested_fullscreen=true;
    events[0]=0;prefetch_borrowed=true;
}
static void report(const char *name){printf("%s:%s:%u/%u/%u:%zu/%zu:%d:%s\n",name,events,reserve_calls,persist_calls,watch_count,loaded_chapter,loaded_offset,s_px,s_message);}
static void seed(const char *path,bool dirty,uint32_t size){
    assert(pending_reserve(path));pending_progress_t *p=pending_find(path);p->dirty=dirty;
    p->value=(book_progress_t){.file_size=size,.chapter=4,.byte_off=321,.px=20};
    reserve_calls=0;events[0]=0;
}
int main(void){
    app_ctx_t ctx={.now_ms=12345};const char *a="/sdcard/  测试书 \t.epub",*b="/flash/other.txt";
    for(int preflight=0;preflight<9;++preflight){
        reset();switch(preflight){case 0:supported=false;break;case 1:sd_present=false;break;case 2:sd_mounted=false;break;case 3:deleted=true;break;case 4:unreadable=true;break;case 5:regular=false;break;case 6:file_size=-1;break;case 7:file_size=(off_t)UINT32_MAX+1;break;case 8:file_size=BOOK_STORE_FLASH_FILE_MAX+1;break;}
        assert(!open_book_impl(&ctx,preflight==8?b:a));assert(!reserve_calls&&!source_opens&&!*events);
        assert(s_reader_return_home==(preflight==0));report("preflight");
    }
    reset();assert(open_book_impl(&ctx,a));assert(!strcmp(s_reader_session.book.title,"测试书"));
    assert(loaded_chapter==0&&loaded_offset==0&&s_px==59&&s_view==READING);
    assert(s_reader_entry_full&&s_reader_image_refresh_pending&&s_stats_last_ms==ctx.now_ms);
    assert(!strcmp(events,"c[p]gRSFYAROETPXLHSB"));report("first");
    // 同书重开会交还干净预留；第二次预留不能合并。/ Reopening drops a clean reservation, so the second reserve cannot be merged.
    reserve_calls=0;fail_reserve=2;events[0]=0;assert(!open_book_impl(&ctx,a));
    assert(!reader_session_has_source(&s_reader_session)&&!pending_find(a)&&reserve_calls==2);
    assert(strstr(s_message,"无法打开图书"));report("second-reserve");
    reset();assert(open_book_impl(&ctx,a));reserve_calls=0;source_opens=0;fail_reserve=1;events[0]=0;
    assert(!open_book_impl(&ctx,b));assert(reader_session_has_source(&s_reader_session)&&!strcmp(s_reader_session.book.path,a));
    assert(!source_opens&&strstr(s_message,"保留待保存进度"));report("first-reserve");
    reset();fail_watch=true;assert(!open_book_impl(&ctx,a));assert(!watch_count&&!source_opens);report("watch-oom");
    for(int error=0;error<4;++error)for(int dirty=0;dirty<2;++dirty){
        reset();seed(a,dirty,2000);source_error=(int[]){ESP_ERR_NOT_SUPPORTED,ESP_ERR_NO_MEM,ESP_ERR_INVALID_SIZE,ESP_FAIL}[error];
        assert(!open_book_impl(&ctx,a));assert(!reader_session_has_source(&s_reader_session));
        assert((pending_find(a)!=NULL)==(bool)dirty&&watch_count==(unsigned)dirty);
        assert(!strchr(events,'E')&&!strchr(events,'T')&&!strchr(events,'L')&&events[strlen(events)-1]=='Y');report("source-error");
    }
    for(int resume=0;resume<7;++resume){
        reset();has_persisted=true;persisted=(book_progress_t){.file_size=2000,.chapter=6,.byte_off=777,.px=90};
        if(resume==0)seed(a,true,2000);
        if(resume==1)seed(a,false,2000);
        if(resume==2)seed(a,true,1999);
        if(resume==3)persisted.file_size=1999;
        if(resume==4)persisted.chapter=12;
        if(resume==5)persisted.chapter=0;
        if(resume==6){persisted.chapter=0;chapters=0;}
        assert(open_book_impl(&ctx,a));assert(s_px==59);
        assert(persist_calls==(resume==0?0u:1u));
        assert(loaded_chapter==(resume==0?4u:resume==3||resume>=4?0u:6u));
        assert(loaded_offset==(resume==0?321u:resume==3||resume==4?0u:777u));report("resume");
    }
    for(int px=0;px<5;++px){reset();global_px=(int[]){17,18,90,91,48}[px];assert(open_book_impl(&ctx,a));assert(s_px==(px==0||px==3?48:global_px));report("size");}
    reset();assert(open_book_impl(&ctx,a));save_fails=true;network_ready=true;embedded=false;hidden=true;
    reserve_calls=0;events[0]=0;assert(open_book_impl(&ctx,b));
    assert(pending_find(a)&&pending_find(a)->dirty&&pending_find(b)->dirty&&!hidden&&!s_book_fonts_applied);
    assert(!strcmp(s_reader_session.book.path,b)&&source_closes==1&&strstr(events,"]wgRSF"));report("switch-dirty");
    for(int failure=0;failure<2;++failure){reset();load_fails=failure==0;skip_loses=failure==1;
        assert(!open_book_impl(&ctx,a));assert(!reader_session_has_source(&s_reader_session)&&!s_text&&!watch_count);
        assert(source_closes==1&&s_view==SHELF);report("chapter-error");free_book();free_book();assert(source_closes==1);
    }
    for(int cycle=0;cycle<100;++cycle){reset();hide_images=true;assert(open_book_impl(&ctx,a));assert(!s_reader_image_refresh_pending);free_book();free_book();assert(!s_text&&!watch_count&&!reader_session_has_source(&s_reader_session));}
    reset();prefetch_borrowed=false;puts("PASS: open ordering, preflight, both reservations, source failures, pending priority, size/chapter/offset recovery, font size, failed load/skip and 100 repeated exits");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'main/apps/app_book.c')
    parser.add_argument('--controller', type=Path, default=ROOT / 'main/book/reader_open.c')
    args = parser.parse_args()
    source = args.source.read_text()
    functions = ['copy_text', 'clean_filename', 'pending_find', 'pending_reserve',
                 'pending_restore', 'pending_discard', 'pending_mark_latest', 'free_book']
    functions += [name for name in ('reader_open_replace', 'reader_open_describe', 'reader_open_load')
                  if re.search(r'^static [^\n]+\b' + name + r'\(', source, re.M)]
    functions.append('open_book_impl')
    code = HARNESS
    if '#include "reader_open.h"' in source:
        code += '\n#include "reader_open.h"\n'
    for name in functions:
        body = extract(source, name)
        if name == 'pending_reserve':
            body = body.replace('pending_reserve(', 'pending_reserve_actual(', 1)
            body = '#define malloc test_malloc\n' + body + '#undef malloc\n'
        if name == 'open_book_impl':
            body = '#define open_book_impl open_book_impl_actual\n' + body + '#undef open_book_impl\n'
        code += body
    code += CASES
    with tempfile.TemporaryDirectory() as directory:
        folder = Path(directory)
        test, binary = folder / 'open.c', folder / 'open'
        test.write_text(code)
        (folder / 'settings.h').write_text('#pragma once\nint app_settings_book_px(void);\n')
        flags = ['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                 '-I' + str(folder), '-I' + str(ROOT / 'main/book'),
                 '-I' + str(ROOT / 'tools/book_source_host_stubs'),
                 '-I' + str(ROOT / 'tools/book_layout_stubs')]
        sources = [str(ROOT / 'main/book/reader_session.c'), str(test)]
        if '#include "reader_open.h"' in source:
            sources.append(str(args.controller))
        subprocess.run(flags + sources + ['-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
