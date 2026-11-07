#!/usr/bin/env python3
"""中文：正文先画、外框后画，draw_reader_outer 不得清屏擦掉正文。
English: The body is painted before the chrome, so draw_reader_outer must not clear the frame.
SPDX-FileCopyrightText: 2026 mindreset
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess
import re
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
enum EpdFontFlags {EPD_DRAW_ALIGN_LEFT, EPD_DRAW_ALIGN_CENTER, EPD_DRAW_ALIGN_RIGHT};
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_GRAY_LIGHT 224
#define UI_GRAY_BLACK 0
#define READER_FULLSCREEN_PROGRESS_TOP (UI_LOCK_HEIGHT - 12)
#define UI_BAR_H 96
#define UI_BAR_TOP (UI_LOCK_HEIGHT - 24 - UI_BAR_H)
#define READER_FOOTER_PROGRESS_SPACE 19
#define CHROME_SLOT_GAP 24
#define CHROME_ICON_W 42
#define CHROME_SLOT_TEXT_MAX 64
enum {READER_PANEL_NONE,READER_PANEL_BACKGROUND_PREVIEW};
// 正文带固定为窗口化档的行；帧缓冲按旋转后的 684×1216、4bpp 行距取字节。
// The body band is fixed to the windowed row range; the frame is counted with the rotated
// 684x1216 footprint at 4bpp row pitch.
static const int body_y=176,body_height=959;
static const int row_bytes=UI_LOCK_WIDTH/2;
static bool s_reader_fullscreen,statusbar_on=true,s_reader_progress_bar=true;
static int s_chrome_height[2]={80,57};
static int s_reader_panel=READER_PANEL_NONE,s_px=48;
static char s_chapter_heading_title[128]="",s_chapter_heading_label[64]="",s_reader_notice[64]="";
static int s_chapter_lead_height,s_chapter_lead_width;
// 桩：只有正文与页眉/页脚/状态栏计数，别的页面函数都不参与。
// Stubs: only the body and a chrome call count; no other page function takes part.
static int chrome_calls;
static int reader_footer_height(void);
static EpdRect body_rect(void){return (EpdRect){36,body_y,612,1192-reader_footer_height()-body_y};}
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
static void ui_fill_rect_fast(EpdRect r,uint8_t gray,uint8_t *fb){
    assert(r.x>=0&&r.y>=0&&r.x+r.width<=UI_LOCK_WIDTH&&r.y+r.height<=UI_LOCK_HEIGHT);
    for(int y=r.y;y<r.y+r.height;++y)for(int x=r.x;x<r.x+r.width;++x){
        uint8_t *pixel=fb+y*row_bytes+x/2;unsigned shift=(x%2)*4;
        *pixel=(uint8_t)((*pixel&~(15u<<shift))|((gray>>4)<<shift));
    }
}
static void epd_draw_vline(int x,int y,int h,uint8_t gray,uint8_t *fb){ui_fill_rect_fast((EpdRect){x,y,1,h},gray,fb);}
static uint8_t ui_control_gray(uint8_t gray){return gray;}

static void ui_hairline(uint8_t *fb,int y,int x,int w,uint8_t gray){(void)fb;(void)y;(void)x;(void)w;(void)gray;++chrome_calls;}
static void ui_text_title_vc(uint8_t *fb,int x,int y,int px,const char *v,const char *face,int align){(void)fb;(void)x;(void)y;(void)px;(void)v;(void)face;(void)align;++chrome_calls;}
static void ui_text_vc(uint8_t *fb,int x,int y,int px,const char *v,int align,bool inv){(void)fb;(void)x;(void)y;(void)px;(void)v;(void)align;(void)inv;++chrome_calls;}
static size_t book_layout_page_count(void){return 47;}
static int ui_text_title_fit(char *out,int preferred,int width,const char *source){(void)out;(void)preferred;(void)width;(void)source;return preferred;}
static int ui_content_width(void){return UI_LOCK_WIDTH;}
// 页脚两段各自记一次，用来验证整行走同一种字体、字号固定 28。
// Record both footer halves so the test can prove one face and a fixed 28 px.
static bool card_face;
static int footer_builtin_draws, footer_card_draws, footer_px, footer_top;
static void ui_text_fixed(uint8_t *fb,int x,int y,int px,const char *v,int align,bool inv){(void)fb;(void)x;(void)y;(void)v;(void)align;(void)inv;++chrome_calls;++footer_builtin_draws;footer_px=px;footer_top=y;}
static int ui_text_fixed_width_px(int px,const char *v){(void)v;return px*2;}
#define TTF_FONT_SLOT_SYSTEM 0
#define UI_INK_BLACK 0
#define UI_INK_WHITE 15
static bool ttf_font_ready(void){return card_face;}
static bool ttf_font_is_builtin(void){return !card_face;}
static int ttf_font_select(int slot){(void)slot;return TTF_FONT_SLOT_SYSTEM;}
static int ttf_text_width_px(int px,const char *v){(void)v;return px*2;}
static int ttf_ascender_px(int px){return px*3/4;}
static void ttf_draw_text_px(uint8_t *fb,int x,int y,int px,const char *v,int align,int fg,int bg){(void)fb;(void)x;(void)y;(void)v;(void)align;(void)fg;(void)bg;++footer_card_draws;footer_px=px;footer_top=y-px*3/4;}
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
    (void)page;(void)px;
    memset(fb+rect.y*row_bytes,0x00,(size_t)rect.height*row_bytes);
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
chrome = ROOT / 'main/book/reader_chrome.c'
settings = (ROOT / 'main/settings/settings.h').read_text()
unit += re.search(r'typedef enum \{\s*APP_READER_SLOT_BLANK.*?\} app_reader_slot_item_t;', settings, re.S)[0] + '\n'
unit += re.search(r'typedef struct \{\s*const char \*title;.*?\} reader_chrome_data_t;', (ROOT / 'main/book/reader_chrome.h').read_text(), re.S)[0] + '\n'
unit += re.search(r'typedef struct \{\s*app_reader_slot_item_t item;.*?\} chrome_slot_t;', chrome.read_text(), re.S)[0] + '\n'
unit += r'''
static app_reader_slot_item_t app_settings_reader_chrome_item(unsigned bar,unsigned slot){
    (void)bar;static const app_reader_slot_item_t items[]={APP_READER_SLOT_CHAPTER,APP_READER_SLOT_BLANK,APP_READER_SLOT_PAGE_PROGRESS};return items[slot];
}
static void ui_nav_battery_draw(uint8_t *fb,int x,int y){(void)fb;(void)x;(void)y;}
static void reader_chrome_data(reader_chrome_data_t *data){*data=(reader_chrome_data_t){.title="图书",.chapter="第2章【灵葫芦】",.page=2,.page_count=47,.percent=20,.clock="12:00",.date="11-07"};}
static int64_t s_dictionary_notice_until;
typedef struct {int unused;} book_layout_glyph_t;
static struct {bool active;char query[8];} s_dictionary_selection;
static int s_dictionary_view;
static struct {char query[8];} s_dictionary_result;
static const book_layout_glyph_t *book_layout_glyphs(size_t page,size_t *count){(void)page;*count=0;return NULL;}
static void ui_reader_dictionary_notice(uint8_t *fb){(void)fb;}
#define ui_reader_dictionary_selection(...) ((void)glyphs,(void)count)
#define ui_reader_dictionary_draw(...) ((void)s_dictionary_view,(void)s_dictionary_result)
'''
for name in ('chrome_copy', 'cut_last_utf8', 'slot_text', 'chrome_face_ready', 'chrome_face_width',
             'chrome_face_draw', 'slot_measure', 'slot_trim', 'layout_slots', 'slot_place', 'reader_chrome_draw_footer_row'):
    unit += function(name, chrome) + '\n'
for name in ('clamp_radius', 'ui_fill_round_rect_raw', 'ui_fill_round_rect'):
    if name == 'ui_fill_round_rect_raw':
        unit += function('epd_fill_circle_helper', ROOT / 'components/epdiy/src/epdiy.c') + '\n'
    unit += function(name, KIT) + '\n'
for name in ('reader_footer_height', 'progress_rect'):
    unit += function(name, BOOK) + '\n'
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
    // 隐藏后页脚带缩小、文字位置不动；回收给正文的像素不会被外框清掉。
    // Hiding shrinks the footer band without moving text or clearing the pixels reclaimed by the body.
    for(int full=0;full<2;++full)for(int height=57;height<=121;height+=8){
        s_reader_fullscreen=full;s_chrome_height[1]=height;
        for(size_t page=0;page<=10;page+=5){
            fb=white_frame();s_reader_progress_bar=true;footer_builtin_draws=footer_card_draws=0;
            draw_reader_outer(fb,page);
            EpdRect strip=full?(EpdRect){36,READER_FULLSCREEN_PROGRESS_TOP+3,612,3}:
                (EpdRect){36,1192-height+8,612,6};
            int first=UI_LOCK_HEIGHT,last=-1;
            for(int y=0;y<UI_LOCK_HEIGHT;++y)if(ink_bytes(fb,(size_t)y*row_bytes,(size_t)(y+1)*row_bytes)){
                if(y<first)first=y;last=y;
            }
            assert(first==strip.y&&last==strip.y+strip.height-1);
            int previous_top=footer_top,previous_draws=footer_builtin_draws;
            assert(previous_draws==(full?0:2));
            if(!full)assert(previous_top==1192-height+27&&footer_px==28);
            free(fb);fb=white_frame();s_reader_progress_bar=false;footer_builtin_draws=footer_card_draws=0;
            draw_reader_outer(fb,page);
            assert(ink_bytes(fb,0,(size_t)UI_LOCK_HEIGHT*row_bytes)==0);
            assert(footer_builtin_draws==previous_draws&&(full||footer_top==previous_top));
            free(fb);
        }
    }
    s_chrome_height[1]=57;s_reader_fullscreen=false;s_reader_progress_bar=false;
    fb=white_frame();book_layout_draw_page(fb,3,body_rect(),s_px);draw_reader_outer(fb,3);
    assert(body_ink(fb)==body_height*row_bytes);
    assert(ink_bytes(fb,1135u*row_bytes,1154u*row_bytes)==19*row_bytes);free(fb);
    puts("PASS: footer bar is 6px at all heights, full-screen line stays 3px, hidden progress preserves footer text and reclaimed body pixels");
    return 0;
}
'''

with tempfile.TemporaryDirectory() as folder:
    src = Path(folder) / 'test.c'
    src.write_text(unit)
    flags = ['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(flags + [str(src), '-o', str(Path(folder) / 'test')], check=True)
    subprocess.run([str(Path(folder) / 'test')], check=True)
