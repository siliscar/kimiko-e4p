#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 chopper1026
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：真实文件页渲染、点击边界及字体回归，可导出 host 图。
English: Real file-page rendering, touch boundaries and font regressions with optional PNGs.
"""
import argparse
from pathlib import Path
import platform
import subprocess
import tempfile

from PIL import Image
import reading_journal_ui_host_test as journal
import reader_title_style_preview as host
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
KIT = ROOT / "main/ui/ui_kit.c"
FILES = ROOT / "main/apps/app_files.c"
NAV = ROOT / "main/ui/ui_nav.c"


def unit():
    code = journal.unit().split(journal.source("main/services/reading_journal_model.c"))[0]
    code = code.replace("static unsigned app_settings_system_font_size(void){return 120;}",
                        "static unsigned font_percent=120;\nstatic unsigned app_settings_system_font_size(void){return font_percent;}")
    code += "\n#undef ui_text_fixed_vc\n"
    for name in ("ui_fill_round_rect_raw", "ui_fill_round_rect", "ui_draw_control_frame",
                 "ui_text_effective_px", "ui_text", "ui_text_vc", "ui_rect_hit"):
        code += host.function(name, KIT) + "\n"
    code += r'''
static void box_text(uint8_t *fb,EpdRect box,int x,int y,int px,const char *text,enum EpdFontFlags align,bool inv){
    int above=0,below=0;ui_font_measure_line_px(px,text,&above,&below);
    int baseline=y+(above-below)/2,width=ui_text_fixed_width_px(px,text);
    int left=x-(align==EPD_DRAW_ALIGN_CENTER?width/2:align==EPD_DRAW_ALIGN_RIGHT?width:0);
    assert(left>=box.x&&left+width<=box.x+box.width);
    assert(baseline-above>=box.y&&baseline+below<box.y+box.height);
    checked_text(fb,x,y,px,text,align,inv);
}
enum {UI_PAGER_NONE=-1,UI_PAGER_FIRST,UI_PAGER_PREVIOUS,UI_PAGER_NEXT,UI_PAGER_LAST};
enum {APP_MAIN_REFRESH_NORMAL};
static int app_settings_main_refresh_mode(void){return APP_MAIN_REFRESH_NORMAL;}
#define UI_NAV_TAB_ICON_PX 44
#define FILE_ICON_PX 46
#define FILE_ROWS 9
'''
    code += host.function("ui_text_box", KIT).replace("ui_text_fixed_vc(fb,x,", "box_text(fb,box,x,") + "\n"
    for name in ("ui_pager_area", "ui_pager_rect", "ui_pager_hit", "ui_pager_target", "ui_pager_draw"):
        code += host.function(name, KIT) + "\n"
    for name in ("icon", "ui_nav_draw", "ui_nav_hit"):
        code += host.function(name, NAV) + "\n"
    code += r'''
static void ui_nav_status(uint8_t *fb){
    checked_text(fb,36,40,28,"08:30",EPD_DRAW_ALIGN_LEFT,false);
    checked_text(fb,342,40,26,"mogo",EPD_DRAW_ALIGN_CENTER,false);
    checked_text(fb,592,40,28,"86%",EPD_DRAW_ALIGN_RIGHT,false);
    ui_draw_control_frame(fb,(EpdRect){604,28,40,24},3,0);
    ui_fill_rect_fast((EpdRect){609,33,26,14},0,fb);
    ui_fill_rect_fast((EpdRect){644,35,4,10},0,fb);
}
static void ui_nav_back(uint8_t *fb,int x,int y){ui_draw_icon(fb,x+22,y+35,48,UI_ICON_ANIMATE_CIRCLE_CHEVRON_LEFT,0x30);}
typedef struct {char path[288];char name[128];long long size;bool is_dir;} file_item_t;
typedef struct {bool mounted,space_valid,present,needs_format;uint64_t filesystem_bytes,free_bytes,capacity_bytes;} board_sd_info_t;
typedef enum {FILE_VIEW_LIST,FILE_VIEW_ACTIONS,FILE_VIEW_RENAME,FILE_VIEW_MOVE} file_view_t;
static file_item_t s_files[24],s_selected;
static int s_count,s_folder,s_page,s_pager_pressed=-1;
static bool s_scan_pending,s_sd_format_confirm,s_pager_paint_pending,s_delete_confirm;
static char s_message[96],s_sd_notice[64],s_dir[288]="/sdcard";
static board_sd_info_t s_sd;
static file_view_t s_view;
static const char *const names[]={"书籍","图片","字体","TF 卡目录"};
static const char *app_settings_books_dir(void){return "/sdcard/books";}
static const char *app_settings_fonts_dir(void){return "/sdcard/fonts";}
static void render_rename(uint8_t *fb){(void)fb;assert(false);}
static void ui_draw_button(uint8_t *fb,EpdRect r,const char *s,bool on){
    (void)on;ui_draw_control_frame(fb,r,8,0);ui_text_box(fb,r,24,s,EPD_DRAW_ALIGN_CENTER,false);
}
'''
    for name in ("copy_utf8", "folder_root", "visible_rows", "root_rows", "file_row_rect", "file_row_index",
                 "file_pager_pages", "draw_file_pager", "file_pager_paint", "file_pager_event",
                 "home_transfer_rect", "storage_card_rect", "sd_needs_format", "file_icon", "file_detail",
                 "file_rule", "file_card_border", "fit_name", "file_list_name", "draw_file_row",
                 "render_actions", "storage_space_valid", "storage_amount", "storage_usage", "render"):
        code += host.function(name, FILES) + "\n"
    code += r'''
// 只替换设备服务；页面分发与几何保留生产实现。/ Stub device services, not page dispatch or geometry.
static int opened,scans,route=-1,nav=-1,moves,cancels,action_calls,rename_calls,formats,remounts;
const app_desc_t app_transfer={0};
static void app_transfer_request_wifi_upload(void){route=0;}
static void app_transfer_request_hotspot_start(void){route=1;}
static void app_transfer_request_usb_start(void){route=2;}
static void ui_nav_request(app_ctx_t *ctx,int tab){(void)ctx;nav=tab;}
static void scan_folder(int folder){++scans;s_folder=folder;s_page=0;}
static void open_selected(app_ctx_t *ctx){(void)ctx;++opened;}
static void leave_move(bool moved){assert(!moved);++cancels;}
static app_redraw_t move_selected(void){++moves;return APP_REDRAW_PAGE;}
static app_redraw_t action_gesture(app_ctx_t *ctx,const ui_gesture_event_t *ev){(void)ctx;(void)ev;++action_calls;return APP_REDRAW_NONE;}
static app_redraw_t rename_gesture(app_ctx_t *ctx,const ui_gesture_event_t *ev){(void)ctx;(void)ev;++rename_calls;return APP_REDRAW_NONE;}
static bool storage_jobs_quiesce(int ms){assert(ms==2000);return true;}
static esp_err_t board_sd_format(void){++formats;return ESP_OK;}
static esp_err_t board_sd_remount(void){++remounts;return ESP_OK;}
static void board_sd_get_info(board_sd_info_t *sd){*sd=s_sd;}
static const char *esp_err_to_name(esp_err_t err){(void)err;return "ERROR";}
#ifndef ESP_ERR_NOT_FINISHED
#define ESP_ERR_NOT_FINISHED 7
#endif
'''
    code += host.function("on_gesture", FILES) + "\n"
    code += (ROOT / "tools/file_ui_host_test.c").read_text()
    return code


def run(output, source=None):
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="files-raster-") as directory:
        work = Path(directory)
        host.write_shims(work)
        shim = work / "epdiy.h"
        shim.write_text(shim.read_text().replace(
            "EPD_DRAW_ALIGN_LEFT, EPD_DRAW_ALIGN_CENTER, EPD_DRAW_ALIGN_RIGHT",
            "EPD_DRAW_ALIGN_LEFT=2, EPD_DRAW_ALIGN_CENTER=8, EPD_DRAW_ALIGN_RIGHT=4"))
        heap = work / "esp_heap_caps.h"
        heap.write_text(heap.read_text() + "\nvoid *heap_caps_realloc(void *,size_t,int);\n")
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
            assets.write('const uint8_t hanzi[] asm("_binary_ui_hanzi_bin_start")={' + ','.join(map(str, payload)) + '};\n')
            assets.write(f'asm(".globl _binary_ui_hanzi_bin_end\\n.set _binary_ui_hanzi_bin_end, _binary_ui_hanzi_bin_start + {len(payload)}");\n')
        (work / "test.c").write_text(unit() if source is None else source)
        env, compiler = toolchain(work)
        flags = compiler + ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable",
                            "-Wno-unused-function", "-Wno-deprecated-declarations", "-fsanitize=address,undefined", "-g"]
        for folder in (work, *(ROOT / "main" / f for f in ("", "ui", "font", "boot", "diagnostics", "display", "settings", "app", "services"))):
            flags += ["-I" + str(folder)]
        units = [work / "test.c", work / "assets.c"] + [ROOT / "main/font" / name for name in (
            "ttf_font.c", "ttf_font_source.c", "ttf_block_cache.c", "ttf_glyph_cache.c", "ui_font.c", "ui_hanzi.c")]
        linker = "-Wl,-dead_strip" if platform.system() == "Darwin" else "-Wl,--gc-sections"
        subprocess.run(flags + list(map(str, units)) + [linker, "-lm", "-lz", "-o", str(work / "test")], env=env, check=True)
        subprocess.run([str(work / "test"), str(output)], env=env, check=True)
    for raw in output.glob("*.raw"):
        image = Image.frombytes("L", (684, 1216), raw.read_bytes()).point(lambda v: v * 17 // 16)
        assert image.getextrema() == (0, 255) and len(image.getcolors()) <= 16
        image.save(raw.with_suffix(".png"))
        raw.unlink()
    print(f"PASS: native 684x1216, 16-gray product render; deterministic sample data only: {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output:
        run(args.output.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="files-ui-") as directory:
            run(Path(directory))
