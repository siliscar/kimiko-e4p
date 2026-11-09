/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：从主循环提取导航动作，保持页面回调、系统字体、返回恢复与呈现顺序。
 * English: Navigation extracted from the loop, retaining page callbacks, system fonts, return restoration and presentation ordering.
 */
#include "app_navigation.h"
#include "app_registry.h"
#include "app_content_open.h"
#include "ttf_font.h"
#include "app_font_context.h"
#include "display.h"
#include "time_service.h"
#include "ota_auto.h"
#include "ui_kit.h"
#include "ui_menu.h"
#include "ui_nav.h"
#include "esp_log.h"

#define TAG "app_loop"
extern const app_desc_t app_transfer;
extern const app_desc_t app_book;

// 快捷栏从正文离开时保留原有一次性返回快照，其他导航不扩大其有效期。
// Retain the existing one-shot quick-layer reader snapshot without extending its lifetime through other navigation.
static bool s_quick_return_body;
static char s_quick_return_path[256];
static bool s_quick_return_fullscreen;
static bool s_nav_contact_down, s_nav_polled;

bool app_navigation_main_page_ready(app_ctx_t* ctx, const app_desc_t* app) {
    int index = app_index_of(app);
    return index >= 0 && index < 4 &&
           app->main_page_visible && app->main_page_visible(ctx);
}

app_redraw_t app_navigation_main_key_action(app_ctx_t *ctx,const app_desc_t *current,int key,bool held) {
    if(held)return key==UI_KEY_2?APP_REDRAW_FULL:APP_REDRAW_NONE;
    app_redraw_t redraw=APP_REDRAW_NONE;
    if(current->on_main_key && current->on_main_key(ctx,key,&redraw))return redraw;
    int tab=app_index_of(current);
    if(tab>=0 && tab<4 && (key==UI_KEY_1||key==UI_KEY_3))
        ui_nav_request(ctx,(tab+(key==UI_KEY_1?3:1))%4);
    return APP_REDRAW_NONE;
}

static void app_navigation_switch_to(
    app_ctx_t* ctx, const app_desc_t** current, const app_desc_t* next
) {
    if (next == NULL || next == *current) return;
    pico_ota_auto_cancel();
    bool main_source = app_navigation_main_page_ready(ctx, *current);
    bool reader_source = *current == &app_book && !main_source;
    bool shelf_source = main_source && app_index_of(*current) == 1;
    display_main_transition_cancel();
    if ((*current)->on_exit != NULL) (*current)->on_exit(ctx);
    // 传书页独占无线：进页前取消在途的后台对时，页面的会话才起得来。
    // The transfer page owns the radio: an in-flight background sync cancels before entry, or the
    // page's own session cannot start.
    if (next == &app_transfer) app_time_service_cancel();
    *current = next;
    app_font_activate_system();
    if (next->on_enter != NULL) next->on_enter(ctx);
    if (main_source && app_navigation_main_page_ready(ctx, next)) display_main_transition_arm(next, shelf_source, true);
    const bool reader_return = reader_source && app_navigation_main_page_ready(ctx, next);
    app_present(ctx, next, reader_return || next->enter_full ? APP_REDRAW_FULL : APP_REDRAW_PAGE);
    display_main_transition_disarm();
    ESP_LOGI(TAG, "page -> %s", next->title);
}

void app_navigation_capture_reader_return(const app_desc_t *current) {
    s_quick_return_body = current == &app_book && app_book_reader_body_visible() &&
        app_book_resume_context(s_quick_return_path, sizeof(s_quick_return_path),
                                &s_quick_return_fullscreen);
}

bool app_poll_main_navigation(app_ctx_t *ctx) {
    if(!ctx||!ctx->read_touch)return false;
    if(ctx->request_app)return true;
    app_touch_t touch={0};
    if(ctx->read_touch(ctx->touch_context,&touch)!=ESP_OK||!touch.touched||touch.count!=1) {
        s_nav_contact_down=s_nav_polled=false;return false;
    }
    int index=ui_nav_hit(touch.x,touch.y);
    if(index<0)return false;
    if(!s_nav_contact_down) {
        s_nav_contact_down=s_nav_polled=true;
        if(index!=1)ui_nav_request(ctx,index);
        ctx->user_activity=true;
    }
    return ctx->request_app!=NULL;
}

void app_navigation_reset_contact(void) {
    s_nav_contact_down = s_nav_polled = false;
}

void app_navigation_observe_contact(esp_err_t error, bool touched) {
    if (error != ESP_OK || !touched) app_navigation_reset_contact();
}

bool app_navigation_contact_polled(void) { return s_nav_polled; }

void app_navigation_claim_contact(void) { s_nav_contact_down = true; }

void app_navigation_apply_request(app_ctx_t *ctx, const app_desc_t **current,
                                   bool *menu_open, int *menu_leaf,
                                   app_navigation_history_t *history,
                                   app_navigation_request_t request,
                                   app_navigation_menu_present_t present_menu,
                                   void *menu_context) {
    const app_desc_t *next = request.next;
    if (request.go_back) {
        bool main_source = app_navigation_main_page_ready(ctx, *current);
        bool reader_source = *current == &app_book && !main_source;
        bool shelf_source = main_source && app_index_of(*current) == 1;
        display_main_transition_cancel();
        *menu_open = history->return_app ? history->return_to_menu : true;
        *menu_leaf = history->return_app ? history->return_menu_leaf : ui_menu_leaf_for_app(*current);
        if (history->return_app) {
            if ((*current)->on_exit) (*current)->on_exit(ctx);
            *current = history->return_app;
            history->return_app = NULL;
            app_font_activate_system();
            // 快捷栏长按离开阅读正文后的返回：按离开时的书与全屏状态重开正文。
            // Returning from a quick-bar hold that left the reading body reopens
            // that book and full-screen state instead of landing on the shelf.
            if (*current == &app_book && s_quick_return_body)
                (void)app_book_request_return_to_body(s_quick_return_path,
                                                     s_quick_return_fullscreen);
            if ((*current)->on_enter) (*current)->on_enter(ctx);
            // 初始化后恢复通用页码，避免 on_enter 的默认页码覆盖来源。
            // Restore the shared leaf after initialization overrides its default.
            ctx->leaf = history->return_leaf;
        }
        // 一次返回就用掉恢复点；没有来源页时也不留到下次。
        // One return consumes the resume point, even when there is no origin page.
        s_quick_return_body = false;
        if (*menu_open) present_menu(ctx, *current, *menu_leaf, menu_context);
        else {
            if (main_source && app_navigation_main_page_ready(ctx, *current))
                display_main_transition_arm(*current, shelf_source, true);
            const bool reader_return = reader_source && app_navigation_main_page_ready(ctx, *current);
            app_present(ctx, *current, reader_return ? APP_REDRAW_FULL : APP_REDRAW_PAGE);
            display_main_transition_disarm();
        }
    } else if (next) {
        if (next != *current) {
            // 快捷栏从正文离开的那次切页保留恢复点；之后从别的页面再切页就作废。
            // Keep the resume point for the switch that left the reading body; a
            // later switch from another page invalidates it.
            if (*current != &app_book) s_quick_return_body = false;
            history->return_app = *current;
            history->return_leaf = ctx->leaf;
            history->return_to_menu = request.selected_from_menu;
            history->return_menu_leaf = *menu_leaf;
        }
        *menu_open = false;
        app_navigation_switch_to(ctx, current, next);
    } else {
        *menu_open = true;
        *menu_leaf = ui_menu_leaf_for_app(*current);
        present_menu(ctx, *current, *menu_leaf, menu_context);
    }
}
