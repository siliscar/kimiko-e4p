#!/usr/bin/env python3
"""中文：实际续读点击、反馈区域与返回来源回归。/ English: Actual continue-card hits, bounded feedback and reader return origins.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
from run_reader_baseline import toolchain
root=Path(__file__).resolve().parents[1]
home=root/'main/apps/app_dashboard.c';book=root/'main/apps/app_book.c'
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "home_components.h"
#include "weather_service.h"
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int now_ms,leaf;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL} app_redraw_t;
enum {HOME_ROOT,HOME_COMPONENT,HOME_GOAL,HOME_TIME,READING,SHELF};
enum {UI_GESTURE_PRESS,UI_GESTURE_MOVE,UI_GESTURE_TAP,UI_GESTURE_LONG_PRESS,UI_GESTURE_SWIPE_L,UI_GESTURE_SWIPE_R,UI_GESTURE_SWIPE_U,UI_GESTURE_CANCEL};
typedef struct {int type,x0,y0,x,y;} ui_gesture_event_t;
#define ESP_OK 0
static int s_view=HOME_ROOT;
static bool s_press_pending;
static EpdRect s_pressed,s_area;
#define UI_NAV_TOP 1112
#define HOME_REGION_TOP 168
#define HOME_REGION_HEIGHT 472
#define WEATHER_REGION_WIDTH 684
#define WEATHER_REGION_HEIGHT 472
static app_home_widget_t slots[2]={APP_HOME_WIDGET_READING,APP_HOME_WIDGET_EMPTY};
app_home_widget_t app_home_component_at(unsigned slot){assert(slot<2);return slots[slot];}
static bool s_weather_tomorrow[2];
static unsigned s_widget_dirty;
static app_weather_view_t weather_view;
static const app_weather_view_t *app_weather_view_stub(void){return &weather_view;}
#define app_weather_view app_weather_view_stub
static void app_components_open(app_ctx_t *ctx,bool city){(void)ctx;(void)city;}
static bool app_weather_request_stub(app_weather_request_t type,const char *query){(void)query;assert(type==APP_WEATHER_FETCH);return true;}
#define app_weather_request app_weather_request_stub
typedef enum {UI_WEATHER_NONE,UI_WEATHER_CITY,UI_WEATHER_TODAY,UI_WEATHER_TOMORROW,UI_WEATHER_REFRESH,UI_WEATHER_SETTINGS} ui_weather_action_t;
static int epd_rotated_display_width(void){return 684;}
static int epd_rotated_display_height(void){return 1216;}
typedef struct {char path[32];} home_book_t;
static home_book_t book={"/sdcard/books/Novel/a.epub"};
static home_book_t *featured(void){return &book;}
static int open_calls,nav=-1;static bool fail_open;
static int ui_nav_hit(int x,int y){(void)x;return y>=1096?2:-1;}
static void ui_nav_request(app_ctx_t *ctx,int tab){(void)ctx;nav=tab;}
static bool app_book_request_open_from_home(const char *path){assert(!strcmp(path,book.path));++open_calls;return !fail_open;}
static EpdRect ui_reading_journal_art(EpdRect r){return(EpdRect){438,r.y+78,210,162};}
static bool app_reading_journal_next_scene(void){return true;}
static void app_reading_journal_open(app_ctx_t *ctx){(void)ctx;}
'''
unit+='\n'.join(function(n,root/'main/ui/ui_weather.c') for n in ['valid_region','refresh_button','ui_weather_hit'])
unit+='\n'.join(function(n,home) for n in ['home_region','continue_card_rect','continue_card_hit_rect','continue_card_pressed','control_feedback_area','redraw_widgets','hit','control','on_gesture'])
settings=(root/'main/settings/settings.h').read_text();a=settings.index('typedef enum {\n    APP_READER_KEY_PREV');b=settings.index('} app_reader_key_action_t;',a)+len('} app_reader_key_action_t;');unit+=settings[a:b]
unit+=r'''
enum {READER_PANEL_NONE,READER_PANEL_TOOLS};
static bool s_reader_return_home;static int s_reader_panel,s_reader_slider,s_stats_activity_ms;
static bool s_reader_tools_toggle;
static struct {bool pending;} s_reader_tap;
// 合并后的提取函数不再读 s_reader_tap；引用一次避免 -Wunused。
// The extracted functions no longer read s_reader_tap; reference it once against -Wunused.
static void consume_s_reader_tap(void){(void)s_reader_tap;}
static int saved,freed,restored,shelf_page=4,shelf_category=3;
static void save_progress(void){++saved;}
static void free_book(void){++freed;}
static void return_to_cached_shelf(app_ctx_t *ctx){++restored;ctx->leaf=shelf_page;assert(shelf_category==3);}
static app_redraw_t turn_page(app_ctx_t *ctx,int d){(void)ctx;(void)d;return APP_REDRAW_AREA;}
static app_redraw_t toggle_reader_fullscreen(app_ctx_t *ctx){(void)ctx;return APP_REDRAW_PAGE;}
static app_redraw_t reader_manual_refresh(app_ctx_t *ctx){(void)ctx;return APP_REDRAW_FULL;}
static void invalidate_prep(void){}
static bool s_fl_on;
static int s_fl_applies;
static bool frontlight_ready(void){return true;}
static bool app_settings_frontlight_on(void){return s_fl_on;}
static void app_settings_set_frontlight_on(bool on){s_fl_on=on;}
static unsigned char app_settings_frontlight_brightness(void){return 40;}
static unsigned char app_settings_frontlight_cct(void){return 50;}
static int frontlight_apply(bool on,unsigned char brightness,unsigned char cct){
 (void)brightness;(void)cct;s_fl_on=on;++s_fl_applies;return 0;
}
'''
unit+='\n'.join(function(n,book) for n in ['reader_return','toggle_reader_tools_panel','reader_key_action','reader_key_label'])
unit+=r'''
int main(void){app_ctx_t ctx={0};consume_s_reader_tap();
 // 只有 NONE↔TOOLS 是纯开关；从设置卡片切过来必须走整页差分。
 // Only NONE↔TOOLS is a pure toggle; switching in from a settings sheet keeps the page diff.
 s_reader_panel=READER_PANEL_NONE;s_reader_tools_toggle=false;toggle_reader_tools_panel();
 assert(s_reader_panel==READER_PANEL_TOOLS&&s_reader_tools_toggle);
 s_reader_panel=2;s_reader_tools_toggle=false;toggle_reader_tools_panel();
 assert(s_reader_panel==READER_PANEL_TOOLS&&!s_reader_tools_toggle);
 s_reader_panel=READER_PANEL_TOOLS;s_reader_tools_toggle=false;toggle_reader_tools_panel();
 assert(s_reader_panel==READER_PANEL_NONE&&s_reader_tools_toggle);
 int points=0,layouts=0,reading_slots=0;
 for(unsigned a=0;a<4;++a)for(unsigned b=0;b<4;++b){
  if(a==b&&a!=APP_HOME_WIDGET_EMPTY)continue;
  slots[0]=a;slots[1]=b;++layouts;
  for(unsigned slot=0;slot<2;++slot){
   if(slots[slot]!=APP_HOME_WIDGET_READING)continue;
   ++reading_slots;int top=home_region(slot).y;
   for(int y=top+72;y<top+436;y+=17)for(int x=24;x<660;x+=17){
    s_view=HOME_ROOT;nav=-1;int old=open_calls;
    ui_gesture_event_t ev={UI_GESTURE_PRESS,x,y,x,y};
    assert(on_gesture(&ctx,&ev)==APP_REDRAW_AREA&&continue_card_pressed(slot)&&!continue_card_pressed(1-slot));
    assert(s_area.x==32&&s_area.y==top+80&&s_area.width==186&&s_area.height==250);
    ev.type=UI_GESTURE_TAP;assert(on_gesture(&ctx,&ev)==APP_REDRAW_NONE);
    assert(open_calls==old+1&&nav==1&&!s_press_pending);++points;
   }
   ui_gesture_event_t ev={UI_GESTURE_PRESS,400,top+180,400,top+180};on_gesture(&ctx,&ev);
   ev.type=UI_GESTURE_MOVE;ev.x=670;assert(on_gesture(&ctx,&ev)==APP_REDRAW_AREA&&!s_press_pending);
   int old=open_calls;ev.type=UI_GESTURE_SWIPE_U;on_gesture(&ctx,&ev);assert(open_calls==old);
   ev=(ui_gesture_event_t){UI_GESTURE_PRESS,400,top+180,400,top+180};on_gesture(&ctx,&ev);
   ev.type=UI_GESTURE_CANCEL;assert(on_gesture(&ctx,&ev)==APP_REDRAW_AREA&&!s_press_pending&&open_calls==old);
   ev.type=UI_GESTURE_PRESS;on_gesture(&ctx,&ev);ev.type=UI_GESTURE_SWIPE_U;
   assert(on_gesture(&ctx,&ev)==APP_REDRAW_AREA&&!s_press_pending&&open_calls==old);
   book.path[0]=0;ev.type=UI_GESTURE_PRESS;assert(on_gesture(&ctx,&ev)==APP_REDRAW_NONE);
   ev.type=UI_GESTURE_TAP;nav=-1;on_gesture(&ctx,&ev);assert(nav==-1&&open_calls==old);
   strcpy(book.path,"/sdcard/books/Novel/a.epub");fail_open=true;ev.type=UI_GESTURE_PRESS;on_gesture(&ctx,&ev);
   ev.type=UI_GESTURE_TAP;assert(on_gesture(&ctx,&ev)==APP_REDRAW_AREA&&nav==-1);fail_open=false;
  }
 }
 assert(layouts==13&&reading_slots==6);
 assert(!strcmp(reader_key_label(APP_READER_KEY_HOME),"返回主页"));
 assert(!strcmp(reader_key_label(APP_READER_KEY_FRONTLIGHT),"前光开关"));
 // 前光开关只驱动灯，不重画正文；再按一次关灯。
 // The frontlight toggle only drives the light and requests no redraw; a second press turns it off.
 s_fl_on=false;s_fl_applies=0;
 assert(reader_key_action(&ctx,APP_READER_KEY_FRONTLIGHT)==APP_REDRAW_NONE&&s_fl_on&&s_fl_applies==1);
 assert(reader_key_action(&ctx,APP_READER_KEY_FRONTLIGHT)==APP_REDRAW_NONE&&!s_fl_on&&s_fl_applies==2);
 for(int via_key=0;via_key<2;++via_key)for(int from_home=0;from_home<2;++from_home){
  s_view=READING;s_reader_panel=READER_PANEL_TOOLS;s_reader_return_home=from_home;
  nav=-1;ctx.leaf=99;int before=freed;
  app_redraw_t result=via_key?reader_key_action(&ctx,APP_READER_KEY_HOME):reader_return(&ctx);
  if(from_home)assert(result==APP_REDRAW_NONE&&nav==0&&freed==before);
  else assert(result==APP_REDRAW_PAGE&&nav==-1&&s_view==SHELF&&ctx.leaf==4&&freed==before+1&&s_reader_panel==READER_PANEL_NONE);
 }
 assert(saved==4&&restored==2);
 printf("PASS: 13 unique layouts, six reading placements, %d hit points, fixed cover-feedback bounds, move/cancel/swipe/empty/open-failure guards; screen/key/hold shared return routes retain home or shelf category/page; renamed label\n",points);
}
'''
with tempfile.TemporaryDirectory(prefix='kiiko-reading-origin-',dir='/private/tmp') as folder:
 c=Path(folder)/'test.c';binary=Path(folder)/'test';c.write_text(unit)
 env,compiler=toolchain(Path(folder))
 subprocess.run(compiler+['-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-Imain/settings','-Imain/services','-g',str(c),'-o',str(binary)],cwd=root,env=env,check=True)
 subprocess.run([str(binary)],env=env,check=True)
