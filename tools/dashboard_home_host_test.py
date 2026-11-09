#!/usr/bin/env python3
"""中文：首页双区布局、封面按下像素、局部刷新与分钟刷新回归。
English: Regress Home layouts, cover press pixels, isolated widget and minute updates.
"""
from pathlib import Path
import os
import subprocess
import tempfile

from home_cover_cache_host_test import function
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
HOME = Path(os.environ.get("HOME_TEST_SOURCE", ROOT / "main/apps/app_dashboard.c"))
source = HOME.read_text()
for retired in ("HOME_COMPONENT", "HOME_GOAL", "HOME_TIME", "ui_home_clock",
                "ui_home_folder", "book_ticket", "home_goal_minutes"):
    assert retired not in source, f"retired Home implementation remains: {retired}"

unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "ui_image_dither.h"
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
#define BOOK_EXCERPT_CAP 128
#define UI_GRAY_WHITE 255
#define UI_GRAY_BLACK 0
#define UI_GRAY_LIGHT 144
#define UI_NAV_TOP 1112
#define HOME_REGION_TOP 168
#define HOME_REGION_HEIGHT 472
#define WEATHER_REGION_WIDTH 684
#define WEATHER_REGION_HEIGHT 472
#define ESP_ERR_NOT_FINISHED 7
#define DISPLAY_REFRESH_DEFAULT 0
enum EpdDrawError {EPD_DRAW_SUCCESS, EPD_DRAW_FAILED};
enum {DISPLAY_REFRESH_DETAIL, DISPLAY_REFRESH_FEEDBACK};
enum EpdFontFlags {EPD_DRAW_ALIGN_LEFT, EPD_DRAW_ALIGN_CENTER, EPD_DRAW_ALIGN_RIGHT};
typedef enum {APP_REDRAW_NONE, APP_REDRAW_AREA, APP_REDRAW_PAGE, APP_REDRAW_FULL} app_redraw_t;
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {bool touched;} touch_t;
typedef struct {bool consumed;touch_t *touch;uint8_t *fb;void *hl;} app_ctx_t;
enum {UI_GESTURE_PRESS, UI_GESTURE_MOVE, UI_GESTURE_TAP, UI_GESTURE_LONG_PRESS,
      UI_GESTURE_SWIPE_L, UI_GESTURE_SWIPE_R, UI_GESTURE_SWIPE_U, UI_GESTURE_CANCEL};
typedef struct {int type,x0,y0,x,y;} ui_gesture_event_t;
typedef struct {char path[128],title[128],author[128];uint8_t *cover;
    bool cover_fast;uint16_t cover_width,cover_height;
    bool has_progress;struct {unsigned pct;} progress;} home_book_t;
typedef struct {bool mounted;} board_sd_info_t;
typedef enum {UI_AVATAR_PENDING, UI_AVATAR_READY, UI_AVATAR_FAILED} ui_avatar_state_t;
static home_book_t s_current,s_recent[6];
static bool s_clock_pending,s_press_pending,s_home_force_full_once,s_scan_pending;
static bool s_avatar_pending,s_avatar_ready,s_cover_waiting;
static char s_avatar_path[288],s_excerpt[128];
static uint8_t s_avatar[3600];
static EpdRect s_pressed,s_area,last_area;
static int64_t s_clock_minute=-2,minute;
static unsigned s_cover_generation;
static int s_cover_next,cover_calls,scan_calls,nav=-1,open_calls,library_calls,weather_requests;
static bool library_city;
#include "home_components.h"
#include "reading_journal_model.h"
static app_home_widget_t slots[2]={APP_HOME_WIDGET_READING,APP_HOME_WIDGET_EMPTY};
app_home_widget_t app_home_component_at(unsigned slot){assert(slot<2);return slots[slot];}
bool app_home_has_widget(app_home_widget_t widget){return slots[0]==widget||slots[1]==widget;}
static bool s_weather_tomorrow[2];
static unsigned s_widget_dirty;
static const reading_journal_model_t *app_reading_journal_model(void){return NULL;}
static const reading_journal_date_t *app_reading_journal_today(void){return NULL;}
static const char *app_reading_journal_notice(void){return "";}
static bool journal_changed;
static int journal_opens,journal_scenes;
static bool app_reading_journal_poll(void){bool changed=journal_changed;journal_changed=false;return changed;}
static bool app_reading_journal_next_scene(void){++journal_scenes;return true;}
static void app_reading_journal_open(app_ctx_t *ctx){(void)ctx;++journal_opens;}
static EpdRect ui_reading_journal_art(EpdRect region){return(EpdRect){438,region.y+78,210,162};}
static int widget_paints[2];
static int draws,pages,fulls,last_intent,clock_paints;
static bool wait_cover,fail_open;
static int contrast=100;
static int app_settings_system_contrast(void){return contrast;}
static inline uint8_t ui_control_gray(uint8_t gray);
static enum EpdDrawError draw_result;
static uint8_t framebuffer[UI_LOCK_WIDTH*UI_LOCK_HEIGHT],cover_pixel;
static void pixel(uint8_t *fb,int x,int y,uint8_t gray){
    assert(x>=0&&x<UI_LOCK_WIDTH&&y>=0&&y<UI_LOCK_HEIGHT);
    fb[y*UI_LOCK_WIDTH+x]=gray;
}
static void ui_fill_rect_fast(EpdRect r,uint8_t gray,uint8_t *fb){
    for(int y=r.y;y<r.y+r.height;++y)for(int x=r.x;x<r.x+r.width;++x)pixel(fb,x,y,gray);
}
static void ui_clear_page(uint8_t *fb){memset(fb,255,sizeof(framebuffer));}
static void ui_reading_journal_widget(uint8_t *fb,EpdRect region,const reading_journal_model_t *model,const reading_journal_date_t *today,const char *notice){
    (void)model;(void)today;(void)notice;ui_fill_rect_fast(region,0x77,fb);
}
static void epd_draw_pixel(int x,int y,uint8_t gray,uint8_t *fb){pixel(fb,x,y,gray);}
static void ui_draw_round_rect(uint8_t *fb,EpdRect r,int radius,uint8_t gray){
    assert(radius==0);gray=ui_control_gray(gray);
    for(int x=r.x;x<r.x+r.width;++x){pixel(fb,x,r.y,gray);pixel(fb,x,r.y+r.height-1,gray);}
    for(int y=r.y;y<r.y+r.height;++y){pixel(fb,r.x,y,gray);pixel(fb,r.x+r.width-1,y,gray);}
}
static void ui_fill_round_rect(uint8_t *fb,EpdRect r,int radius,uint8_t gray){
    (void)radius;ui_fill_rect_fast(r,gray,fb);
}
static void ui_nav_status(uint8_t *fb){pixel(fb,24,18,0);}
static void ui_nav_draw(uint8_t *fb,int tab){
    assert(tab==0);ui_fill_rect_fast((EpdRect){0,1112,684,104},255,fb);
    ui_fill_rect_fast((EpdRect){0,1112,684,2},144,fb);pixel(fb,24,1164,0);
}
static void ui_nav_clock_draw(uint8_t *fb){++clock_paints;pixel(fb,24,18,0);}
static void ui_text(uint8_t *fb,int x,int y,int px,const char *text,enum EpdFontFlags align,bool white){
    (void)px;(void)text;(void)align;pixel(fb,x,y,white?255:0);
}
static void label(uint8_t *fb,int x,int y,int px,const char *text,enum EpdFontFlags align,uint8_t gray){
    (void)px;(void)text;(void)align;pixel(fb,x,y,gray);
}
static void label_on(uint8_t *fb,int x,int y,int px,const char *text,enum EpdFontFlags align,uint8_t gray,uint8_t bg){
    (void)bg;label(fb,x,y,px,text,align,gray);
}
static int ui_text_fixed_width_px(int px,const char *text){(void)px;(void)text;return 100;}
static void fit(char *text,int px,int width){(void)text;(void)px;(void)width;}
static void circle_frame(uint8_t *fb,int x,int y,int radius){(void)radius;pixel(fb,x,y,0);}
static void ui_builtin_avatar_draw(uint8_t *fb,int x,int y,int side){
    assert(x==588&&y==88&&side==60);pixel(fb,x,y,0);
}
static void ui_wallpaper_draw_avatar(uint8_t *fb,const uint8_t *data,EpdRect r,int radius){
    (void)data;(void)radius;pixel(fb,r.x,r.y,0);
}
static void ui_draw_separator(uint8_t *fb,int y,int x,int width,uint8_t gray){
    ui_fill_rect_fast((EpdRect){x,y,width,1},gray,fb);
}
static int title_lines(uint8_t *fb,int top,const char *text,uint8_t bg){
    (void)text;(void)bg;pixel(fb,248,top+126,0);return 1;
}
enum {UI_ICON_SLIDERS_HORIZONTAL,UI_ICON_CHEVRON_RIGHT};
static void ui_draw_icon(uint8_t *fb,int x,int y,int px,int icon,int gray){
    (void)px;(void)icon;pixel(fb,x,y,gray);
}
static home_book_t *featured(void){return s_current.path[0]?&s_current:&s_recent[0];}
static int64_t ui_nav_clock_minute(void){return minute;}
static enum EpdDrawError display_refresh_area(void *hl,int intent,EpdRect area,int flags){
    (void)hl;assert(flags==DISPLAY_REFRESH_DEFAULT);++draws;last_area=area;last_intent=intent;return draw_result;
}
static void guard_draw_result(void *hl,enum EpdDrawError result){(void)hl;assert(result==draw_result);}
static enum EpdDrawError update_display_full(void *hl){(void)hl;++fulls;return draw_result;}
static enum EpdDrawError update_display_fast_page(void *hl){(void)hl;++pages;return draw_result;}
static int board_sd_get_info(board_sd_info_t *info){info->mounted=true;return 0;}
static unsigned book_cover_jobs_begin(void){return 1;}
static void scan_books(void){++scan_calls;}
static void restore_cover_cache(void){}
static void remember_scan_state(const board_sd_info_t *info){(void)info;}
static void refresh_excerpt(void){}
static void avatar_prepare_state(void){}
static ui_avatar_state_t ui_avatar_poll(const char *path,int size,uint8_t *out){
    (void)path;(void)size;(void)out;return UI_AVATAR_READY;
}
static bool load_cover(home_book_t *book,int slot){
    assert(slot==0||slot==1);++cover_calls;s_cover_waiting=wait_cover;
    if(wait_cover)return false;book->cover=&cover_pixel;return true;
}
static int ui_nav_hit(int x,int y){return y>=1112?x/171:-1;}
static void ui_nav_request(app_ctx_t *ctx,int tab){(void)ctx;nav=tab;}
static bool app_book_request_open_from_home(const char *path){assert(path[0]);++open_calls;return !fail_open;}
'''
unit += r'''
#include "weather_service.h"
static app_weather_view_t weather_view;
static const app_weather_view_t *app_weather_view_stub(void){return &weather_view;}
#define app_weather_view app_weather_view_stub
static int64_t app_weather_now_stub(void){return 1793579400;}
#define app_weather_now app_weather_now_stub
static bool poll_changed;
static bool app_weather_poll_stub(bool automatic){assert(automatic==app_home_has_widget(APP_HOME_WIDGET_WEATHER));return poll_changed;}
#define app_weather_poll app_weather_poll_stub
static void app_components_open(app_ctx_t *ctx,bool city){(void)ctx;library_city=city;++library_calls;}
static void ui_weather_draw(uint8_t *f,EpdRect region,bool tomorrow,const app_weather_view_t *v,int64_t now){
    (void)v;assert(now==1793579400);assert(region.x==0&&region.width==684&&region.height==472);
    assert(region.y==168||region.y==640);++widget_paints[region.y==640];
    ui_fill_rect_fast(region,tomorrow?0x66:0x55,f);
}
typedef enum {UI_WEATHER_NONE,UI_WEATHER_CITY,UI_WEATHER_TODAY,UI_WEATHER_TOMORROW,UI_WEATHER_REFRESH,UI_WEATHER_SETTINGS} ui_weather_action_t;
static int epd_rotated_display_width(void){return 684;}
static int epd_rotated_display_height(void){return 1216;}

static bool app_weather_request_stub(app_weather_request_t request,const char *query){(void)query;assert(request==APP_WEATHER_FETCH);++weather_requests;return true;}
#define app_weather_request app_weather_request_stub
'''
unit += "\n" + function("valid_region", ROOT / "main/ui/ui_weather.c")
unit += "\n" + function("refresh_button", ROOT / "main/ui/ui_weather.c")
unit += "\n" + function("ui_weather_hit", ROOT / "main/ui/ui_weather.c")
unit += "\n" + function("ui_control_gray", ROOT / "main/ui/ui_kit.h")
unit += "\n" + function("ui_contrast_gray", ROOT / "main/ui/ui_kit.c")
unit += "\n" + function("ui_inset_rect", ROOT / "main/ui/ui_kit.c")
for name in ("home_region", "continue_card_rect", "continue_card_hit_rect", "continue_card_pressed",
             "control_feedback_area", "home_frame", "draw_cover", "render_reading", "render_widget", "redraw_widgets", "render_root", "render", "present", "on_tick",
             "hit", "control", "on_gesture", "main_page_visible"):
    unit += "\n" + function(name, HOME)
unit += r'''
static uint8_t before[sizeof(framebuffer)];
static void isolated(EpdRect area){
    for(int y=0;y<1216;++y)for(int x=0;x<684;++x)
        if(x<area.x||x>=area.x+area.width||y<area.y||y>=area.y+area.height)
            assert(framebuffer[y*684+x]==before[y*684+x]);
}
static void empty_slot(unsigned slot){
    EpdRect r=home_region(slot);
    for(int y=r.y;y<r.y+r.height;++y)for(int x=0;x<684;++x)
        assert(framebuffer[y*684+x]==((x==632&&y==r.y+38)?0:255));
}
static void binary_margin_feedback(EpdRect area,EpdRect cover){
    unsigned changed=0;
    for(int y=0;y<1216;++y)for(int x=0;x<684;++x){
        unsigned at=y*684+x;
        if(framebuffer[at]==before[at])continue;
        ++changed;
        assert(hit(area,x,y));
        assert(!hit(cover,x,y));
        assert(before[at]==255&&framebuffer[at]==0);
    }
    assert(changed>0);
}
static void cover_feedback(app_ctx_t *ctx){
    static uint8_t gray[320*360],bits[40*360];
    const unsigned shapes[][2]={{178,242},{97,317},{263,113},{171,241}};
    const int contrasts[]={70,160};
    const int actions[]={UI_GESTURE_LONG_PRESS,UI_GESTURE_CANCEL,UI_GESTURE_MOVE,
                         UI_GESTURE_TAP,UI_GESTURE_TAP};
    unsigned cases=0;
    for(unsigned slot=0;slot<2;++slot)for(unsigned format=0;format<3;++format)
    for(unsigned shape=0;shape<sizeof(shapes)/sizeof(*shapes);++shape)
    for(unsigned c=0;c<sizeof(contrasts)/sizeof(*contrasts);++c){
        if(format==2&&shape)continue;
        unsigned w=shapes[shape][0],h=shapes[shape][1],stride=(w+7)/8;
        memset(bits,0,sizeof(bits));
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
            gray[y*w+x]=(uint8_t)(11*x+7*y);
            if((x+3*y)%7<4)bits[y*stride+x/8]|=1u<<(x&7);
        }
        contrast=contrasts[c];s_current.cover_fast=format==1;
        s_current.cover=format==2?NULL:format==1?bits:gray;
        s_current.cover_width=format==2?0:w;s_current.cover_height=format==2?0:h;
        strcpy(s_current.path,"/book.epub");slots[slot]=APP_HOME_WIDGET_READING;
        slots[1-slot]=APP_HOME_WIDGET_WEATHER;
        EpdRect region=home_region(slot),box={36,region.y+84,178,242};
        if(format!=2){
            if(w*242>h*178)box.height=178*h/w;else box.width=242*w/h;
            box.x=36+(178-box.width)/2;box.y=region.y+84+242-box.height;
        }
        for(unsigned action=0;action<sizeof(actions)/sizeof(*actions);++action){
            ++cases;s_press_pending=s_clock_pending=false;s_widget_dirty=0;
            s_home_force_full_once=false;fail_open=action==4;nav=-1;
            render(ctx,ctx->fb);memcpy(before,framebuffer,sizeof(before));
            const int old_pages=pages,old_fulls=fulls,old_opens=open_calls,old_library=library_calls;
            ui_gesture_event_t ev={UI_GESTURE_PRESS,72,region.y+200,72,region.y+200};
            assert(on_gesture(ctx,&ev)==APP_REDRAW_AREA&&s_press_pending);
            assert(present(ctx,APP_REDRAW_AREA)&&last_intent==DISPLAY_REFRESH_FEEDBACK);
            binary_margin_feedback(last_area,box);
            ev.type=UI_GESTURE_MOVE;assert(on_gesture(ctx,&ev)==APP_REDRAW_NONE&&s_press_pending);
            ev.type=actions[action];
            if(ev.type==UI_GESTURE_MOVE){ev.x=680;ev.y=region.y+20;}
            app_redraw_t redraw=on_gesture(ctx,&ev);assert(!s_press_pending);
            if(ev.type==UI_GESTURE_LONG_PRESS){
                assert(redraw==APP_REDRAW_NONE&&library_calls==old_library+1&&!library_city);
                assert(open_calls==old_opens&&nav==-1);
            }else if(ev.type==UI_GESTURE_TAP){
                assert(open_calls==old_opens+1&&library_calls==old_library);
                assert(redraw==(fail_open?APP_REDRAW_AREA:APP_REDRAW_NONE)&&nav==(fail_open?-1:1));
            }else{
                assert(redraw==APP_REDRAW_AREA&&library_calls==old_library&&open_calls==old_opens&&nav==-1);
            }
            if(redraw==APP_REDRAW_AREA){
                assert(present(ctx,redraw)&&last_intent==DISPLAY_REFRESH_DETAIL);
            }else render(ctx,ctx->fb);
            assert(memcmp(framebuffer,before,sizeof(before))==0);
            assert(pages==old_pages&&fulls==old_fulls);
        }
    }
    s_current.cover=NULL;s_current.cover_width=s_current.cover_height=0;fail_open=false;contrast=100;
    printf("PASS: %u real-cover press/hold/cancel/drag/tap cases, fixed grayscale/1bpp/placeholder pixels and binary margin feedback in both slots\n",cases);
}
int main(void){
    app_ctx_t ctx={.fb=framebuffer};touch_t touch={0};ctx.touch=&touch;
    cover_feedback(&ctx);
    draws=pages=fulls=open_calls=library_calls=0;
    unsigned layouts=0;
    for(unsigned a=0;a<4;++a)for(unsigned b=0;b<4;++b){
        if(a==b&&a!=APP_HOME_WIDGET_EMPTY)continue;
        slots[0]=a;slots[1]=b;++layouts;
        for(int has_book=0;has_book<2;++has_book)for(int avatar=0;avatar<2;++avatar)for(int pressed=0;pressed<2;++pressed){
            snprintf(s_current.path,sizeof(s_current.path),"%s",has_book?"/book.epub":"");
            strcpy(s_current.title,"Title");strcpy(s_current.author,"Author");strcpy(s_excerpt,"Excerpt");
            s_current.has_progress=true;s_current.progress.pct=42;s_avatar_ready=avatar;
            s_press_pending=pressed;s_pressed=continue_card_rect(a==APP_HOME_WIDGET_READING?0:1);
            render(&ctx,ctx.fb);assert(main_page_visible(&ctx));
            for(unsigned slot=0;slot<2;++slot){
                EpdRect r=home_region(slot);assert(r.x==0&&r.y==(slot?640:168)&&r.width==684&&r.height==472);
                if(slots[slot]==APP_HOME_WIDGET_EMPTY)empty_slot(slot);
                if(slots[slot]==APP_HOME_WIDGET_READING&&has_book){
                    assert(framebuffer[(r.y+84)*684+36]==0);
                    assert(framebuffer[(r.y+80)*684+36]==255);
                }
                if(slots[slot]==APP_HOME_WIDGET_WEATHER)
                    for(int y=r.y;y<r.y+r.height;++y)for(int x=0;x<684;++x)assert(framebuffer[y*684+x]==0x55);
                assert(continue_card_pressed(slot)==(pressed&&s_pressed.y==r.y));
            }
            for(int x=0;x<684;++x)assert(framebuffer[1112*684+x]==144);
            assert(framebuffer[18*684+24]==0&&framebuffer[1164*684+24]==0);
        }
    }
    assert(layouts==13);s_press_pending=false;strcpy(s_current.path,"/book.epub");
    slots[0]=APP_HOME_WIDGET_READING;slots[1]=APP_HOME_WIDGET_EMPTY;
    ui_gesture_event_t ev={UI_GESTURE_TAP,540,118,540,118};nav=-1;
    assert(on_gesture(&ctx,&ev)==APP_REDRAW_NONE&&nav==-1);
    ev.x0=618;assert(on_gesture(&ctx,&ev)==APP_REDRAW_NONE&&nav==3);
    ev.x0=400;ev.y0=1150;on_gesture(&ctx,&ev);assert(nav==2);
    minute=s_clock_minute=100;s_cover_next=1;
    assert(on_tick(&ctx)==APP_REDRAW_NONE&&!draws&&!pages&&!fulls&&!scan_calls&&!cover_calls);
    render(&ctx,ctx.fb);assert(framebuffer[(HOME_REGION_TOP+38)*684+632]==255);memcpy(before,framebuffer,sizeof(before));
    minute=101;assert(on_tick(&ctx)==APP_REDRAW_AREA&&s_clock_pending);
    assert(present(&ctx,APP_REDRAW_AREA));isolated((EpdRect){24,18,156,44});
    assert(draws==1&&!pages&&!fulls&&clock_paints==1&&last_intent==DISPLAY_REFRESH_DETAIL);
    assert(last_area.x==24&&last_area.y==18&&last_area.width==156&&last_area.height==44&&s_clock_minute==101);
    minute=102;draw_result=EPD_DRAW_FAILED;assert(on_tick(&ctx)==APP_REDRAW_AREA);
    present(&ctx,APP_REDRAW_AREA);assert(s_clock_minute==101&&on_tick(&ctx)==APP_REDRAW_AREA);
    draw_result=EPD_DRAW_SUCCESS;present(&ctx,APP_REDRAW_AREA);assert(s_clock_minute==102);
    touch.touched=true;minute=103;assert(on_tick(&ctx)==APP_REDRAW_NONE);touch.touched=false;
    ctx.consumed=true;assert(on_tick(&ctx)==APP_REDRAW_NONE);ctx.consumed=false;minute=102;
    s_cover_next=0;s_current.cover=NULL;wait_cover=true;assert(on_tick(&ctx)==APP_REDRAW_NONE&&s_cover_next==0);
    wait_cover=false;assert(on_tick(&ctx)==APP_REDRAW_AREA&&cover_calls==2&&s_area.y==168&&s_area.height==472);
    memcpy(before,framebuffer,sizeof(before));present(&ctx,APP_REDRAW_AREA);isolated(home_region(0));
    for(int i=0;i<10;++i)assert(on_tick(&ctx)==APP_REDRAW_NONE);
    assert(cover_calls==2&&!scan_calls);
    s_home_force_full_once=true;present(&ctx,APP_REDRAW_PAGE);assert(fulls==1&&!s_home_force_full_once);
    present(&ctx,APP_REDRAW_PAGE);assert(pages==1);present(&ctx,APP_REDRAW_FULL);assert(fulls==2);
    for(unsigned slot=0;slot<2;++slot){
        slots[slot]=APP_HOME_WIDGET_WEATHER;slots[1-slot]=APP_HOME_WIDGET_READING;
        EpdRect region=home_region(slot);s_weather_tomorrow[slot]=false;render(&ctx,ctx.fb);
        int requests_before=weather_requests,pages_before=pages,fulls_before=fulls;
        for(int tomorrow=1;tomorrow>=0;--tomorrow){
            memcpy(before,framebuffer,sizeof(before));
            ev=(ui_gesture_event_t){UI_GESTURE_TAP,tomorrow?596:498,region.y+42,0,0};
            assert(on_gesture(&ctx,&ev)==APP_REDRAW_AREA&&s_weather_tomorrow[slot]==(bool)tomorrow);
            assert(weather_requests==requests_before&&s_widget_dirty==(1u<<slot));
            assert(s_area.y==region.y&&s_area.height==472&&s_area.x==0&&s_area.width==684);
            assert(present(&ctx,APP_REDRAW_AREA));isolated(region);
            assert(last_area.y==region.y&&last_area.height==472&&pages==pages_before&&fulls==fulls_before);
        }
        memcpy(before,framebuffer,sizeof(before));
        ev=(ui_gesture_event_t){UI_GESTURE_TAP,632,region.y+446,0,0};
        assert(on_gesture(&ctx,&ev)==APP_REDRAW_AREA&&weather_requests==requests_before+1);
        present(&ctx,APP_REDRAW_AREA);isolated(region);
        poll_changed=true;assert(on_tick(&ctx)==APP_REDRAW_AREA&&s_widget_dirty==(1u<<slot));poll_changed=false;
        memcpy(before,framebuffer,sizeof(before));present(&ctx,APP_REDRAW_AREA);isolated(region);
        int library_before=library_calls;ev=(ui_gesture_event_t){UI_GESTURE_TAP,80,region.y+42,0,0};
        assert(on_gesture(&ctx,&ev)==APP_REDRAW_NONE&&library_calls==library_before+1&&library_city);
        library_before=library_calls;ev=(ui_gesture_event_t){UI_GESTURE_TAP,632,region.y+446,0,0};nav=-1;
        int refresh_before=weather_requests;
        assert(on_gesture(&ctx,&ev)==APP_REDRAW_AREA&&library_calls==library_before&&weather_requests==refresh_before+1&&nav==-1);
        present(&ctx,APP_REDRAW_AREA);
        ev.y0=region.y+471;refresh_before=weather_requests;
        assert(on_gesture(&ctx,&ev)==APP_REDRAW_NONE&&library_calls==library_before&&weather_requests==refresh_before&&nav==-1);
        ev.x0=664;ev.y0=region.y+446;
        assert(on_gesture(&ctx,&ev)==APP_REDRAW_NONE&&library_calls==library_before&&weather_requests==refresh_before&&nav==-1);
        ev.y0=1112;on_gesture(&ctx,&ev);assert(nav==3);
    }
    for(unsigned slot=0;slot<2;++slot){
        slots[slot]=APP_HOME_WIDGET_JOURNAL;slots[1-slot]=APP_HOME_WIDGET_WEATHER;
        EpdRect region=home_region(slot);render(&ctx,ctx.fb);
        int opened=journal_opens,scenes=journal_scenes;
        ev=(ui_gesture_event_t){UI_GESTURE_TAP,500,region.y+150,0,0};
        memcpy(before,framebuffer,sizeof(before));
        assert(on_gesture(&ctx,&ev)==APP_REDRAW_AREA&&journal_scenes==scenes+1&&journal_opens==opened);
        assert(present(&ctx,APP_REDRAW_AREA));isolated(region);
        ev.x0=120;assert(on_gesture(&ctx,&ev)==APP_REDRAW_NONE&&journal_opens==opened+1&&journal_scenes==scenes+1);
        ev.x0=648;ev.y0=region.y+78;assert(on_gesture(&ctx,&ev)==APP_REDRAW_NONE&&journal_opens==opened+2);
        journal_changed=true;assert(on_tick(&ctx)==APP_REDRAW_AREA&&s_widget_dirty==(1u<<slot));
        memcpy(before,framebuffer,sizeof(before));present(&ctx,APP_REDRAW_AREA);isolated(region);
        assert(!journal_changed&&last_area.y==region.y&&last_area.height==472);
    }
    for(unsigned a=0;a<4;++a)for(unsigned b=0;b<4;++b){
        if(a==b&&a!=APP_HOME_WIDGET_EMPTY)continue;
        slots[0]=a;slots[1]=b;
        for(unsigned slot=0;slot<2;++slot){
            EpdRect r=home_region(slot);int library_before=library_calls;
            ev=(ui_gesture_event_t){UI_GESTURE_LONG_PRESS,10,r.y+10,10,r.y+10};
            assert(on_gesture(&ctx,&ev)==APP_REDRAW_NONE&&library_calls==library_before+1&&!library_city);
            if(slots[slot]!=APP_HOME_WIDGET_WEATHER&&slots[slot]!=APP_HOME_WIDGET_JOURNAL){
                library_before=library_calls;ev=(ui_gesture_event_t){UI_GESTURE_TAP,632,r.y+38,0,0};
                assert(on_gesture(&ctx,&ev)==APP_REDRAW_NONE&&library_calls==library_before+(slots[slot]==APP_HOME_WIDGET_EMPTY)&&!library_city);
            }
        }
        if(!app_home_has_widget(APP_HOME_WIDGET_WEATHER)){
            int count=weather_requests;poll_changed=true;assert(on_tick(&ctx)==APP_REDRAW_NONE);poll_changed=false;
            assert(weather_requests==count&&redraw_widgets(APP_HOME_WIDGET_WEATHER)==APP_REDRAW_NONE&&!s_widget_dirty);
        }
    }
    puts("PASS: 13 unique Home layouts, both weather/reading/journal slots, journal art-only taps/detail entry/date polling, nav starts at1112, settings entries, isolated regions/header/nav, minute retry and resume-cover/full-refresh policy");
}
'''
with tempfile.TemporaryDirectory(prefix="dashboard-home-") as directory:
    work = Path(directory)
    env, compiler = toolchain(work)
    c_file, binary = work / "test.c", work / "test"
    c_file.write_text(unit)
    subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-g",
                    "-fsanitize=address,undefined", "-Imain/services", "-Imain/settings", "-Imain/ui",
                    str(c_file), "main/ui/ui_image_dither.c", "-o", str(binary)],
                   cwd=ROOT, env=env, check=True)
    subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)
