#!/usr/bin/env python3
"""中文：组件库与城市页单按钮底栏、按需键盘及自动定位的真实回调回归。
English: Regress real callbacks for Home-only footers, on-demand keyboard and automatic location.
"""
from pathlib import Path
import re
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
PAGE = ROOT / "main/apps/app_components.c"
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "weather_service.h"
#include "home_components.h"
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {bool touched;int count,x,y;} touch_t;
typedef struct {uint8_t *fb;int64_t now_ms;bool request_return,consumed,released;touch_t *touch;const int *request_app;void *hl;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE} app_redraw_t;
enum EpdFontFlags {EPD_DRAW_ALIGN_LEFT,EPD_DRAW_ALIGN_CENTER,EPD_DRAW_ALIGN_RIGHT};
enum {UI_GESTURE_PRESS,UI_GESTURE_TAP,UI_GESTURE_SWIPE_L,UI_GESTURE_SWIPE_R,UI_GESTURE_CANCEL};
typedef struct {int type,x0,y0;} ui_gesture_event_t;
typedef enum {UI_KEYBOARD_NONE,UI_KEYBOARD_CHANGED,UI_KEYBOARD_DONE} ui_keyboard_result_t;
typedef struct {EpdRect area;bool layout,field,settle;} ui_keyboard_update_t;
enum {DISPLAY_REFRESH_FEEDBACK,DISPLAY_REFRESH_DETAIL,DISPLAY_REFRESH_SETTLE,DISPLAY_REFRESH_DEFAULT};
typedef int display_refresh_intent_t;
typedef enum {UI_WEATHER_NONE,UI_WEATHER_CITY,UI_WEATHER_TODAY,UI_WEATHER_TOMORROW,UI_WEATHER_REFRESH,UI_WEATHER_SETTINGS} ui_weather_action_t;
typedef int ui_icon_t;
enum {UI_ICON_HOUSE,UI_ICON_CHEVRON_LEFT,UI_ICON_CHEVRON_RIGHT,UI_ICON_RADIO_TOWER,UI_ICON_BOOK_OPEN,UI_ICON_SUN,UI_ICON_EYE_CLOSED,UI_ICON_BOOKMARK};
enum {UI_KEY_1,UI_KEY_2,UI_KEY_3};
#define UI_NAV_TOP 1112
#define WEATHER_REGION_WIDTH 684
#define WEATHER_REGION_HEIGHT 472
static bool s_requested_city,s_city,s_weather_settings,s_tomorrow,s_keyboard_visible,s_keyboard_bound;
static bool s_detail,s_settle;
static EpdRect s_area;
static ui_keyboard_update_t next_update={{24,612,636,114},false,false,false};
static int refresh_intent,refreshes;
static const int app_components=7;
static char s_query[129],s_notice[97];
static int s_edit,nav=-1,icons,cancels,ends,keys,begins,paints,pages,holds,idles,draws,requests,locates,searches,fields,location_controls;
static unsigned row_names,row_admins,selections;
static unsigned search_page;
static bool pending,request_ok=true,save_ok=true,poll_changed;
static bool idle_touch;
static ui_keyboard_result_t key_result;
static app_weather_view_t view;
static app_home_widget_t slots[2]={APP_HOME_WIDGET_READING,APP_HOME_WIDGET_WEATHER};
static bool layout_save_ok=true;
static int layout_writes,layout_inits,weather_inits,prepares;
void app_home_components_init(bool legacy){assert(legacy==view.enabled);++layout_inits;}
void app_weather_init(void){++weather_inits;}
bool ui_weather_prepare(void){++prepares;return true;}
app_home_widget_t app_home_component_at(unsigned slot){assert(slot<2);return slots[slot];}
bool app_home_component_set(unsigned slot,app_home_widget_t widget){
    assert(slot<2&&(unsigned)widget<4);++layout_writes;
    if(layout_save_ok){unsigned other=slot?0:1;if(widget!=APP_HOME_WIDGET_EMPTY&&slots[other]==widget)slots[other]=slots[slot];slots[slot]=widget;}
    return layout_save_ok;
}
bool app_home_has_widget(app_home_widget_t widget){return slots[0]==widget||slots[1]==widget;}
static int epd_rotated_display_width(void){return 684;}
static int epd_rotated_display_height(void){return 1216;}
const app_weather_view_t *app_weather_view(void){return &view;}
int64_t app_weather_now(void){return 1793579400;}
bool app_weather_request(app_weather_request_t type,const char *query){
    ++requests;if(type==APP_WEATHER_LOCATE){assert(!query);++locates;}
    if(type==APP_WEATHER_SEARCH){assert(query==s_query);++searches;}return request_ok;
}
bool app_weather_search(const char *query,unsigned page){assert(query==s_query);++searches;search_page=page;return request_ok;}
bool app_weather_set_enabled(bool enabled){view.enabled=enabled;return true;}
bool app_weather_set_auto_refresh(bool enabled){if(save_ok)view.auto_refresh=enabled;return save_ok;}
bool app_weather_select(const weather_location_t *city){++selections;if(save_ok)view.city=*city;return save_ok;}
void app_weather_cancel_join(void){++cancels;}
void app_weather_clear_search(void){view.result_count=view.result_total=view.result_page=0;view.notice[0]=0;}
bool app_weather_poll(bool automatic){assert(automatic==(!s_city&&app_home_has_widget(APP_HOME_WIDGET_WEATHER)));return poll_changed;}
bool weather_query_valid(const char *query){return strlen(query)>=2;}
static void ui_draw_separator(uint8_t *fb,int y,int x,int width,int gray){(void)fb;(void)y;(void)x;(void)width;(void)gray;}
static void ui_draw_icon(uint8_t *fb,int x,int y,int px,int icon,int gray){
    (void)fb;(void)x;(void)px;(void)gray;
    if(y==1164){++icons;assert(icon==UI_ICON_HOUSE&&x==342&&px==44);}
}
static void ui_nav_request(app_ctx_t *ctx,int tab){(void)ctx;nav=tab;}
static void ui_text_edit_init(int *edit,char *query,size_t size){assert(edit==&s_edit&&query==s_query&&size==sizeof(s_query));}
static void ui_keyboard_end(void){++ends;pending=false;}
static void ui_keyboard_begin(int *edit,bool ascii){assert(edit==&s_edit&&!ascii);++begins;pending=false;}
static bool ui_keyboard_pending(void){return pending;}
static void ui_keyboard_hold_start(int x,int y,int top,int64_t now){(void)x;(void)y;(void)top;(void)now;}
static bool ui_keyboard_press(int x,int y,int top,int64_t now){(void)x;(void)y;(void)top;(void)now;return false;}
static bool ui_keyboard_release(void){return false;}
static void ui_keyboard_hold_cancel(void){}
static bool ui_keyboard_page(int dir){assert(dir==1||dir==-1);++pages;return true;}
static ui_keyboard_result_t ui_keyboard_tap(int x,int y,int top,int64_t now){(void)x;(void)y;(void)top;(void)now;++keys;return key_result;}
static ui_keyboard_result_t ui_keyboard_hold_tick(bool held,int x,int y,int64_t now){(void)held;(void)x;(void)y;(void)now;++holds;return UI_KEYBOARD_NONE;}
static ui_keyboard_result_t ui_keyboard_idle_tick(bool held,int64_t now){(void)now;idle_touch=held;++idles;return UI_KEYBOARD_NONE;}
static bool ui_text_input_tap(int *edit,EpdRect rect,int px,bool white,void *data,int x,int y){(void)edit;(void)rect;(void)px;(void)white;(void)data;(void)x;(void)y;return true;}
static ui_keyboard_update_t ui_keyboard_update(uint8_t *fb,int top,EpdRect field,int px,bool masked,void *suffix,bool force){
    (void)fb;(void)force;assert(top==560&&field.x==36&&field.y==270&&field.width==480&&field.height==68&&px==28&&!masked&&!suffix);
    ++paints;return next_update;
}
static int display_refresh_area(void *hl,int intent,EpdRect area,unsigned flags){
    (void)hl;assert(area.x==s_area.x&&area.y==s_area.y&&area.width==s_area.width&&area.height==s_area.height&&flags==DISPLAY_REFRESH_DEFAULT);
    refresh_intent=intent;++refreshes;return 0;
}
static void guard_draw_result(void *hl,int result){(void)hl;assert(!result);}
static app_redraw_t edited(app_ctx_t *ctx){(void)ctx;return APP_REDRAW_PAGE;}
static void ui_clear_page(uint8_t *fb){(void)fb;row_names=row_admins=0;}
static void ui_nav_status(uint8_t *fb){(void)fb;}
static void ui_text(uint8_t *fb,int x,int y,int px,const char *value,enum EpdFontFlags align,bool white){(void)fb;(void)x;(void)y;(void)px;(void)value;(void)align;(void)white;}
static void text(uint8_t *fb,int x,int y,int px,const char *value,enum EpdFontFlags align){
    (void)fb;(void)x;(void)y;(void)px;(void)align;assert(!strstr(value,"重新识别"));
    if(!strcmp(value,"自动定位"))++location_controls;
    if(s_city&&x==48&&y>=350&&y<1022){
        assert(!s_keyboard_visible);
        if(px==36){assert(y==376+(int)row_names*84);++row_names;}
        else {assert(px==24&&y==412+(int)row_admins*84);++row_admins;}
    }
}
static void fitted(uint8_t *fb,int x,int y,int px,const char *value,int width){(void)width;text(fb,x,y,px,value,EPD_DRAW_ALIGN_LEFT);}
static void ui_draw_control_frame(uint8_t *fb,EpdRect rect,int radius,int gray){(void)fb;(void)rect;(void)radius;(void)gray;}
static void ui_fill_round_rect(uint8_t *fb,EpdRect rect,int radius,int gray){(void)fb;(void)rect;(void)radius;(void)gray;}
static void epd_fill_circle(int x,int y,int radius,int gray,uint8_t *fb){(void)x;(void)y;(void)radius;(void)gray;(void)fb;}
static void epd_draw_circle(int x,int y,int radius,int gray,uint8_t *fb){(void)x;(void)y;(void)radius;(void)gray;(void)fb;}
static int ui_text_fixed_width_px(int px,const char *value){return px*(int)strlen(value);}
static bool ui_rect_hit(EpdRect r,uint16_t x,uint16_t y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
static void ui_text_input_draw(uint8_t *fb,int *edit,EpdRect rect,int px,bool masked,void *suffix){(void)fb;(void)edit;(void)rect;(void)px;(void)masked;(void)suffix;++fields;}
static void ui_keyboard_draw(uint8_t *fb,int top){(void)fb;assert(top==560);++draws;}
void ui_weather_draw(uint8_t *fb,EpdRect region,bool tomorrow,const app_weather_view_t *v,int64_t now){(void)fb;assert(region.x==0&&region.y==236&&region.width==684&&region.height==472);(void)tomorrow;(void)v;assert(now==1793579400);}
'''
unit += re.search(r'static const app_home_widget_t s_choices\[\]=\{[^;]+;', PAGE.read_text()).group(0)
for name, path in (("valid_region", ROOT / "main/ui/ui_weather.c"), ("refresh_button", ROOT / "main/ui/ui_weather.c"),
                   ("ui_weather_hit", ROOT / "main/ui/ui_weather.c"),
                   ("city_begin", PAGE), ("app_components_open", PAGE), ("app_components_open_home", PAGE), ("enter", PAGE),
                   ("system_nav", PAGE), ("component_option", PAGE), ("city_result_row", PAGE), ("city_page_button", PAGE),
                   ("render", PAGE), ("keyboard_paint", PAGE), ("present", PAGE),
                   ("keyboard_hide", PAGE), ("submit", PAGE), ("back", PAGE),
                   ("gesture", PAGE), ("tick", PAGE), ("key", PAGE)):
    unit += "\n" + function(name, path)
unit += r'''
int main(void){
    app_ctx_t ctx={0};
    next_update.settle=true;
    assert(keyboard_paint(&ctx,false)==APP_REDRAW_AREA&&present(&ctx,APP_REDRAW_AREA));
    assert(refresh_intent==DISPLAY_REFRESH_SETTLE&&refreshes==1&&!s_settle);
    next_update.settle=false;next_update.layout=true;
    assert(keyboard_paint(&ctx,false)==APP_REDRAW_AREA&&present(&ctx,APP_REDRAW_AREA));
    assert(refresh_intent==DISPLAY_REFRESH_DETAIL&&refreshes==2);
    next_update.layout=false;
    assert(keyboard_paint(&ctx,false)==APP_REDRAW_AREA&&present(&ctx,APP_REDRAW_AREA));
    assert(refresh_intent==DISPLAY_REFRESH_FEEDBACK&&refreshes==3&&!present(&ctx,APP_REDRAW_PAGE));
    app_components_open_home(&ctx,APP_HOME_SLOT_COUNT);assert(!ctx.request_app);
    view.enabled=true;app_components_open_home(&ctx,APP_HOME_SLOT_BOTTOM);assert(ctx.request_app==&app_components);
    enter(&ctx);assert(!s_city&&!s_weather_settings&&!s_tomorrow&&layout_inits==1&&weather_inits==1&&prepares==1);
    app_components_open(&ctx,true);enter(&ctx);assert(s_city&&s_weather_settings&&!s_keyboard_visible&&!s_keyboard_bound);
    assert(key(&ctx,UI_KEY_1)==APP_REDRAW_PAGE&&!s_city&&s_weather_settings);
    assert(key(&ctx,UI_KEY_1)==APP_REDRAW_PAGE&&!s_weather_settings);
    for(int screen=0;screen<3;++screen){
        bool city=screen==2;
        s_city=city;s_weather_settings=screen!=0;s_keyboard_visible=false;icons=0;system_nav(NULL);assert(icons==1);
        for(int x=0;x<684;++x){
            s_city=city;s_weather_settings=screen!=0;nav=-1;ctx.request_return=false;int before=cancels;
            ui_gesture_event_t event={UI_GESTURE_TAP,x,1164};
            app_redraw_t redraw=gesture(&ctx,&event);
            if(x>=228&&x<456)assert(nav==0&&redraw==APP_REDRAW_NONE);
            else assert(s_city==city&&nav==-1&&!ctx.request_return&&redraw==APP_REDRAW_NONE&&cancels==before);
        }
    }
    s_city=s_weather_settings=false;ui_gesture_event_t event={UI_GESTURE_TAP,600,281};
    assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&slots[0]==APP_HOME_WIDGET_READING);
    for(unsigned slot=0;slot<2;++slot)for(unsigned choice=0;choice<4;++choice){
        event=(ui_gesture_event_t){UI_GESTURE_TAP,100,(slot?645:263)+(int)choice*76};
        assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&slots[slot]==s_choices[choice]);
    }
    assert(slots[0]==APP_HOME_WIDGET_EMPTY&&slots[1]==APP_HOME_WIDGET_EMPTY);
    assert(tick(&ctx)==APP_REDRAW_NONE);
    layout_save_ok=false;event=(ui_gesture_event_t){UI_GESTURE_TAP,624,365};
    assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&slots[0]==APP_HOME_WIDGET_EMPTY&&s_notice[0]);layout_save_ok=true;
    assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&slots[0]==APP_HOME_WIDGET_WEATHER&&!s_notice[0]);
    event=(ui_gesture_event_t){UI_GESTURE_TAP,624,743};
    assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&slots[1]==APP_HOME_WIDGET_WEATHER&&slots[0]==APP_HOME_WIDGET_EMPTY);
    for(int x=0;x<684;++x){
        int before=layout_writes;event=(ui_gesture_event_t){UI_GESTURE_TAP,x,365};
        app_redraw_t result=gesture(&ctx,&event);
        assert(result==((x>=36&&x<648)?APP_REDRAW_PAGE:APP_REDRAW_NONE));
        assert(layout_writes==before+((x>=36&&x<648)?1:0));
    }
    int before_layout=layout_writes;event=(ui_gesture_event_t){UI_GESTURE_TAP,100,548};
    assert(gesture(&ctx,&event)==APP_REDRAW_NONE&&layout_writes==before_layout);
    render(&ctx,NULL);
    event=(ui_gesture_event_t){UI_GESTURE_TAP,100,964};
    assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&s_weather_settings);
    event=(ui_gesture_event_t){UI_GESTURE_TAP,596,278};
    assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&s_tomorrow);
    event.x0=498;assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&!s_tomorrow);
    event=(ui_gesture_event_t){UI_GESTURE_TAP,600,838};
    assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&view.auto_refresh&&!s_city);
    save_ok=false;assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&view.auto_refresh&&s_notice[0]);save_ok=true;
    assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&!view.auto_refresh&&!s_city&&!s_notice[0]);
    event=(ui_gesture_event_t){UI_GESTURE_TAP,400,934};
    assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&s_city);
    assert(!keys);
    assert(!s_keyboard_visible&&!s_keyboard_bound&&!begins);
    render(&ctx,NULL);assert(draws==0&&fields==1&&location_controls==1);
    assert(tick(&ctx)==APP_REDRAW_NONE&&!holds&&!idles);
    event=(ui_gesture_event_t){UI_GESTURE_TAP,100,800};
    assert(gesture(&ctx,&event)==APP_REDRAW_NONE&&!keys&&!s_keyboard_visible);
    event=(ui_gesture_event_t){UI_GESTURE_SWIPE_L,100,800};
    assert(gesture(&ctx,&event)==APP_REDRAW_NONE&&!pages);
    event=(ui_gesture_event_t){UI_GESTURE_TAP,100,304};
    assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&s_keyboard_visible&&begins==1);
    render(&ctx,NULL);assert(draws==1);
    assert(tick(&ctx)==APP_REDRAW_NONE&&holds==1&&idles==1);
    strcpy(s_query,"shanghai");pending=true;int old_ends=ends;
    event=(ui_gesture_event_t){UI_GESTURE_TAP,20,700};
    assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&!s_keyboard_visible);
    assert(!strcmp(s_query,"shanghai")&&pending&&ends==old_ends);
    assert(tick(&ctx)==APP_REDRAW_NONE&&holds==1&&idles==1);
    event=(ui_gesture_event_t){UI_GESTURE_TAP,100,304};
    assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&begins==1&&pending);
    assert(gesture(&ctx,&event)==APP_REDRAW_AREA);
    event=(ui_gesture_event_t){UI_GESTURE_SWIPE_L,100,400};
    assert(gesture(&ctx,&event)==APP_REDRAW_NONE&&!pages);
    event.y0=800;assert(gesture(&ctx,&event)==APP_REDRAW_AREA&&pages==1);
    event=(ui_gesture_event_t){UI_GESTURE_TAP,594,304};
    assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&s_keyboard_visible&&!requests&&s_notice[0]);
    pending=false;assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&!s_keyboard_visible&&searches==1);
    assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&searches==2);
    event=(ui_gesture_event_t){UI_GESTURE_TAP,100,304};
    assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&begins==1);
    key_result=UI_KEYBOARD_DONE;event=(ui_gesture_event_t){UI_GESTURE_TAP,600,980};
    assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&!s_keyboard_visible&&searches==3);key_result=UI_KEYBOARD_NONE;
    event=(ui_gesture_event_t){UI_GESTURE_TAP,100,304};gesture(&ctx,&event);
    event=(ui_gesture_event_t){UI_GESTURE_TAP,468,193};
    assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&!s_keyboard_visible&&locates==1);
    event.x0=600;assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&locates==2);
    request_ok=false;assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&s_notice[0]);request_ok=true;
    event.x0=648;assert(gesture(&ctx,&event)==APP_REDRAW_NONE&&locates==3);
    city_begin();assert(!s_query[0]&&!pending&&!s_keyboard_bound&&!s_keyboard_visible);
    event=(ui_gesture_event_t){UI_GESTURE_TAP,100,304};gesture(&ctx,&event);
    view.result_count=1;strcpy(view.results[0].name,"上海");
    save_ok=false;event=(ui_gesture_event_t){UI_GESTURE_TAP,100,365};
    unsigned old_selections=selections;
    assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&s_city&&!s_keyboard_visible&&selections==old_selections);
    assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&s_city&&!s_keyboard_visible&&s_notice[0]);
    save_ok=true;assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&!s_city&&!strcmp(view.city.name,"上海"));
    city_begin();strcpy(s_query,"杭州");view.result_count=8;view.result_total=14;view.result_page=0;
    assert(WEATHER_CITY_LIMIT==8);
    for(unsigned i=0;i<view.result_count;++i){
        snprintf(view.results[i].id,sizeof(view.results[i].id),"10121010%u",i);
        snprintf(view.results[i].name,sizeof(view.results[i].name),"杭州%u",i);
        strcpy(view.results[i].admin2,"杭州市");strcpy(view.results[i].admin1,"浙江省");
        EpdRect row=city_result_row(i);
        assert(row.x==36&&row.y==350+(int)i*84&&row.width==612&&row.height==84);
    }
    assert(city_result_row(7).y+city_result_row(7).height<=city_page_button(false).y);
    assert(city_page_button(false).y+city_page_button(false).height<UI_NAV_TOP);
    render(&ctx,NULL);assert(row_names==8&&row_admins==8);
    s_keyboard_visible=true;render(&ctx,NULL);assert(!row_names&&!row_admins);s_keyboard_visible=false;
    for(unsigned i=0;i<8;++i)for(int edge=0;edge<3;++edge){
        s_city=true;EpdRect row=city_result_row(i);
        event=(ui_gesture_event_t){UI_GESTURE_TAP,edge==0?36:edge==1?342:647,row.y+(edge==0?0:edge==1?42:83)};
        assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&!s_city&&!strcmp(view.city.id,view.results[i].id));
    }
    s_city=true;old_selections=selections;
    const int misses[][2]={{35,400},{648,400},{36,349},{36,1022},{36,1035},{648,1068},{36,1100}};
    for(unsigned i=0;i<sizeof(misses)/sizeof(*misses);++i){
        event=(ui_gesture_event_t){UI_GESTURE_TAP,misses[i][0],misses[i][1]};
        assert(gesture(&ctx,&event)==APP_REDRAW_NONE&&s_city&&selections==old_selections);
    }
    event=(ui_gesture_event_t){UI_GESTURE_TAP,614,1068};
    assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&search_page==1&&s_city);
    view.result_page=1;view.result_count=6;int old_searches=searches;
    assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&searches==old_searches);
    render(&ctx,NULL);assert(row_names==6&&row_admins==6);
    event.x0=70;assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&search_page==0);
    view.result_page=0;old_searches=searches;
    assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&searches==old_searches);
    render(&ctx,NULL);
    for(int x=0;x<684;++x){
        city_begin();s_keyboard_visible=true;nav=-1;
        event=(ui_gesture_event_t){UI_GESTURE_TAP,x,1164};
        assert(gesture(&ctx,&event)==APP_REDRAW_PAGE&&!s_keyboard_visible&&s_city);
        assert(nav==((x>=228&&x<456)?0:-1));
    }
    city_begin();assert(key(&ctx,UI_KEY_1)==APP_REDRAW_PAGE&&!s_city&&s_weather_settings);
    assert(key(&ctx,UI_KEY_1)==APP_REDRAW_PAGE&&!s_weather_settings);
    ctx.request_return=false;assert(key(&ctx,UI_KEY_1)==APP_REDRAW_PAGE&&ctx.request_return);
    city_begin();nav=-1;assert(key(&ctx,UI_KEY_2)==APP_REDRAW_PAGE&&nav==0);
    s_keyboard_visible=true;touch_t touch={.touched=true,.count=2};ctx.touch=&touch;
    assert(tick(&ctx)==APP_REDRAW_NONE&&idle_touch);
    touch.touched=false;assert(tick(&ctx)==APP_REDRAW_NONE&&!idle_touch);
    puts("PASS: eight large city rows/edge hits, paging/keyboard separation, full-pixel candidate settle, Home-only footers and weather settings");
}
'''
with tempfile.TemporaryDirectory(prefix="weather-nav-") as directory:
    work = Path(directory)
    source, binary = work / "test.c", work / "test"
    source.write_text(unit)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-Imain/services", "-Imain/settings", str(source), "-o", str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary)], check=True)
