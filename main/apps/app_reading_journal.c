/*
 * SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 *
 * 中文：本月日历及逐日书目详情；底栏仅首页，插画与首页共享。
 * English: Current-month calendar and daily book details; Home-only footer and shared scenes.
 */
#include "app.h"
#include "app_reading_journal_mode.h"
#include "reading_journal.h"
#include "ui_reading_journal.h"
#include "ui_gesture.h"
#include "ui_kit.h"
#include "ui_menu.h"
#include "ui_nav.h"

extern const app_desc_t app_reading_journal;
static unsigned s_selected,s_page;
static int32_t s_opened_day;
static unsigned s_opened_today,s_opened_month;
static void enter(app_ctx_t *ctx) {
    (void)ctx;(void)app_reading_journal_poll();
    const reading_journal_date_t *today=app_reading_journal_today();
    s_selected=(unsigned)today->day;s_opened_day=today->serial;s_page=0;
    s_opened_today=s_selected;s_opened_month=(unsigned)(today->year*100+today->month);
}
void app_reading_journal_open(app_ctx_t *ctx) {ctx->request_app=&app_reading_journal;}
static void render(app_ctx_t *ctx,uint8_t *fb) {
    (void)ctx;ui_clear_page(fb);ui_nav_status(fb);
    ui_reading_journal_detail(fb,app_reading_journal_model(),app_reading_journal_today(),s_selected,s_page,app_reading_journal_notice());
    ui_draw_separator(fb,UI_NAV_TOP,0,684,0x90);ui_draw_icon(fb,342,1164,44,UI_ICON_HOUSE,0);
}
static app_redraw_t page_move(int direction) {
    unsigned books=reading_journal_totals(app_reading_journal_model(),s_selected).books;
    if(direction<0 && s_page) {--s_page;return APP_REDRAW_PAGE;}
    if(direction>0 && (s_page+1)*2<books) {++s_page;return APP_REDRAW_PAGE;}
    return APP_REDRAW_NONE;
}
static app_redraw_t gesture(app_ctx_t *ctx,const ui_gesture_event_t *event) {
    if(event->type==UI_GESTURE_SWIPE_L && event->y0>=860 && event->y0<UI_NAV_TOP)return page_move(1);
    if(event->type==UI_GESTURE_SWIPE_R && event->y0>=860 && event->y0<UI_NAV_TOP)return page_move(-1);
    if(event->type!=UI_GESTURE_TAP)return APP_REDRAW_NONE;
    int x=event->x0,y=event->y0;
    if(y>=UI_NAV_TOP && x>=228 && x<456) {ui_nav_request(ctx,0);return APP_REDRAW_NONE;}
    if(ui_rect_hit((EpdRect){438,171,210,162},x,y)) {
        (void)app_reading_journal_next_scene();return APP_REDRAW_PAGE;
    }
    unsigned day=ui_reading_journal_date_hit(app_reading_journal_today(),x,y);
    if(day && day!=s_selected) {s_selected=day;s_page=0;return APP_REDRAW_PAGE;}
    if(y>=1058 && y<UI_NAV_TOP) {
        if(x>=24 && x<92)return page_move(-1);
        if(x>=592 && x<660)return page_move(1);
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t tick(app_ctx_t *ctx) {
    if(ctx->consumed || (ctx->touch && ctx->touch->touched))return APP_REDRAW_NONE;
    if(!app_reading_journal_poll())return APP_REDRAW_NONE;
    const reading_journal_date_t *today=app_reading_journal_today();
    if(s_opened_day!=today->serial) {
        // 正看昨天时保留选择；正看今天则跟随午夜，跨月回到新今天。
        // Preserve historical selection; follow midnight from Today, and reset at month boundaries.
        if(!today->serial || !s_opened_day || s_selected==s_opened_today ||
           s_selected>(unsigned)today->day || s_opened_month!=(unsigned)(today->year*100+today->month))s_selected=(unsigned)today->day;
        s_opened_day=today->serial;s_opened_today=(unsigned)today->day;
        s_opened_month=(unsigned)(today->year*100+today->month);s_page=0;
    }
    return APP_REDRAW_PAGE;
}
static void flush(app_ctx_t *ctx) {(void)ctx;(void)app_reading_journal_flush();}
static app_redraw_t key(app_ctx_t *ctx,int value) {
    if(value==UI_KEY_2)ui_nav_request(ctx,0);
    else ctx->request_return=true;
    return APP_REDRAW_NONE;
}
static app_redraw_t held_key(app_ctx_t *ctx,int value) {(void)ctx;return value==UI_KEY_2?APP_REDRAW_FULL:APP_REDRAW_NONE;}
static bool no_handle(app_ctx_t *ctx) {(void)ctx;return false;}
const app_desc_t app_reading_journal={
    .title="阅读手账",.detail="本月阅读记录",.enter_full=false,.owns_keys=true,
    .defer_middle_short=true,.on_key=key,.on_key_long=held_key,
    .menu_handle_enabled=no_handle,.on_enter=enter,.on_exit=flush,.on_before_lock=flush,
    .render=render,.on_gesture=gesture,.on_tick=tick,
};
