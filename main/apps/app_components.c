/*
 * 中文：内置组件库与天气选城，复用共用键盘；不直接访问供应商。
 * English: Built-in component library and weather city picker using the shared keyboard, never the provider directly.
 */
#include "app.h"
#include "app_components_mode.h"
#include "ui_weather.h"
#include "ui_keyboard.h"
#include "ui_nav.h"
#include "ui_menu.h"
#include "ui_gesture.h"
#include "ui_kit.h"
#include "display.h"
#include "display_refresh.h"
#include "weather_service.h"
#include "home_components.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

extern const app_desc_t app_components;
static bool s_requested_city, s_city, s_weather_settings, s_tomorrow, s_detail, s_settle, s_keyboard_visible, s_keyboard_bound;
static char s_query[129], s_notice[97];
static ui_text_edit_t s_edit;
static EpdRect s_area;
static const app_home_widget_t s_choices[]={APP_HOME_WIDGET_READING,APP_HOME_WIDGET_WEATHER,APP_HOME_WIDGET_JOURNAL,APP_HOME_WIDGET_EMPTY};
static void text(uint8_t *fb, int x, int y, int px, const char *value, enum EpdFontFlags align) {
    ui_text_fixed_vc(fb,x,y,px,value,align,false);
}
static void fitted(uint8_t *fb, int x, int y, int px, const char *value, int width) {
    char shown[193];snprintf(shown,sizeof(shown),"%s",value);
    while(*shown && ui_text_fixed_width_px(px,shown)>width) {
        size_t n=strlen(shown)-1;while(n&&((unsigned char)shown[n]&192)==128)--n;shown[n]=0;
    }
    text(fb,x,y,px,shown,EPD_DRAW_ALIGN_LEFT);
}
static void city_begin(void) {
    s_city=true;s_notice[0]=0;s_query[0]=0;app_weather_clear_search();
    ui_keyboard_end();s_keyboard_visible=s_keyboard_bound=false;
    ui_text_edit_init(&s_edit,s_query,sizeof(s_query));
}
void app_components_open(app_ctx_t *ctx, bool city_picker) {
    s_requested_city=city_picker;ctx->request_app=&app_components;
}
void app_components_open_home(app_ctx_t *ctx, unsigned slot) {
    if(slot<APP_HOME_SLOT_COUNT)app_components_open(ctx,false);
}
static void enter(app_ctx_t *ctx) {
    (void)ctx;app_weather_init();(void)ui_weather_prepare();s_tomorrow=false;s_notice[0]=0;
    app_home_components_init(app_weather_view()->enabled);
    s_weather_settings=s_requested_city;
    s_city=false;if(s_requested_city)city_begin();s_requested_city=false;
}
static void before_lock(app_ctx_t *ctx) {(void)ctx;app_weather_cancel_join();ui_keyboard_hold_cancel();ui_keyboard_release();}
static void leave(app_ctx_t *ctx) {
    before_lock(ctx);ui_keyboard_end();app_weather_clear_search();ui_weather_release();
}
static void system_nav(uint8_t *fb) {
    ui_draw_separator(fb,UI_NAV_TOP,0,684,0x90);
    ui_draw_icon(fb,342,1164,44,UI_ICON_HOUSE,0);
}
static void component_option(uint8_t *fb,int y,app_home_widget_t widget,unsigned slot) {
    static const char *names[]={"最近阅读","天气","留白","阅读手账"};
    const ui_icon_t icons[]={UI_ICON_BOOK_OPEN,UI_ICON_SUN,UI_ICON_EYE_CLOSED,UI_ICON_BOOKMARK};
    ui_draw_icon(fb,58,y,30,icons[widget],0);
    text(fb,94,y,30,names[widget],EPD_DRAW_ALIGN_LEFT);
    epd_draw_circle(624,y,15,0,fb);epd_draw_circle(624,y,14,0,fb);
    if(app_home_component_at(slot)==widget)epd_fill_circle(624,y,8,0,fb);
}
static EpdRect city_result_row(unsigned index) {
    return (EpdRect){36,350+(int)index*84,612,84};
}
static EpdRect city_page_button(bool next) {
    return (EpdRect){next?576:36,1036,72,64};
}
static void render(app_ctx_t *ctx,uint8_t *fb) {
    (void)ctx;ui_clear_page(fb);ui_nav_status(fb);
    ui_text(fb,36,91,52,s_city?"所在城市":s_weather_settings?"天气设置":"首页组件",EPD_DRAW_ALIGN_LEFT,false);
    const app_weather_view_t *v=app_weather_view();
    if(!s_city&&!s_weather_settings) {
        text(fb,36,205,26,"上方区域",EPD_DRAW_ALIGN_LEFT);
        for(unsigned i=0;i<4;++i)component_option(fb,263+(int)i*76,s_choices[i],APP_HOME_SLOT_TOP);
        ui_draw_separator(fb,548,36,612,0x90);
        text(fb,36,583,26,"下方区域",EPD_DRAW_ALIGN_LEFT);
        for(unsigned i=0;i<4;++i)component_option(fb,645+(int)i*76,s_choices[i],APP_HOME_SLOT_BOTTOM);
        ui_draw_separator(fb,920,36,612,0x90);
        text(fb,36,964,27,"天气设置",EPD_DRAW_ALIGN_LEFT);
        char city[193];snprintf(city,sizeof(city),"%s",v->has_city?v->city.name:"");
        while(*city&&ui_text_fixed_width_px(25,city)>300) {
            size_t n=strlen(city)-1;while(n&&((unsigned char)city[n]&192)==128)--n;city[n]=0;
        }
        text(fb,610,964,25,city,EPD_DRAW_ALIGN_RIGHT);
        ui_draw_icon(fb,637,964,26,UI_ICON_CHEVRON_RIGHT,0);
        if(s_notice[0])fitted(fb,36,1040,22,s_notice,612);
    } else if(!s_city) {
        text(fb,36,189,28,"天气",EPD_DRAW_ALIGN_LEFT);
        text(fb,648,189,23,app_home_has_widget(APP_HOME_WIDGET_WEATHER)?"已添加":"未添加",EPD_DRAW_ALIGN_RIGHT);
        ui_weather_draw(fb,(EpdRect){0,236,684,472},s_tomorrow,v,app_weather_now());
        ui_draw_separator(fb,790,36,612,0x90);
        text(fb,36,838,28,"自动刷新",EPD_DRAW_ALIGN_LEFT);
        text(fb,528,838,22,"每6小时",EPD_DRAW_ALIGN_RIGHT);
        ui_fill_round_rect(fb,(EpdRect){560,814,88,48},24,v->auto_refresh?0:0x90);
        epd_fill_circle(v->auto_refresh?624:584,838,18,255,fb);
        ui_draw_separator(fb,886,36,612,0x90);
        text(fb,36,934,28,"所在城市",EPD_DRAW_ALIGN_LEFT);
        fitted(fb,300,934,26,v->has_city?v->city.name:"自动识别 / 手动选择",300);
        ui_draw_icon(fb,639,934,24,UI_ICON_CHEVRON_RIGHT,0);
        fitted(fb,36,1083,20,s_notice[0]?s_notice:"点城市名称，可自动定位或手动修改。",612);
    } else {
        fitted(fb,36,193,24,v->has_city?v->city.name:"尚未选择城市",380);
        ui_draw_control_frame(fb,(EpdRect){440,165,208,56},8,0);
        ui_draw_icon(fb,468,193,24,UI_ICON_RADIO_TOWER,0);
        text(fb,560,193,24,"自动定位",EPD_DRAW_ALIGN_CENTER);
        fitted(fb,36,235,20,s_notice[0]?s_notice:v->state==APP_WEATHER_LOADING?"正在查询…":
            v->notice[0]?v->notice:"输入中文或拼音，点搜索后选择匹配城市",612);
        ui_text_input_draw(fb,&s_edit,(EpdRect){36,270,480,68},28,false,NULL);
        ui_draw_control_frame(fb,(EpdRect){540,270,108,68},16,0);
        text(fb,594,304,26,"搜索",EPD_DRAW_ALIGN_CENTER);
        if(!s_keyboard_visible) {
            for(unsigned i=0;i<v->result_count;i++) {
                const weather_location_t *city=&v->results[i];EpdRect row=city_result_row(i);char value[256];
                fitted(fb,48,row.y+26,36,city->name,550);
                snprintf(value,sizeof(value),"%s · %s",city->admin2,city->admin1);
                fitted(fb,48,row.y+62,24,value,550);
                ui_draw_icon(fb,634,row.y+42,28,UI_ICON_CHEVRON_RIGHT,0);
                ui_draw_separator(fb,row.y+row.height-1,row.x,row.width,0x90);
            }
            if(v->result_total) {
                EpdRect prev=city_page_button(false),next=city_page_button(true);
                char page[64];snprintf(page,sizeof(page),"%u / %u   共%u个地点",v->result_page+1,
                    (v->result_total+WEATHER_CITY_LIMIT-1)/WEATHER_CITY_LIMIT,v->result_total);
                text(fb,342,1068,24,page,EPD_DRAW_ALIGN_CENTER);
                if(v->result_page)ui_draw_icon(fb,prev.x+prev.width/2,1068,32,UI_ICON_CHEVRON_LEFT,0);
                if((v->result_page+1)*WEATHER_CITY_LIMIT<v->result_total)
                    ui_draw_icon(fb,next.x+next.width/2,1068,32,UI_ICON_CHEVRON_RIGHT,0);
            }
        }
        if(s_keyboard_visible)ui_keyboard_draw(fb,560);
    }
    system_nav(fb);
}
static app_redraw_t keyboard_paint(app_ctx_t *ctx,bool force) {
    ui_keyboard_update_t dirty=ui_keyboard_update(ctx->fb,560,(EpdRect){36,270,480,68},28,false,NULL,force);
    s_area=dirty.area;s_detail=dirty.layout;s_settle=dirty.settle;
    return s_area.width?APP_REDRAW_AREA:APP_REDRAW_NONE;
}
static bool keyboard_hide(void) {
    if(!s_keyboard_visible)return false;
    s_keyboard_visible=false;ui_keyboard_release();ui_keyboard_hold_cancel();
    return true;
}
static app_redraw_t submit(void) {
    if(ui_keyboard_pending())snprintf(s_notice,sizeof(s_notice),"请先选择候选字");
    else if(!weather_query_valid(s_query))snprintf(s_notice,sizeof(s_notice),"请输入2至64个汉字或字母");
    else {
        keyboard_hide();
        s_notice[0]=0;
        if(!app_weather_request(APP_WEATHER_SEARCH,s_query))snprintf(s_notice,sizeof(s_notice),"暂不能搜索，请稍后重试并检查网络与时间");
    }
    return APP_REDRAW_PAGE;
}
static app_redraw_t edited(app_ctx_t *ctx) {
    const app_weather_view_t *v=app_weather_view();
    bool old_results=v->result_count||v->notice[0]||s_notice[0];
    s_notice[0]=0;app_weather_clear_search();
    return old_results?APP_REDRAW_PAGE:keyboard_paint(ctx,true);
}
static void back(app_ctx_t *ctx) {
    if(s_city){app_weather_cancel_join();ui_keyboard_end();app_weather_clear_search();s_city=false;s_notice[0]=0;}
    else if(s_weather_settings){s_weather_settings=false;s_notice[0]=0;}
    else ctx->request_return=true;
}
static app_redraw_t gesture(app_ctx_t *ctx,const ui_gesture_event_t *ev) {
    if(s_city) {
        if(ev->type==UI_GESTURE_PRESS) {
            if(!s_keyboard_visible)return APP_REDRAW_NONE;
            ui_keyboard_hold_start(ev->x0,ev->y0,560,ctx->now_ms);
            return ui_keyboard_press(ev->x0,ev->y0,560,ctx->now_ms)?keyboard_paint(ctx,false):APP_REDRAW_NONE;
        }
        bool feedback=ui_keyboard_release();ui_keyboard_hold_cancel();
        if(ev->type==UI_GESTURE_SWIPE_L||ev->type==UI_GESTURE_SWIPE_R) {
            bool changed=s_keyboard_visible&&ev->x0>=24&&ev->x0<660&&ev->y0>=560&&ev->y0<1070&&
                ui_keyboard_page(ev->type==UI_GESTURE_SWIPE_L?1:-1);
            return changed||feedback?keyboard_paint(ctx,false):APP_REDRAW_NONE;
        }
        if(ev->type!=UI_GESTURE_TAP)return feedback?keyboard_paint(ctx,false):APP_REDRAW_NONE;
        int x=ev->x0,y=ev->y0;
        if(s_keyboard_visible&&x>=24&&x<660&&y>=560&&y<1070) {
            ui_keyboard_result_t result=ui_keyboard_tap(x,y,560,ctx->now_ms);
            if(result==UI_KEYBOARD_DONE)return submit();
            if(result==UI_KEYBOARD_CHANGED)return edited(ctx);
            return feedback?keyboard_paint(ctx,false):APP_REDRAW_NONE;
        }
        if(x>=36&&x<516&&y>=270&&y<338) {
            bool opened=!s_keyboard_visible;
            if(!s_keyboard_bound){ui_keyboard_begin(&s_edit,false);s_keyboard_bound=true;}
            s_keyboard_visible=true;
            ui_text_input_tap(&s_edit,(EpdRect){36,270,480,68},28,false,NULL,x,y);
            return opened?APP_REDRAW_PAGE:keyboard_paint(ctx,true);
        }
        if(x>=540&&x<648&&y>=270&&y<338)return submit();
        const app_weather_view_t *page=app_weather_view();
        if(!s_keyboard_visible&&page->result_total&&
            (ui_rect_hit(city_page_button(false),x,y)||ui_rect_hit(city_page_button(true),x,y))) {
            unsigned next=page->result_page;
            if(x<108&&next)--next;
            else if(x>=576&&(next+1)*WEATHER_CITY_LIMIT<page->result_total)++next;
            if(next!=page->result_page&&!app_weather_search(s_query,next))
                snprintf(s_notice,sizeof(s_notice),"暂不能搜索，请稍后重试并检查网络与时间");
            return APP_REDRAW_PAGE;
        }
        bool results_visible=!s_keyboard_visible;
        bool hidden=keyboard_hide();
        if(y>=165&&y<221&&x>=440&&x<648) {
            s_notice[0]=0;
            if(!app_weather_request(APP_WEATHER_LOCATE,NULL))snprintf(s_notice,sizeof(s_notice),"暂不能定位，请稍后重试并检查网络与时间");
            return APP_REDRAW_PAGE;
        }
        if(results_visible) {
            const app_weather_view_t *v=app_weather_view();
            for(unsigned i=0;i<v->result_count;i++)if(ui_rect_hit(city_result_row(i),x,y)) {
                weather_location_t city=v->results[i];
                if(app_weather_select(&city)){ui_keyboard_end();s_city=false;s_notice[0]=0;(void)app_weather_request(APP_WEATHER_FETCH,NULL);}
                else snprintf(s_notice,sizeof(s_notice),"保存失败，请检查设置存储");
                return APP_REDRAW_PAGE;
            }
        }
        if(ev->y0>=UI_NAV_TOP&&ev->x0>=228&&ev->x0<456)ui_nav_request(ctx,0);
        return hidden?APP_REDRAW_PAGE:feedback?keyboard_paint(ctx,false):APP_REDRAW_NONE;
    } else if(ev->type==UI_GESTURE_TAP&&!s_weather_settings) {
        int x=ev->x0,y=ev->y0;
        if(x>=36&&x<648) {
            for(unsigned slot=0;slot<APP_HOME_SLOT_COUNT;++slot)for(unsigned choice=0;choice<4;++choice) {
                int row=(slot?645:263)+(int)choice*76;
                if(y>=row-34&&y<row+34) {
                    if(!app_home_component_set(slot,s_choices[choice]))snprintf(s_notice,sizeof(s_notice),"设置保存失败，请稍后重试");
                    else {s_notice[0]=0;if(!app_home_has_widget(APP_HOME_WIDGET_WEATHER))app_weather_cancel_join();}
                    return APP_REDRAW_PAGE;
                }
            }
            if(y>=926&&y<1002){s_weather_settings=true;s_notice[0]=0;return APP_REDRAW_PAGE;}
        }
    } else if(ev->type==UI_GESTURE_TAP) {
        int x=ev->x0,y=ev->y0;
        if(y>=800&&y<876) {
            if(!app_weather_set_auto_refresh(!app_weather_view()->auto_refresh))snprintf(s_notice,sizeof(s_notice),"设置保存失败，请稍后重试");
            else s_notice[0]=0;
            return APP_REDRAW_PAGE;
        }
        if(y>=900&&y<972){city_begin();return APP_REDRAW_PAGE;}
        ui_weather_action_t action=ui_weather_hit((EpdRect){0,236,684,472},x,y);
        if(action==UI_WEATHER_SETTINGS){s_weather_settings=false;s_notice[0]=0;return APP_REDRAW_PAGE;}
        if(action==UI_WEATHER_CITY){city_begin();return APP_REDRAW_PAGE;}
        if(action==UI_WEATHER_TODAY||action==UI_WEATHER_TOMORROW){s_tomorrow=action==UI_WEATHER_TOMORROW;return APP_REDRAW_PAGE;}
        if(action==UI_WEATHER_REFRESH){(void)app_weather_request(APP_WEATHER_FETCH,NULL);return APP_REDRAW_PAGE;}
    }
    if(ev->type==UI_GESTURE_TAP&&ev->y0>=UI_NAV_TOP) {
        if(ev->x0>=228&&ev->x0<456)ui_nav_request(ctx,0);
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t tick(app_ctx_t *ctx) {
    if(ctx->consumed)return APP_REDRAW_NONE;
    if(app_weather_poll(!s_city&&app_home_has_widget(APP_HOME_WIDGET_WEATHER)))return APP_REDRAW_PAGE;
    if(s_city&&s_keyboard_visible) {
        bool held=!ctx->released&&ctx->touch&&ctx->touch->touched&&ctx->touch->count==1;
        if(ui_keyboard_hold_tick(held,held?ctx->touch->x:0,held?ctx->touch->y:0,ctx->now_ms)==UI_KEYBOARD_CHANGED) {
            return edited(ctx);
        }
        if(ui_keyboard_idle_tick(ctx->touch&&ctx->touch->touched,ctx->now_ms)==UI_KEYBOARD_CHANGED)return keyboard_paint(ctx,false);
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t key(app_ctx_t *ctx,int key_value) {
    if(key_value==UI_KEY_2)ui_nav_request(ctx,0);
    else back(ctx);
    return APP_REDRAW_PAGE;
}
static bool present(app_ctx_t *ctx,app_redraw_t redraw) {
    if(redraw!=APP_REDRAW_AREA)return false;
    display_refresh_intent_t intent=s_settle?DISPLAY_REFRESH_SETTLE:s_detail?DISPLAY_REFRESH_DETAIL:DISPLAY_REFRESH_FEEDBACK;
    guard_draw_result(ctx->hl,display_refresh_area(ctx->hl,intent,s_area,DISPLAY_REFRESH_DEFAULT));
    s_settle=false;
    return true;
}
static display_rect_t area(app_ctx_t *ctx) {(void)ctx;return display_rect_from_epd(s_area);}
static bool no_menu(app_ctx_t *ctx) {(void)ctx;return false;}
const app_desc_t app_components={
    .title="首页组件",.detail="上下区域与天气设置",.owns_keys=true,.enter_full=false,.menu_handle_enabled=no_menu,
    .on_enter=enter,.on_exit=leave,.on_before_lock=before_lock,.render=render,.present=present,
    .on_tick=tick,.on_gesture=gesture,.on_key=key,.area_hint=area,
};
