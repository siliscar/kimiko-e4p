#!/usr/bin/env python3
"""中文：真实「标题样式」三分之二屏面板的槽位几何与离散滚动回归。
English: Slot geometry and discrete scrolling regression for the real two-thirds heading-style sheet.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'main/apps/app_book.c').read_text()


def block(text, start):
    at = text.index('{', start)
    depth, quote, comment, escape = 0, None, None, False
    while at < len(text):
        char, pair = text[at], text[at:at+2]
        if comment == 'line':
            if char == '\n': comment = None
        elif comment == 'block':
            if pair == '*/': comment = None; at += 1
        elif quote:
            if escape: escape = False
            elif char == '\\': escape = True
            elif char == quote: quote = None
        elif pair == '//': comment = 'line'; at += 1
        elif pair == '/*': comment = 'block'; at += 1
        elif char in "\"'": quote = char
        elif char == '{': depth += 1
        elif char == '}':
            depth -= 1
            if not depth: return text[start:at+1]
        at += 1
    raise AssertionError('Unclosed function')


def function(name):
    match = re.search(r'^static [^\n;{}]+\b' + name + r'\([^;{}]*\)\s*\{', source, re.M)
    assert match, name
    return block(source, match.start())


# 面板几何常量与四个槽位函数都取真实源码，测试不另抄坐标。
# Geometry constants and the four slot functions come from the real source; the test copies no coordinates.
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "epdiy.h"
#include "app.h"
#include "ui_gesture.h"
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL} app_redraw_t;
enum EpdDrawMode {MODE_GC16,MODE_GL16,MODE_DU};
#define UI_LOCK_WIDTH 684
#define APP_BOOK_TITLE_STYLE_COUNT 4
#define READING 1
#define READER_PANEL_TITLE_STYLE 15
static int s_view=READING,s_reader_panel=READER_PANEL_TITLE_STYLE,s_pressed_control=-1;
static int s_title_style_scroll;
static bool s_title_style_drag;
static EpdRect s_area;
static enum EpdDrawMode s_mode;
static int renders,pushes;
static EpdRect pushed;
static enum EpdDrawMode pushed_mode;
static void render(app_ctx_t *ctx,uint8_t *fb){(void)ctx;(void)fb;++renders;}
'''
unit += ''.join(m + '\n' for m in re.findall(r'#define TITLE_STYLE_\w+ \d+', source))
for name in ('title_style_slot_y', 'title_style_card_rect', 'title_style_window_rect',
             'heading_size_rect', 'title_style_scroll_gesture'):
    unit += function(name) + '\n'

# 真实分发块：TAP 留给 action_at，只接住纵向移动与手势收尾。
# Real dispatch block: TAP stays with action_at; only vertical motion and gesture ends land here.
dispatch = source.index('if (s_view == READING && s_reader_panel == READER_PANEL_TITLE_STYLE) {')
dispatch = block(source, dispatch)
unit += '\nstatic app_redraw_t event(app_ctx_t *ctx,const ui_gesture_event_t *ev){' + dispatch + \
        '\nreturn APP_REDRAW_NONE;}\n'

unit += r'''
static ui_gesture_t recognizer;
static app_touch_t touch;
static app_ctx_t ctx;
static app_redraw_t feed(int x,int y,bool down,bool press,int64_t now){
 touch=(app_touch_t){.count=down?1:0,.touched=down,.x=x,.y=y};
 ctx=(app_ctx_t){.touch=&touch,.pressed=press,.released=!down,.now_ms=now};
 ui_gesture_event_t gesture;
 if(!ui_gesture_feed(&recognizer,&ctx,&gesture))return APP_REDRAW_NONE;
 app_redraw_t redraw=event(&ctx,&gesture);
 if(redraw==APP_REDRAW_AREA){++pushes;pushed=s_area;pushed_mode=s_mode;}
 return redraw;
}
static void reset(int offset){
 s_title_style_scroll=offset;s_title_style_drag=false;ui_gesture_reset(&recognizer);
}
static void whole_or_outside(void){
 // 三档落点每张卡要么整张在视口内，要么完全在外，绝不切一半。
 // At every landing each card is either wholly inside the viewport or wholly outside it.
 for(int i=0;i<APP_BOOK_TITLE_STYLE_COUNT;++i){
  EpdRect card=title_style_card_rect(i),window=title_style_window_rect(i);
  bool inside=card.y>=TITLE_STYLE_VIEW_TOP&&card.y+card.height<=TITLE_STYLE_VIEW_BOTTOM;
  bool outside=card.y+card.height<=TITLE_STYLE_VIEW_TOP||card.y>=TITLE_STYLE_VIEW_BOTTOM;
  assert(inside||outside);
  assert(window.y==card.y+TITLE_STYLE_WINDOW_TOP&&window.height==TITLE_STYLE_WINDOW_HEIGHT);
 }
}
int main(void){
 // 几何：三分之二屏面板、三槽视口，三档落点可见卡数 2/3/3。
 // Geometry: a two-thirds sheet with a three-slot viewport; 2/3/3 cards visible at the three landings.
 assert(TITLE_STYLE_SHEET_TOP==1216/3);
 assert(TITLE_STYLE_VIEW_TOP+TITLE_STYLE_VIEW_HEIGHT==TITLE_STYLE_VIEW_BOTTOM);
 assert(TITLE_STYLE_VIEW_HEIGHT==TITLE_STYLE_VIEW_SLOTS*TITLE_STYLE_CARD_PITCH);
 int offsets[]={0,TITLE_STYLE_CARD_PITCH,2*TITLE_STYLE_CARD_PITCH};
 int expect[]={2,3,3};
 for(int s=0;s<3;++s){
  s_title_style_scroll=offsets[s];whole_or_outside();
  int visible=0;
  for(int i=0;i<APP_BOOK_TITLE_STYLE_COUNT;++i){
   EpdRect card=title_style_card_rect(i);
   if(card.y>=TITLE_STYLE_VIEW_TOP&&card.y+card.height<=TITLE_STYLE_VIEW_BOTTOM)++visible;
  }
  assert(visible==expect[s]);
 }
 // 字号行跟着槽 0 走：首屏三格都在视口内，滚过一槽后整行离开视口。
 // The size row follows slot 0: all three controls start inside the viewport and leave after one slot.
 s_title_style_scroll=0;
 for(int i=0;i<3;++i){
  EpdRect r=heading_size_rect(i);
  assert(r.y>=TITLE_STYLE_VIEW_TOP&&r.y+r.height<=TITLE_STYLE_VIEW_BOTTOM);
 }
 assert(heading_size_rect(1).y==heading_size_rect(0).y);
 s_title_style_scroll=TITLE_STYLE_CARD_PITCH;
 assert(heading_size_rect(0).y+64<=TITLE_STYLE_VIEW_TOP);

 // 手势：一次拖动一槽，闩住后同一次手势不再推进；长拖两槽；刷新只推视口。
 // Gestures: one slot per drag with a latch, two slots for a long drag, and only the viewport pushed.
 reset(0);renders=pushes=0;
 assert(feed(300,800,true,true,0)==APP_REDRAW_NONE);
 assert(feed(300,650,true,false,100)==APP_REDRAW_AREA&&s_title_style_scroll==TITLE_STYLE_CARD_PITCH);
 assert(renders==1&&pushes==1&&pushed_mode==MODE_GL16);
 assert(pushed.x==0&&pushed.y==TITLE_STYLE_VIEW_TOP&&pushed.width==UI_LOCK_WIDTH&&
        pushed.height==TITLE_STYLE_VIEW_HEIGHT);
 for(int i=1;i<=20;++i)assert(feed(300,650-i*5,true,false,100+i*10)==APP_REDRAW_NONE&&
                               s_title_style_scroll==TITLE_STYLE_CARD_PITCH);
 assert(feed(300,600,false,false,400)==APP_REDRAW_NONE&&renders==1&&pushes==1);
 assert(feed(300,600,false,false,900)==APP_REDRAW_NONE&&renders==1);

 reset(0);
 feed(300,900,true,true,0);
 assert(feed(300,500,true,false,100)==APP_REDRAW_AREA&&s_title_style_scroll==2*TITLE_STYLE_CARD_PITCH);
 feed(300,500,false,false,200);

 // 顶部与底部都夹住，不越界。
 // Both ends clamp without overshooting.
 reset(0);feed(300,600,true,true,0);
 assert(feed(300,800,true,false,100)==APP_REDRAW_NONE&&s_title_style_scroll==0);
 feed(300,800,false,false,200);
 reset(2*TITLE_STYLE_CARD_PITCH);feed(300,900,true,true,0);
 assert(feed(300,600,true,false,100)==APP_REDRAW_NONE&&s_title_style_scroll==2*TITLE_STYLE_CARD_PITCH);
 feed(300,600,false,false,200);

 // 视口外起手、横向拖动、短拖都不接管。
 // Starts outside the viewport, horizontal drags and short drags are ignored.
 reset(0);feed(300,450,true,true,0);
 assert(feed(300,200,true,false,100)==APP_REDRAW_NONE&&s_title_style_scroll==0);
 feed(300,200,false,false,200);
 reset(0);feed(300,800,true,true,0);
 assert(feed(500,800,true,false,100)==APP_REDRAW_NONE&&s_title_style_scroll==0);
 feed(500,800,false,false,200);
 reset(0);feed(300,800,true,true,0);
 assert(feed(300,700,true,false,100)==APP_REDRAW_NONE&&s_title_style_scroll==0);
 feed(300,700,false,false,200);

 // 多点取消只复位闩，不推进；TAP 不被分发块吞掉（返回 NONE 由 action_at 接手）。
 // A multitouch cancel only clears the latch; a TAP is not swallowed by the dispatch block.
 reset(0);feed(300,800,true,true,0);touch.count=2;ctx.pressed=false;
 ui_gesture_event_t canceled;
 assert(ui_gesture_feed(&recognizer,&ctx,&canceled)&&canceled.type==UI_GESTURE_CANCEL);
 assert(event(&ctx,&canceled)==APP_REDRAW_NONE&&s_title_style_scroll==0&&!s_title_style_drag);
 reset(0);feed(300,800,true,true,0);
 assert(feed(300,800,false,false,100)==APP_REDRAW_NONE&&s_title_style_scroll==0);

 puts("PASS: two-thirds heading-style sheet keeps every card whole at all three landings, moves one "
      "or two slots per gesture with a latch, clamps at both ends and pushes only the 642 px viewport");
}
'''
with tempfile.TemporaryDirectory(prefix='mogo-title-style-scroll-') as directory:
    path = Path(directory) / 'sheet.c'
    binary = path.with_suffix('')
    path.write_text(unit)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I' + str(root / 'tools/ui_gesture_stubs'), '-I' + str(root / 'main/ui'),
                    str(path), str(root / 'main/ui/ui_gesture.c'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
