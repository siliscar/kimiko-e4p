"""Reader body taps: an open panel is dismissed, never turned through.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
book = ROOT / 'main/apps/app_book.c'
source = book.read_text()

unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int64_t now_ms;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE} app_redraw_t;
enum {READING,SHELF,READER_PANEL_NONE,READER_PANEL_TOOLS,READER_PANEL_FONT_SETTINGS};
static int s_view=READING,s_reader_panel=READER_PANEL_NONE;
static bool s_clear_confirm,s_scan_pending;
static int panel_calls,turn_calls,last_turn;
static bool image_slots,vertical_turn,rtl_book;
static app_redraw_t reader_panel_action(app_ctx_t *ctx,uint16_t x,uint16_t y){(void)ctx;(void)x;(void)y;++panel_calls;return APP_REDRAW_PAGE;}
static app_redraw_t turn_page(app_ctx_t *ctx,int dir){(void)ctx;++turn_calls;last_turn=dir;return APP_REDRAW_PAGE;}
static EpdRect body_rect(void){return (EpdRect){36,160,612,900};}
static int reader_image_slots(int page){(void)page;return image_slots;}
static int book_layout_link_at(size_t page,int x,int y){(void)page;(void)x;(void)y;return -1;}
static bool note_open_link(int link){(void)link;return false;}
static EpdRect book_layout_image_rect(void){return (EpdRect){0,0,684,1216};}
static bool ui_rect_hit(EpdRect r,int x,int y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static EpdRect ui_rect_union(EpdRect a,EpdRect b){
 int x0=a.x<b.x?a.x:b.x,y0=a.y<b.y?a.y:b.y;
 int x1=(a.x+a.width)>(b.x+b.width)?(a.x+a.width):(b.x+b.width);
 int y1=(a.y+a.height)>(b.y+b.height)?(a.y+a.height):(b.y+b.height);
 return (EpdRect){x0,y0,x1-x0,y1-y0};
}
static bool app_settings_reader_vertical_turn(void){return vertical_turn;}
static bool app_settings_book_vertical(void){return rtl_book;}
'''
unit += re.search(r'^#define s_toolbar .*', source, re.M)[0] + '\n'
input_header = (ROOT / 'main/book/book_reader_input.h').read_text()
unit += '\n'.join(line for line in input_header.splitlines()
                  if not line.startswith(('#include', '#pragma'))) + '\n'

action = function('action_at', book)
start = action.index('    if (s_view == READING) {')
end = action.index('    if (s_view == SHELF) {', start)
branch = action[start:end]
unit += 'static app_redraw_t reading_tap(app_ctx_t *ctx,uint16_t x,uint16_t y){\n' + branch
unit += '    return APP_REDRAW_NONE;\n}\n'

unit += r'''
static void reset(void){
 s_view=READING;s_reader_panel=READER_PANEL_NONE;s_clear_confirm=s_scan_pending=false;
 s_page=0;image_slots=vertical_turn=rtl_book=false;panel_calls=turn_calls=last_turn=0;
}
int main(void){
 app_ctx_t ctx={0};
 reset();assert(reading_tap(&ctx,100,600)==APP_REDRAW_PAGE&&turn_calls==1&&last_turn==-1&&panel_calls==0);
 reset();assert(reading_tap(&ctx,500,600)==APP_REDRAW_PAGE&&turn_calls==1&&last_turn==1&&panel_calls==0);
 reset();vertical_turn=true;
 assert(reading_tap(&ctx,342,300)==APP_REDRAW_PAGE&&last_turn==-1&&turn_calls==1);
 reset();assert(reading_tap(&ctx,342,900)==APP_REDRAW_PAGE&&last_turn==1&&turn_calls==1);
 reset();s_reader_panel=READER_PANEL_TOOLS;
 assert(reading_tap(&ctx,100,600)==APP_REDRAW_PAGE&&panel_calls==1&&turn_calls==0);
 assert(reading_tap(&ctx,500,600)==APP_REDRAW_PAGE&&panel_calls==2&&turn_calls==0);
 assert(reading_tap(&ctx,342,1150)==APP_REDRAW_PAGE&&panel_calls==3&&turn_calls==0);
 assert(reading_tap(&ctx,342,60)==APP_REDRAW_PAGE&&panel_calls==4&&turn_calls==0);
 assert(reading_tap(&ctx,10,600)==APP_REDRAW_PAGE&&panel_calls==5&&turn_calls==0);
 reset();s_reader_panel=READER_PANEL_TOOLS;image_slots=true;
 assert(reading_tap(&ctx,10,600)==APP_REDRAW_PAGE&&panel_calls==1&&turn_calls==0);
 reset();image_slots=true;
 assert(reading_tap(&ctx,10,600)==APP_REDRAW_PAGE&&turn_calls==1&&last_turn==-1&&panel_calls==0);
 reset();s_reader_panel=READER_PANEL_FONT_SETTINGS;
 assert(reading_tap(&ctx,100,600)==APP_REDRAW_PAGE&&panel_calls==1&&turn_calls==0);
 reset();assert(reading_tap(&ctx,10,600)==APP_REDRAW_NONE&&panel_calls==0&&turn_calls==0);
 puts("PASS: reader taps turn only without a panel; tools and other panels dismiss without turning, including bar, header, margins and full-bleed image strips");
}
'''

assert 'y < 1096' not in action, 'the tools-bar body exception is back'
assert 'if (s_toolbar) return reader_panel_action(ctx, x, y);' in action

from reader_session_test_state import position_state
unit = position_state(page=0) + unit
with tempfile.TemporaryDirectory(dir=ROOT / 'build') as folder:
    path = Path(folder)
    (path / 'test.c').write_text(unit)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(path / 'test.c'),
                    '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
