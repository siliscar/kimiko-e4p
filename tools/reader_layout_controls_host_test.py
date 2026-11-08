#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
# 中文：四向边距输入/几何与扩展缩进编码，覆盖非对称组合和旧配置。
# English: Four-side margin input/geometry and extended indent encoding, across asymmetric combinations and legacy configurations.
from pathlib import Path
import subprocess,tempfile,re
from home_cover_cache_host_test import function
r=Path(__file__).resolve().parents[1]
f=r/'main/apps/app_book.c'
u=r'''
#define READER_WIDTH 684
#define READER_HEIGHT 1216
#define READER_BAR_TOP 1096
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#define UI_LOCK_WIDTH 684
#define UI_BAR_TOP 1120
#define UI_BAR_H 96
#define READER_FULLSCREEN_PROGRESS_TOP 1198
#define UI_LOCK_HEIGHT 1216
#define READER_FOOTER_PROGRESS_SPACE 19
#define BOOK_MARGIN_MIN 0
#define BOOK_MARGIN_MAX 96
#define BOOK_MARGIN_STEP 6
#define BOOK_MARGIN_CHOICES 17
enum { MARGIN_LEFT, MARGIN_RIGHT, MARGIN_TOP, MARGIN_BOTTOM, MARGIN_COUNT };
typedef struct {int x,y,width,height;} EpdRect;
static bool s_reader_fullscreen;
static bool s_reader_progress_bar=true;
static int s_margin[MARGIN_COUNT];
static bool app_settings_reader_statusbar(void){return true;}
static int s_chrome_height[2]={80,57};
static unsigned s_book_indent_adjust=20;
static void nvs_put_u8(const char *key,uint8_t v){(void)key;assert(v==s_book_indent_adjust);}
#define NVS_KEY_BOOK_INDENT_ADJUST "bk_indent_adj"
'''
settings=(r/'main/settings/settings.h').read_text()
for name in ('APP_BOOK_TRACK_MIN','APP_BOOK_TRACK_MAX','APP_BOOK_TRACK_COUNT','APP_BOOK_PARAGRAPH_COUNT'):
    u+=re.search(r'^#define '+name+r' .*',settings,re.M)[0]+'\n'
for name in ('app_settings_book_paragraph_value','app_settings_book_paragraph_level'):
    u+=function(name,r/'main/settings/settings.c')+'\n'
u+=function('slider_index',f)+'\n'
for n in ('reader_footer_height','body_rect_for_tracking','reader_margin_level','reader_margin_value','reader_margin_input'):
    u+=function(n,f)+'\n'
for n in ('app_settings_book_indent_adjust','app_settings_set_book_indent_adjust'):
    u+=function(n,r/'main/settings/settings.c')+'\n'
u+=r'''
int main(void){
 const uint8_t stops[]={0,5,10,15,20,25,30,40,50,75,100};
 EpdRect slider={156,0,396,50};
 assert(APP_BOOK_PARAGRAPH_COUNT==11&&APP_BOOK_TRACK_COUNT==31);
 int midpoint=slider.x+(slider.width-1)/2;
 assert(app_settings_book_paragraph_value(slider_index(slider,midpoint,APP_BOOK_PARAGRAPH_COUNT))==25);
 assert(slider_index(slider,midpoint,APP_BOOK_TRACK_COUNT)+APP_BOOK_TRACK_MIN==0);
 for(unsigned i=0;i<11;++i){
  int x=slider.x+64+(i*(slider.width-128)+5)/10;
  assert(app_settings_book_paragraph_value(slider_index(slider,x,11))==stops[i]);
  assert(app_settings_book_paragraph_level(stops[i])==i);
 }
 for(int px=-15;px<=15;++px){
  int x=slider.x+64+((px+15)*(slider.width-128)+15)/30;
  assert(slider_index(slider,x,31)+APP_BOOK_TRACK_MIN==px);
 }

 // 四向边距共用固定十七档，就近吸附到 6 px 台阶。
 // All four sides share the fixed 17-step ladder and snap to the nearest 6 px stop.
 assert(BOOK_MARGIN_CHOICES==17);
 for(int i=0;i<17;++i){
  assert(reader_margin_value(i)==i*6&&reader_margin_level(i*6)==i);
  if(i>0)assert(reader_margin_level(i*6+2)==i);
  if(i<16)assert(reader_margin_level(i*6+4)==i+1);
 }
 assert(reader_margin_level(-7)==0&&reader_margin_level(100)==16);
 assert(reader_margin_value(-3)==0&&reader_margin_value(99)==96);
 EpdRect rail={36,540,294,66};
 assert(reader_margin_input(rail,rail.x+10,24)==18);
 assert(reader_margin_input(rail,rail.x+rail.width-1,24)==30);
 assert(reader_margin_input(rail,rail.x+64,24)==0);
 assert(reader_margin_input(rail,rail.x+rail.width-64,24)==30);
 assert(reader_margin_input(rail,rail.x+10,0)==0);
 assert(reader_margin_input(rail,rail.x+rail.width-1,96)==96);
 assert(reader_margin_input(rail,rail.x+10,96)==90);

 // 四向内缩互不牵连：横排竖排同一公式，左大右小也不会互相顶替。
 // The four insets stay independent: both writing modes share one formula, and a large left
 // margin never stands in for a small right one.
 for(int full=0;full<2;++full){
  s_reader_fullscreen=full;
  const int top=full?96:176,bottom=full?1196:1159;
  const int lefts[]={0,12,24,48,96},rights[]={0,24,48,96};
  const int tops[]={0,18,48,96},bottoms[]={0,30,48,96};
  for(unsigned li=0;li<5;++li)for(unsigned ri=0;ri<4;++ri)
   for(unsigned ti=0;ti<4;++ti)for(unsigned bi=0;bi<4;++bi){
    s_margin[MARGIN_LEFT]=lefts[li];s_margin[MARGIN_RIGHT]=rights[ri];
    s_margin[MARGIN_TOP]=tops[ti];s_margin[MARGIN_BOTTOM]=bottoms[bi];
    EpdRect a=body_rect_for_tracking(2);
    assert(a.x==s_margin[MARGIN_LEFT]);
    assert(a.y==top+s_margin[MARGIN_TOP]);
    assert(a.width==684-s_margin[MARGIN_LEFT]-s_margin[MARGIN_RIGHT]);
    assert(a.height==bottom-top-s_margin[MARGIN_TOP]-s_margin[MARGIN_BOTTOM]);
   }
 }
 for(int i=-100;i<=100;++i){app_settings_set_book_indent_adjust(i);assert(app_settings_book_indent_adjust()==i&&s_book_indent_adjust<=200);}
 for(int old=0;old<=40;++old){s_book_indent_adjust=old;assert(app_settings_book_indent_adjust()==old-20);}
 puts("PASS: centered paragraph/tracking ladders, four independent margin insets in both orientations, 6 px snapping to 96, endpoint stepping, all 201 indent adjustments and legacy encoding");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.c';p.write_text(u);out=p.with_suffix('')
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined',str(p),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
