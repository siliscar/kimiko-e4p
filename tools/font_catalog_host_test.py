#!/usr/bin/env python3
"""中文：执行实际字体扫描，覆盖大目录、重扫和内存不足。
English: Exercise production font scanning with large directories, rescans and allocation faults.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "main/font/ttf_font.c").read_text()
header = (ROOT / "main/font/ttf_font.h").read_text()


def function(name, text=source):
    match = re.search(r"^(?:static )?[^\n]+\b" + name + r"\([^;{}]*\)\s*\{", text, re.M)
    assert match, name
    pos, depth = match.end(), 1
    while depth:
        depth += (text[pos] == "{") - (text[pos] == "}")
        pos += 1
    return text[match.start():pos]


unit = r'''
#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static int allocations_left=-1;
static void *heap_caps_realloc(void *p,size_t n,int caps){
 assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(!allocations_left)return NULL;if(allocations_left>0)--allocations_left;
 return realloc(p,n);
}
static void *heap_caps_calloc(size_t n,size_t size,int caps){
 void *p=heap_caps_realloc(NULL,n*size,caps);if(p)memset(p,0,n*size);return p;
}
static const char *configured_dir;
static const char *k_font_dirs[2];
static const char *app_settings_fonts_dir(void){return configured_dir;}
static ttf_font_item_t *s_catalog;
static int s_catalog_n;
'''
macros = "\n".join(re.findall(r"^#define TTF_FONT_(?:MAX|NAME_MAX|PATH_MAX) .*", header, re.M))
item = re.search(r"typedef struct \{\s*char name\[TTF_FONT_NAME_MAX\];.*?\} ttf_font_item_t;", header, re.S)[0]
capacity = re.search(r"^static size_t s_catalog_cap[^\n]*", source, re.M)
unit = unit.replace("static ttf_font_item_t *s_catalog;", macros + "\n" + item + "\nstatic ttf_font_item_t *s_catalog;")
if capacity:
    unit += capacity[0] + "\n"
for name in ("is_junk_name", "is_ttf_name", "font_stem", "font_item_cmp", "catalog_has_name",
             "catalog_alloc", "ttf_font_scan", "ttf_font_count", "ttf_font_item"):
    unit += function(name) + "\n"
book = (ROOT / "main/apps/app_book.c").read_text()
unit += re.search(r"^#define BOOK_FONT_PAGE .*", book, re.M)[0] + "\n"
unit += r'''
typedef struct{int x,y,width,height;}EpdRect;
typedef struct{int unused;}app_ctx_t;
enum{APP_REDRAW_NONE,APP_REDRAW_PAGE,READER_PANEL_FONT_PICKER,READER_PANEL_FONT_SETTINGS};
static int s_reader_panel,s_font_page,scale=100;
static const ttf_font_item_t *selected;
static int app_settings_system_font_size(void){return scale;}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static int select_reading_font_item(app_ctx_t *ctx,const ttf_font_item_t *item){(void)ctx;selected=item;return APP_REDRAW_PAGE;}
'''
for name in ("reader_font_picker_top", "reader_font_pick_rect"):
    unit += function(name, book) + "\n"
choose = re.search(r"    if \(s_reader_panel == READER_PANEL_FONT_PICKER\) \{[\s\S]*?\n    \}", book)[0]
unit += "static int reader_choose(int x,int y){app_ctx_t *ctx=NULL;" + choose + "return APP_REDRAW_NONE;}\n"
unit += r'''
static void clear_catalog(void){
 free(s_catalog);s_catalog=NULL;s_catalog_n=0;
'''
if capacity:
    unit += "s_catalog_cap=0;\n"
unit += r'''
}
int main(int argc,char **argv){
 assert(argc==5);k_font_dirs[0]=argv[1];configured_dir=argv[2];
 allocations_left=0;assert(!ttf_font_scan()&&!ttf_font_count()&&!ttf_font_item(0));
 allocations_left=-1;
 assert(ttf_font_scan()==513&&ttf_font_count()==513);
 for(int i=0;i<513;++i){
  const ttf_font_item_t *item=ttf_font_item(i);assert(item);
  char expected[64];snprintf(expected,sizeof(expected),"Font%04d",i);assert(!strcmp(item->name,expected));
  assert(strstr(item->path,i<100?argv[1]:argv[2])==item->path);
 }
 assert(!ttf_font_item(-1)&&!ttf_font_item(513));
 for(scale=100;scale<=200;scale+=10)for(int i=0;i<513;++i){
  s_reader_panel=READER_PANEL_FONT_PICKER;s_font_page=i/BOOK_FONT_PAGE;selected=NULL;
  EpdRect r=reader_font_pick_rect(i%BOOK_FONT_PAGE);
  assert(reader_choose(r.x+r.width/2,r.y+r.height/2)==APP_REDRAW_PAGE);
  assert(selected==ttf_font_item(i));
 }
 assert(ttf_font_scan()==513);
 k_font_dirs[0]=argv[3];configured_dir=argv[4];
 assert(!ttf_font_scan()&&!ttf_font_count()&&!ttf_font_item(0));
 clear_catalog();k_font_dirs[0]=argv[1];configured_dir=argv[2];
 allocations_left=1;
 int partial=ttf_font_scan();assert(partial>0&&partial<513);
 for(int i=0;i<partial;++i){assert(ttf_font_item(i));if(i)assert(strcasecmp(ttf_font_item(i-1)->name,ttf_font_item(i)->name)<0);}
 assert(!ttf_font_item(partial));
 allocations_left=-1;assert(ttf_font_scan()==513);
 clear_catalog();
 puts("PASS: 513 fonts across both roots, reader selection at 11 scales, ordering, duplicates, sidecars, bounds, rescan, missing/empty roots, allocation failure and retry");
}
'''
with tempfile.TemporaryDirectory(prefix="font-catalog-") as directory:
    work = Path(directory)
    primary, secondary, empty = (work / name for name in ("primary", "secondary", "empty"))
    for path in (primary, secondary, empty):
        path.mkdir()
    for i in reversed(range(513)):
        ((primary if i < 100 else secondary) / f"Font{i:04d}.ttf").touch()
    for name in ("font0000.TTF", ".hidden.ttf", "._Sidecar.ttf", "notes.txt"):
        (secondary / name).touch()
    c, binary = work / "test.c", work / "test"
    c.write_text(unit)
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                    "-Wno-misleading-indentation", "-fsanitize=address,undefined", str(c), "-o", str(binary)], check=True)
    subprocess.run([str(binary), str(primary), str(secondary), str(empty), str(work / "missing")], check=True)
