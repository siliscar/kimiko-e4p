#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：验证传书移动/删除后的 MOBI 阅读状态接续，不访问设备或网络。
English: Verify MOBI state after transfer moves/deletes without devices or networking.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
from home_cover_cache_host_test import ROOT, function

page = ROOT / 'main/apps/app_transfer.c'
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#define BOOK_STORE_PATH_MAX 288
typedef struct {uint32_t file_size,byte_off;uint16_t chapter;} book_progress_t;
static book_progress_t saved={77,123,2};
static char last[288],old_path[288],new_path[288];
static unsigned saves,forgets,titles;
static bool book_progress_load(const char *p,uint32_t size,book_progress_t *out){assert(!strcmp(p,old_path)&&size==77);*out=saved;return true;}
static bool book_progress_last_path(char *out,size_t cap){snprintf(out,cap,"%s",last);return true;}
static int book_progress_save(const char *p,const book_progress_t *value){assert(!strcmp(p,new_path)&&!memcmp(value,&saved,sizeof(saved)));++saves;return 0;}
static int book_progress_forget(const char *p){assert(!strcmp(p,old_path));++forgets;return 0;}
static int book_progress_set_last_path(const char *p){strcpy(last,p);return 0;}
static int book_title_clear(const char *p){assert(!strcmp(p,old_path));++titles;return 0;}
static const char *app_settings_system_font_path(void){return "";}
static const char *app_settings_font_path(void){return "";}
static const char *app_settings_wallpaper_path(void){return "";}
static void app_settings_set_system_font_path(const char *p){(void)p;assert(false);}
static void app_settings_set_font_path(const char *p){(void)p;assert(false);}
static void app_settings_set_wallpaper_path(const char *p){(void)p;assert(false);}
'''
for name in ('transfer_book_file', 'transfer_file_deleted', 'transfer_file_moved'):
    unit += function(name, page) + '\n'
unit += r'''
int main(void){
 const char *extensions[]={"mobi","MOBI","epub","txt","bin"};
 for(unsigned i=0;i<5;++i){
  snprintf(old_path,sizeof(old_path),"/sdcard/books/old.%s",extensions[i]);
  snprintf(new_path,sizeof(new_path),"/sdcard/books/new.%s",extensions[i]);strcpy(last,old_path);
  saves=forgets=titles=0;bool book=i<4;
  assert(transfer_book_file(old_path)==book);transfer_file_moved(old_path,new_path,77);
  assert(saves==book&&forgets==book&&titles==book&&!strcmp(last,book?new_path:old_path));
  saves=forgets=titles=0;transfer_file_deleted(old_path);
  assert(!saves&&forgets==book&&titles==book);
 }
 assert(!transfer_book_file("/sdcard/bad.mobi.exe"));
 puts("PASS: MOBI/TXT/EPUB transfer moves preserve progress and latest path; deletes clear state; unrelated files stay untouched");
}
'''
with tempfile.TemporaryDirectory(prefix='mogo-mobi-product-') as directory:
    source, executable = Path(directory) / 'test.c', Path(directory) / 'test'
    source.write_text(unit)
    compiler = os.environ.get('CC', 'cc')
    extra = []
    modern = Path('/Library/Developer/CommandLineTools/usr/bin/clang')
    if sys.platform == 'darwin' and 'CC' not in os.environ and modern.exists():
        compiler = str(modern)
        extra = ['-isysroot', '/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk']
    subprocess.run([compiler, *extra, '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    str(source), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
