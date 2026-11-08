"""Actual settings row culling/arrow glyph independence and reader clock refresh.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1]
settings=ROOT/'main/apps/app_device_settings.c'; book=ROOT/'main/apps/app_book.c'
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#define UI_NAV_TOP 1096
#define UI_LOCK_WIDTH 684
#define READER_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define UI_GRAY_WHITE 255
#define UI_GRAY_BLACK 0
#define SETTINGS_ROW_H settings_main_row_height()
#define SETTINGS_ICON_PX 32
#define UI_ICON_CHEVRON_RIGHT 7
#define EPD_DRAW_ALIGN_RIGHT 1
#define EPD_DRAW_ALIGN_LEFT 0
#define EPD_DRAW_SUCCESS 0
enum EpdDrawMode {MODE_GL16=1};
#define E0470_WAVEFORM 1
#define ESP_LOGI(...) ((void)0)
typedef struct {int x,y,width,height;}EpdRect;
typedef int app_redraw_t;
enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL};
enum {READING,SHELF};
typedef struct {bool touched;}touch_t;
typedef struct {touch_t *touch;uint8_t *fb;void *hl;}app_ctx_t;
static unsigned scale=100,text_calls,icons,arrows,measures,profile_calls;
static int s_main_scroll,last_label_y;
static unsigned pairs;
static int ui_text_effective_px(int px){return (px+2)*scale/100;}
static int ui_text_fixed_width_px(int px,const char *value){++measures;return (int)strlen(value)*px/3;}
static void ui_text(uint8_t *fb,int x,int y,int px,const char *text,int align,bool inv){(void)fb;(void)x;(void)y;(void)px;(void)align;(void)inv;assert(!strstr(text,"›"));++text_calls;}
static void ui_text_vc(uint8_t *fb,int x,int y,int px,const char *text,int align,bool inv){if(x==100)last_label_y=y;if(x==594){assert(y==last_label_y);++pairs;}ui_text(fb,x,y,px,text,align,inv);}
static void ui_draw_icon(uint8_t *fb,int x,int y,int px,int icon,int gray){(void)fb;(void)x;(void)y;(void)px;(void)gray;assert(icon==UI_ICON_CHEVRON_RIGHT);++arrows;}
static void setting_icon(uint8_t *fb,int index,int x,int y){(void)fb;(void)index;(void)x;(void)y;++icons;}
static void settings_card(uint8_t *fb,EpdRect r,int radius,int fill,int edge){(void)fb;(void)r;(void)radius;(void)fill;(void)edge;}
static void settings_divider(uint8_t *fb,int y,int x,int width){(void)fb;(void)y;(void)x;(void)width;}
'''
for name in ('settings_main_row_height','settings_fit_text','fit_value','settings_value','setting_group'):
 unit+=function(name,settings)+'\n'
unit+=r'''
static int s_view=READING;
static char *s_text="book";
static bool s_reader_fullscreen,statusbar=true,wallpaper;
static int64_t s_reader_clock_minute=-1,minute=100;
static int s_reader_power=-1;
static uint32_t chrome=1,s_reader_chrome_fp;
static bool s_reader_chrome_fp_valid;
typedef struct {int percent;bool charging;}pmu_snapshot_t;
static pmu_snapshot_t power={.percent=-1};
static const pmu_snapshot_t *device_power_get(void){return &power;}
static int pmu_battery_percent(const pmu_snapshot_t *p){return p->percent;}
static bool pmu_battery_charging(const pmu_snapshot_t *p){return p->charging;}
enum {READER_PANEL_NONE,READER_PANEL_BACKGROUND_PREVIEW};
static int s_reader_panel;
static uint8_t fb[684*1216],next_fb[684*1216];
static bool locked;
static unsigned status_paints,background_paints,header_paints;
// Clock-band height follows the mode: 80 for full screen or the top-hugging header, 160 when
// the windowed header joins the status bar. Assertions track the painted band, not a constant.
static int expected_band(void){return !s_reader_fullscreen&&statusbar?160:80;}
static bool app_settings_reader_statusbar(void){return statusbar;}
static int s_chrome_height[2]={80,57};
static const char *app_settings_book_background_path(void){return wallpaper?"/sdcard/pictures/bg.jpg":"";}
static void lock_draw(void){assert(!locked);locked=true;}
static void unlock_draw(void){assert(locked);locked=false;}
static void epd_fill_rect(EpdRect r,int color,uint8_t *frame){assert(r.x==0&&r.y==0&&r.width==684&&r.height==expected_band());memset(frame,color,684*expected_band());}
static void ui_fill_rect_fast(EpdRect r,int color,uint8_t *frame){epd_fill_rect(r,color,frame);}
static void ui_nav_status(uint8_t *frame){assert(statusbar);++status_paints;frame[20*684+36]=(uint8_t)minute;}
static void draw_reader_header(uint8_t *frame){(void)frame;++header_paints;}
static void ui_reader_background_paint(uint8_t *frame,EpdRect r,bool blend){assert(r.height==expected_band()&&blend);++background_paints;frame[0]=29;}
enum EpdDrawError {OK=0,FAULT=1};
static EpdRect s_area;
static EpdRect reader_area(void);
static bool s_reader_cleanup,s_water_turn_pending,s_reader_turn_pending,s_reader_image_refresh_pending,s_toolbar,s_clear_confirm,s_reader_footer_pending;
'''
for name in ('reader_status_area','reader_status_visible','reader_status_paint','reader_status_power','reader_status_prepare'):
 code=function(name,book)
 if name=='reader_status_prepare':code=code.replace('reader_status_prepare(', 'reader_status_prepare_actual(', 1)
 unit+=code+'\n'
unit+='static bool reader_status_prepare(app_ctx_t *ctx,app_redraw_t redraw,int64_t minute,int power){return reader_status_prepare_actual(ctx,redraw,minute,power,chrome);}\n'
present=function('present',book)
ack=present[present.index('    if (err==EPD_DRAW_SUCCESS && reader_status_visible()'):present.index('    const bool reader_frame_ok')]
unit+='static void frame_ack(int err,app_redraw_t redraw,bool tools_band,bool status_updated,int64_t frame_minute,int frame_power){\nuint32_t frame_chrome=chrome;struct {bool status_updated;} output={status_updated};\n'+ack+'}\n'
# Actual prefetched-page path; freshness is checked after copying, before any push.
unit+=r'''
static uint8_t *s_page_images;
static const uint8_t *reader_prefetch_frame(size_t page){assert(page==2);return next_fb;}
static bool reader_prefetch_resume(size_t page){(void)page;return false;}
static void book_layout_adopt_page(size_t page){assert(locked&&page==2);}
static void draw_reader_outer(uint8_t *frame,size_t page){assert(locked&&frame==fb&&page==2);}
static int s_mode;
static int64_t esp_timer_get_time(void){return 1000;}
static bool ui_reader_background_prepare(const char *p){(void)p;return true;}
static void prepare_inline_image(void){}
static bool app_settings_reader_hide_images(void){return false;}
static int book_layout_page_image_count(int p){(void)p;return 0;}
static size_t fb_bytes(void){return sizeof(fb);}
static void draw_reader(uint8_t *f,int p){(void)f;(void)p;assert(false);}
static EpdRect reader_area(void){return (EpdRect){0,160,684,900};}
'''
unit+=function('paint_reading',book)+'\n'
files=ROOT/'main/apps/app_files.c'
unit+=r'''
typedef struct {bool mounted,space_valid;uint64_t filesystem_bytes,free_bytes,capacity_bytes;}board_sd_info_t;
'''
for name in ('storage_space_valid','storage_amount','storage_usage'):unit+=function(name,files)+'\n'
unit+=r'''
int main(void){
 uint8_t pixels[1]={0};const int marks[]={0,1,2,3,4,5};
 const char *labels[]={"WiFi","系统字体","系统对比度","主页刷新模式","自动休眠锁屏","关机睡眠"};
 const char *values[]={"很长的网络名称","用户导入的非常长的字体名称","100%","水波纹","10 分钟","先浅后深"};
 for(scale=100;scale<=200;scale+=10){
  assert(SETTINGS_ROW_H==82);pairs=0;
  text_calls=icons=arrows=measures=0;s_main_scroll=0;
  setting_group(pixels,1600,"hidden",1649,marks,labels,values,6);
  assert(!text_calls&&!icons&&!arrows&&!measures);
  setting_group(pixels,300,"visible",349,marks,labels,values,6);
  unsigned visible=(1096-349+SETTINGS_ROW_H-1)/SETTINGS_ROW_H;
  if(visible>6)visible=6;
  assert(icons==visible&&arrows==visible&&text_calls==1+visible*2&&pairs==visible);
  s_main_scroll=1500;text_calls=icons=arrows=measures=0;
  setting_group(pixels,300,"hidden",349,marks,labels,values,6);
  assert(!text_calls&&!icons&&!arrows&&!measures);
 }
 scale=100;arrows=0;settings_value(pixels,627,200,21,"network  ›");assert(arrows==1);
 settings_value(pixels,627,200,21,"›");assert(arrows==2);
 settings_value(pixels,627,200,21,"network");assert(arrows==2);
 app_ctx_t ctx={.fb=fb};memset(fb,77,sizeof(fb));
 // 静置期间分钟、电量变化不绘图，翻页时才准备；确认必须来自成功的实际顶部输出。
 // Idle time/power changes do not paint; only turns prepare status and successful top output acknowledges it.
 for(int idle=0;idle<8;++idle){
  ++minute;power.percent=60+idle;power.charging=idle&1;
  assert(!reader_status_prepare(&ctx,APP_REDRAW_NONE,minute,reader_status_power()));
  assert(!reader_status_prepare(&ctx,APP_REDRAW_AREA,minute,reader_status_power()));
 }
 assert(!status_paints&&!header_paints&&fb[0]==77&&s_reader_clock_minute==-1&&s_reader_power==-1);
 s_area=reader_area();s_reader_turn_pending=true;
 assert(reader_status_prepare(&ctx,APP_REDRAW_AREA,minute,reader_status_power()));
 assert(fb[159*684]==255&&fb[160*684]==77&&fb[20*684+36]==minute);
 int power_value=reader_status_power();
 frame_ack(FAULT,APP_REDRAW_AREA,false,true,minute,power_value);
 assert(s_reader_power==-1&&s_reader_clock_minute==-1);
 assert(reader_status_prepare(&ctx,APP_REDRAW_AREA,minute,power_value));
 frame_ack(OK,APP_REDRAW_AREA,false,false,minute,power_value);assert(s_reader_power==-1);
 frame_ack(OK,APP_REDRAW_AREA,true,true,minute,power_value);assert(s_reader_power==-1);
 frame_ack(OK,APP_REDRAW_AREA,false,true,minute,power_value);assert(s_reader_power==power_value&&s_reader_clock_minute==minute);
 assert(!reader_status_prepare(&ctx,APP_REDRAW_AREA,minute,power_value));
 // 页码槽位变化单独触发顶部，成功输出前不确认指纹。/ Page-slot changes alone refresh the top; acknowledge only successful output.
 ++chrome;assert(reader_status_prepare(&ctx,APP_REDRAW_AREA,minute,power_value));
 frame_ack(FAULT,APP_REDRAW_AREA,false,true,minute,power_value);
 assert(s_reader_chrome_fp!=chrome&&reader_status_prepare(&ctx,APP_REDRAW_AREA,minute,power_value));
 frame_ack(OK,APP_REDRAW_AREA,false,true,minute,power_value);
 assert(s_reader_chrome_fp==chrome&&!reader_status_prepare(&ctx,APP_REDRAW_AREA,minute,power_value));
 // 电量未知恢复、百分比和充电状态独立触发下一次翻页重画，不需分钟变化。
 // Unknown recovery, percentage and charging changes independently prepare the next turn within the same minute.
 for(int pct=-1;pct<=68;pct+=23)for(int charging=0;charging<2;++charging){
  power.percent=pct;power.charging=charging;
  assert(reader_status_prepare(&ctx,APP_REDRAW_AREA,minute,reader_status_power()));
  frame_ack(OK,APP_REDRAW_AREA,false,true,minute,reader_status_power());
  assert(!reader_status_prepare(&ctx,APP_REDRAW_AREA,minute,reader_status_power()));
 }
 ++minute;s_reader_panel=READER_PANEL_BACKGROUND_PREVIEW;
 assert(!reader_status_prepare(&ctx,APP_REDRAW_AREA,minute,reader_status_power()));s_reader_panel=READER_PANEL_NONE;
 s_toolbar=true;assert(!reader_status_prepare(&ctx,APP_REDRAW_AREA,minute,reader_status_power()));s_toolbar=false;
 s_clear_confirm=true;assert(!reader_status_prepare(&ctx,APP_REDRAW_AREA,minute,reader_status_power()));s_clear_confirm=false;
 s_reader_cleanup=true;assert(!reader_status_prepare(&ctx,APP_REDRAW_AREA,minute,reader_status_power()));
 frame_ack(OK,APP_REDRAW_AREA,false,false,minute,reader_status_power());assert(s_reader_clock_minute==minute);s_reader_cleanup=false;
 ++minute;frame_ack(FAULT,APP_REDRAW_PAGE,false,false,minute,reader_status_power());assert(s_reader_clock_minute!=minute);
 frame_ack(OK,APP_REDRAW_PAGE,false,false,minute,reader_status_power());assert(s_reader_clock_minute==minute);
 ++minute;frame_ack(OK,APP_REDRAW_FULL,false,false,minute,reader_status_power());assert(s_reader_clock_minute==minute);
 // 各全屏/状态栏组合保留80/160px边界；背景重画不越入正文。
 // All fullscreen/status-bar combinations preserve 80/160px bounds and background painting stays above body.
 for(int full=0;full<2;++full)for(int bar=0;bar<2;++bar)for(int bg=0;bg<2;++bg){
  s_reader_fullscreen=full;statusbar=bar;wallpaper=bg;++minute;memset(fb,77,sizeof(fb));
  unsigned before=header_paints,bg_before=background_paints;
  bool visible=bar||!full;
  s_area=reader_area();
  assert(reader_status_prepare(&ctx,APP_REDRAW_AREA,minute,reader_status_power())==visible);
  if(visible){
   assert(fb[expected_band()*684]==77&&fb[(expected_band()-1)*684]==255);
   assert(header_paints==before+!full&&background_paints==bg_before+bg);
   assert(fb[0]==(bg?29:255));
   frame_ack(OK,APP_REDRAW_AREA,false,true,minute,reader_status_power());
  }else assert(fb[0]==77&&header_paints==before);
 }
 // 全屏图页返回文字不依赖纯文字效果标记；局部设置区域不能触发顶部。
 // Image-to-text full-screen turns need no pure-text effect flag; local settings regions never prepare status.
 s_reader_fullscreen=true;statusbar=true;++minute;
 s_reader_turn_pending=s_water_turn_pending=s_reader_image_refresh_pending=s_reader_footer_pending=false;
 s_area=reader_area();assert(reader_status_prepare(&ctx,APP_REDRAW_AREA,minute,reader_status_power()));
 s_area.height=20;assert(!reader_status_prepare(&ctx,APP_REDRAW_AREA,minute,reader_status_power()));
 s_reader_fullscreen=false;statusbar=true;wallpaper=false;
 memset(next_fb,15,sizeof(next_fb));next_fb[20*684+36]=1;minute=110;
 assert(paint_reading(&ctx,MODE_GL16)==APP_REDRAW_AREA);
 assert(fb[20*684+36]==110&&fb[160*684]==15&&fb[159*684]==255&&s_area.y==160);
 assert(reader_status_prepare(&ctx,APP_REDRAW_AREA,minute,reader_status_power()));
 frame_ack(OK,APP_REDRAW_AREA,false,true,minute,reader_status_power());
 s_view=SHELF;++minute;assert(!reader_status_prepare(&ctx,APP_REDRAW_AREA,minute,reader_status_power()));
 board_sd_info_t info={.mounted=true,.space_valid=true,.filesystem_bytes=128ULL*1024*1024*1024,.free_bytes=100ULL*1024*1024*1024,.capacity_bytes=256ULL*1024*1024*1024};
 char total[32],usage[96];storage_usage(&info,total,sizeof(total),usage,sizeof(usage));
 assert(!strcmp(total,"128.00 GB")&&strstr(usage,"28.00 GB")&&strstr(usage,"100.00 GB"));
 info.free_bytes=info.filesystem_bytes-1024*1024;
 storage_usage(&info,total,sizeof(total),usage,sizeof(usage));assert(strstr(usage,"1.0 MB"));
 info.free_bytes=info.filesystem_bytes+1;assert(!storage_space_valid(&info));
 info.space_valid=false;storage_usage(&info,total,sizeof(total),usage,sizeof(usage));assert(strstr(usage,"暂不可用")&&!strstr(usage,"256.00 GB"));
 info.space_valid=true;info.free_bytes=0;assert(storage_space_valid(&info));
 puts("PASS: visible-only settings glyph work, missing-chevron fonts, 11 scales, turn-coupled status/no idle paint/statusbar/background/error retry, fresh prefetch, large partition/small-file/invalid-space display");
}
'''
# Render may not decode/read NVS, and clock prefix may not prepare/cache whole pages.
s=function('render',settings)
assert 'ui_wallpaper_draw_rounded' not in s and 'wifi_transfer_get_saved_wifi' not in s
prepare=function('reader_status_prepare',book)
assert 'prepare_inline_image' not in prepare and 'kick_prep' not in prepare
assert 'reader_clock_tick' not in function('on_tick',book)
assert 'reader_status_prepare' not in function('on_tick',book)
from reader_session_test_state import position_state
unit = position_state(page=2) + unit
with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
 d=Path(folder);c=d/'test.c';exe=d/'test';c.write_text(unit)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
