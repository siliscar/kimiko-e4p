#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 chopper1026
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：实际正文计时门槛、空闲保存及锁屏收尾。/ English: Regress actual body timing, idle persistence and pre-lock flush."""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
BOOK = ROOT / "main/apps/app_book.c"
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define ESP_OK 0
enum {READING,SHELF,CATEGORIES,SEARCH,READER_PANEL_NONE,READER_PANEL_TOOLS,READER_PANEL_SETTINGS};
typedef struct {int64_t now_ms;} app_ctx_t;
static int s_view=READING,s_reader_panel=READER_PANEL_NONE;
#define s_toolbar (s_reader_panel!=READER_PANEL_NONE)
static const char *s_text="text";
static int64_t s_stats_last_ms,s_stats_activity_ms;
static uint32_t s_stats_pending_ms,s_session_read_ms,s_stats_pending_turns;
static struct {struct {char path[128],title[128];uint32_t file_size;}book;struct {unsigned chapter;}position;}s_reader_session={.book={"/book.epub","Book",500},.position={3}};
static uint32_t journal_ms,ticket_seconds,ticket_turns;
static unsigned records,polls,flushes,latest;
static bool ticket_ok=true;
static int book_ticket_record(uint32_t seconds,uint32_t turns){if(!ticket_ok)return 1;ticket_seconds+=seconds;ticket_turns+=turns;return 0;}
static void app_reading_journal_record(const char *path,const char *title,uint32_t size,unsigned chapter,uint32_t ms,uint32_t turns){
    assert(!strcmp(path,"/book.epub")&&!strcmp(title,"Book")&&size==500&&chapter==3&&!turns);journal_ms+=ms;++records;
}
static bool app_reading_journal_poll(void){++polls;return false;}
static bool app_reading_journal_flush(void){++flushes;return true;}
static void middle_pending_clear(void){}
static void reader_indent_hold_cancel(void){}
static int s_category_ui;
static unsigned category_suspends;
static void book_category_ui_suspend(int *ui){assert(ui==&s_category_ui);++category_suspends;}
static int s_search_parent;
static bool s_shelf_selecting;
static unsigned keyboard_ends,category_resets,selections_cleared,shelf_sorts,shelf_restores;
static void ui_keyboard_end(void){++keyboard_ends;}
static void category_reset(void){++category_resets;}
static void clear_selection(void){++selections_cleared;}
static void sort_shelf(app_ctx_t *ctx){(void)ctx;++shelf_sorts;}
static void shelf_category_restore(app_ctx_t *ctx){(void)ctx;++shelf_restores;}
static bool s_shake_cfg_pending,s_shelf_key_mode,s_shelf_key_paint_pending;
static int s_shelf_key_delta,s_shelf_key_pressed,s_shelf_categories;
static void shelf_category_remember(app_ctx_t *ctx){(void)ctx;}
static void book_shelf_categories_menu_close(int *value){(void)value;}
static void lock_draw(void){}
static void invalidate_prep(void){}
static int s_orientation;
static void reader_orientation_cancel(int *state){(void)state;}
static bool on_system_overlay(app_ctx_t *ctx){(void)ctx;return false;}
static void unlock_draw(void){}
static void save_progress(void){}
static void book_progress_set_last_path(const char *p){assert(!strcmp(p,"/book.epub"));++latest;}
'''
unit += "\n".join(function(name, BOOK) for name in ("flush_ticket_stats", "track_ticket_stats", "before_lock"))
unit += r'''
int main(void){
    app_ctx_t ctx={.now_ms=2000};s_stats_last_ms=s_stats_activity_ms=1000;
    track_ticket_stats(&ctx);assert(journal_ms==1000&&records==1&&s_stats_pending_ms==1000&&s_session_read_ms==1000);
    for(int panel=READER_PANEL_TOOLS;panel<=READER_PANEL_SETTINGS;++panel){
        s_reader_panel=panel;ctx.now_ms+=1000;track_ticket_stats(&ctx);assert(journal_ms==1000&&polls==(unsigned)(panel-READER_PANEL_TOOLS+1));
    }
    s_reader_panel=READER_PANEL_NONE;s_stats_activity_ms=ctx.now_ms;
    ctx.now_ms+=1000;track_ticket_stats(&ctx);assert(journal_ms==2000);
    ctx.now_ms+=5001;track_ticket_stats(&ctx);assert(journal_ms==2000);
    ctx.now_ms+=300000;track_ticket_stats(&ctx);assert(journal_ms==2000);
    unsigned old_polls=polls;ctx.now_ms+=1000;track_ticket_stats(&ctx);assert(journal_ms==2000&&polls==old_polls+1);
    s_stats_last_ms=0;s_stats_activity_ms=ctx.now_ms;ctx.now_ms+=1000;track_ticket_stats(&ctx);assert(journal_ms==2000);
    ctx.now_ms+=1000;track_ticket_stats(&ctx);assert(journal_ms==3000);
    ctx.now_ms-=500;track_ticket_stats(&ctx);assert(journal_ms==3000);
    s_view=SHELF;ctx.now_ms+=1000;track_ticket_stats(&ctx);assert(journal_ms==3000);
    s_view=READING;s_text=NULL;ctx.now_ms+=1000;track_ticket_stats(&ctx);assert(journal_ms==3000);
    s_text="text";s_stats_activity_ms=ctx.now_ms;ctx.now_ms+=1000;
    before_lock(&ctx);assert(journal_ms==4000&&flushes==1&&latest==1&&!s_stats_last_ms);
    ctx.now_ms+=100000;track_ticket_stats(&ctx);assert(journal_ms==4000);
    ctx.now_ms+=1000;track_ticket_stats(&ctx);assert(journal_ms==5000);
    s_stats_pending_ms=29999;ctx.now_ms+=1000;track_ticket_stats(&ctx);assert(ticket_seconds>=30&&s_stats_pending_ms==999);
    ticket_ok=false;s_stats_pending_ms=30000;s_stats_pending_turns=2;flush_ticket_stats();assert(s_stats_pending_ms==30000&&s_stats_pending_turns==2);
    ticket_ok=true;flush_ticket_stats();assert(!s_stats_pending_ms&&!s_stats_pending_turns&&ticket_turns==2);
    uint32_t before_category=journal_ms;
    s_view=CATEGORIES;s_text=NULL;before_lock(&ctx);assert(category_suspends==1&&journal_ms==before_category&&flushes==2);
    assert(s_view==SHELF&&keyboard_ends==1&&category_resets==1&&selections_cleared==1&&shelf_sorts==1&&shelf_restores==1);
    s_view=SEARCH;s_search_parent=CATEGORIES;before_lock(&ctx);
    assert(s_view==SHELF&&category_suspends==2&&keyboard_ends==2&&category_resets==2);
    s_view=SHELF;s_shelf_selecting=true;before_lock(&ctx);
    assert(!s_shelf_selecting&&s_view==SHELF&&category_suspends==3&&selections_cleared==3&&shelf_restores==3);
    puts("PASS: actual reader timing excludes panels/shelf/unloaded/sleep/backward/gapped/idle intervals; idle polls, lock tail and ticket retries stay coherent");
}
'''
with tempfile.TemporaryDirectory(prefix="journal-reader-") as directory:
    work = Path(directory)
    env, compiler = toolchain(work)
    source, binary = work / "test.c", work / "test"
    source.write_text(unit)
    subprocess.run(compiler + ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", str(source), "-o", str(binary)], env=env, check=True)
    subprocess.run([str(binary)], env=env, check=True)
