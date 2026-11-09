#!/usr/bin/env python3
"""中文：正文先画、外框后画，draw_reader_outer 不得清屏擦掉正文。
English: The body is painted before the chrome, so draw_reader_outer must not clear the frame.
SPDX-FileCopyrightText: 2026 mindreset
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BOOK = ROOT / 'main/apps/app_book.c'
KIT = ROOT / 'main/ui/ui_kit.c'


def function(name, path):
    import re
    text = path.read_text()
    scan = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"', lambda m: ' ' * len(m[0]), text)
    start = re.search(r'^(?:static )?(?:[A-Za-z_]\w*(?:\s+|\s*\*\s*))+' + name + r'\([^;{}]*?\)\s*\{', scan, re.M)
    assert start, name
    at, depth = start.end(), 1
    while depth:
        depth += (scan[at] == '{') - (scan[at] == '}')
        at += 1
    return text[start.start():at]


unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {int x,y,width,height;} EpdRect;
#define EPD_DRAW_ALIGN_LEFT 0
#define EPD_DRAW_ALIGN_CENTER 1
#define EPD_DRAW_ALIGN_RIGHT 2
// 固件里对齐参数是枚举；桩把宏留给调用点，这里只补类型。
// The firmware passes an enum; the stubs keep the macros for call sites and add the type here.
enum EpdFontFlags { EPD_FONT_FLAGS_HOST = 0 };
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_GRAY_LIGHT 224
#define UI_GRAY_BLACK 0
#define READER_FULLSCREEN_PROGRESS_TOP 1180
enum {READER_PANEL_NONE,READER_PANEL_BACKGROUND_PREVIEW};
// 正文带固定为窗口化档的行；帧缓冲按旋转后的 684×1216、4bpp 行距取字节。
// The body band is fixed to the windowed row range; the frame is counted with the rotated
// 684x1216 footprint at 4bpp row pitch.
static const int body_y=176,body_height=959;
static const int row_bytes=UI_LOCK_WIDTH/2;
static bool s_reader_fullscreen,statusbar_on=true,s_save_failed;
static int s_reader_panel=READER_PANEL_NONE,s_px=48;
static char s_chapter_heading_title[128]="",s_chapter_heading_label[64]="",s_reader_notice[64]="";
static int s_chapter_lead_height,s_chapter_lead_width;
// 桩：只有正文与页眉/页脚/状态栏计数，别的页面函数都不参与。
// Stubs: only the body and a chrome call count; no other page function takes part.
static int chrome_calls;
static EpdRect body_rect(void){return (EpdRect){36,body_y,612,body_height};}
static EpdRect progress_rect(void){return (EpdRect){36,1135,612,57};}
static bool app_settings_reader_statusbar(void){return statusbar_on;}
static bool app_settings_book_vertical(void){return false;}
static void ui_text_set_system_scale(bool on){(void)on;}
static void ui_nav_status(uint8_t *fb){(void)fb;++chrome_calls;}
static void draw_reader_header(uint8_t *fb){(void)fb;++chrome_calls;}
static void draw_vertical_heading(uint8_t *fb,EpdRect body){(void)fb;(void)body;++chrome_calls;}
// 题头带与样式编号按固件取值；这个测试只关心正文是否被外框擦掉。
// Band and style index match the firmware; this test only checks the body surviving the chrome.
enum {APP_BOOK_TITLE_STYLE_PLAIN=3};
#define APP_BOOK_TITLE_SIZE_DEFAULT 48
static unsigned app_settings_book_title_style(void){return APP_BOOK_TITLE_STYLE_PLAIN;}
static unsigned app_settings_book_title_size(void){return APP_BOOK_TITLE_SIZE_DEFAULT;}
// 题头带高按固件取值；draw_reader_outer 的实参要和真源码一致。
// The band height matches the firmware; draw_reader_outer passes it straight from the real source.
static EpdRect reader_heading_band(int top){return (EpdRect){36,top,612,294};}
static void draw_reader_title_style(uint8_t *fb,EpdRect band,int style,int size,const char *label,const char *title){(void)fb;(void)band;(void)style;(void)size;(void)label;(void)title;++chrome_calls;}
static void draw_reader_images(uint8_t *fb,size_t page,EpdRect body){(void)fb;(void)page;(void)body;}
static void draw_reader_panel(uint8_t *fb){(void)fb;++chrome_calls;}
static void ui_reader_background_paint(uint8_t *fb,EpdRect r,bool top){(void)fb;(void)r;(void)top;}
static void ui_fill_round_rect(uint8_t *fb,EpdRect r,int radius,uint8_t gray){(void)fb;(void)r;(void)radius;(void)gray;++chrome_calls;}
static void ui_hairline(uint8_t *fb,int y,int x,int w,uint8_t gray){(void)fb;(void)y;(void)x;(void)w;(void)gray;++chrome_calls;}
static void ui_text_title_vc(uint8_t *fb,int x,int y,int px,const char *v,const char *face,int align){(void)fb;(void)x;(void)y;(void)px;(void)v;(void)face;(void)align;++chrome_calls;}
static void ui_text_vc(uint8_t *fb,int x,int y,int px,const char *v,int align,bool inv){(void)fb;(void)x;(void)y;(void)px;(void)v;(void)align;(void)inv;++chrome_calls;}
static size_t book_layout_page_count(void){return 47;}
static int ui_text_title_fit(char *out,int preferred,int width,const char *source){(void)out;(void)preferred;(void)width;(void)source;return preferred;}
static int ui_content_width(void){return UI_LOCK_WIDTH;}
// 页脚两段各自记一次，用来验证整行走同一种字体、字号固定 28。
// Record both footer halves so the test can prove one face and a fixed 28 px.
static bool card_face;
static int footer_builtin_draws, footer_card_draws, footer_px;
static void ui_text_fixed(uint8_t *fb,int x,int y,int px,const char *v,int align,bool inv){(void)fb;(void)x;(void)y;(void)v;(void)align;(void)inv;++chrome_calls;++footer_builtin_draws;footer_px=px;}
static int ui_text_fixed_width_px(int px,const char *v){(void)v;return px*2;}
#define TTF_FONT_SLOT_SYSTEM 0
#define UI_INK_BLACK 0
#define UI_INK_WHITE 15
static bool ttf_font_ready(void){return card_face;}
static bool ttf_font_is_builtin(void){return !card_face;}
static int ttf_font_select(int slot){(void)slot;return TTF_FONT_SLOT_SYSTEM;}
static int ttf_text_width_px(int px,const char *v){(void)v;return px*2;}
static int ttf_ascender_px(int px){return px*3/4;}
static void ttf_draw_text_px(uint8_t *fb,int x,int y,int px,const char *v,int align,int fg,int bg){(void)fb;(void)x;(void)y;(void)v;(void)align;(void)fg;(void)bg;++footer_card_draws;footer_px=px;}
static void fit_fixed_text(char *out,int px,int width){(void)out;(void)px;(void)width;}
static void copy_text(char *out,size_t cap,const char *in){snprintf(out,cap,"%s",in);}
// 章名带内建字库缺失的【】，用来证明页脚字体不再随标点变化。
// The title carries 【】, which the builtin face lacks, proving the footer face no longer
// follows the punctuation.
static void reader_footer_chapter_name(char *out,size_t cap){snprintf(out,cap,"%s","第2章【灵葫芦】");}
static unsigned percent(size_t page){return (unsigned)page*10;}
// 正文替身：把正文行带写成黑，代表排版已经画好的一页文字。
// Body stand-in: blacken the body row band, standing for a page the layout already painted.
static void book_layout_draw_page(uint8_t *fb,size_t page,EpdRect rect,int px){
    (void)page;(void)rect;(void)px;
    memset(fb+body_y*row_bytes,0x00,(size_t)body_height*row_bytes);
}
static int ink_bytes(const uint8_t *fb,size_t from,size_t to){
    int count=0;
    for(size_t i=from;i<to;++i)if(fb[i]!=0xff)++count;
    return count;
}
static int body_ink(const uint8_t *fb){
    return ink_bytes(fb,(size_t)body_y*row_bytes,(size_t)(body_y+body_height)*row_bytes);
}
'''
unit += 'int epd_width(void){return 1216;}\n'
unit += 'int epd_height(void){return 684;}\n'
unit += function('ui_clear_page', KIT) + '\n'
unit += function('reader_footer_card_face', BOOK) + '\n'
unit += function('reader_footer_width', BOOK) + '\n'
unit += function('fit_reader_footer_text', BOOK) + '\n'
unit += function('reader_footer_text', BOOK) + '\n'
unit += function('draw_reader_outer', BOOK) + '\n'
unit += function('draw_reader', BOOK) + '\n'
unit += r'''
static uint8_t *white_frame(void){
    uint8_t *fb=malloc((size_t)UI_LOCK_WIDTH*UI_LOCK_HEIGHT/2);
    assert(fb);
    memset(fb,0xff,(size_t)UI_LOCK_WIDTH*UI_LOCK_HEIGHT/2);
    return fb;
}
int main(void){
    uint8_t *fb;
    // 一：窗口化整页重绘。正文先画、外框后画，正文必须留在帧缓冲里。
    // One: windowed full page repaint. The body is painted first and the chrome after it;
    // the body text must stay in the frame.
    fb=white_frame();
    s_reader_fullscreen=false;statusbar_on=true;s_reader_panel=READER_PANEL_NONE;
    chrome_calls=0;
    draw_reader(fb,2);
    assert(chrome_calls>0);
    assert(body_ink(fb)>0);
    puts("PASS: draw_reader keeps the painted body under the chrome");
    free(fb);
    // 二：预绘命中。整帧来自预绘缓冲，外框只在它上面补画，正文同样不能被清掉。
    // Two: a prefetch hit. The frame comes from the prefetch buffer and the chrome is drawn over
    // it; the body must not be cleared here either.
    fb=white_frame();
    book_layout_draw_page(fb,3,body_rect(),s_px);
    chrome_calls=0;
    draw_reader_outer(fb,3);
    assert(chrome_calls>0);
    assert(body_ink(fb)>0);
    puts("PASS: draw_reader_outer keeps the prefetched body");
    free(fb);
    // 三：有卡上字体时页脚两段都走阅读字体，字号固定 28，不受章名标点影响。
    // Three: with a card face loaded both footer halves use it at a fixed 28 px, independent of
    // the punctuation in the chapter name.
    fb=white_frame();
    s_reader_fullscreen=false;s_reader_panel=READER_PANEL_NONE;
    card_face=true;footer_builtin_draws=footer_card_draws=0;footer_px=0;
    draw_reader_outer(fb,2);
    assert(footer_card_draws==2&&!footer_builtin_draws&&footer_px==28);
    puts("PASS: footer keeps the reading face at 28 px");
    free(fb);
    // 四：没有卡上字体时两段一起退回内建补充位图，仍是 28。
    // Four: without a card face both halves fall back to the builtin supplement, still 28 px.
    fb=white_frame();
    card_face=false;footer_builtin_draws=footer_card_draws=0;footer_px=0;
    draw_reader_outer(fb,2);
    assert(footer_builtin_draws==2&&!footer_card_draws&&footer_px==28);
    puts("PASS: footer falls back to the builtin face at 28 px");
    free(fb);
    return 0;
}
'''

with tempfile.TemporaryDirectory() as folder:
    src = Path(folder) / 'test.c'
    src.write_text(unit)
    flags = ['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function']
    subprocess.run(flags + [str(src), '-o', str(Path(folder) / 'test')], check=True)
    subprocess.run([str(Path(folder) / 'test')], check=True)
