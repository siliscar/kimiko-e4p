/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：从主循环提取顶部快捷层，持有预览、局部呈现和服务操作；主循环保留调度及锁屏门控。
 * English: Top quick layer extracted from the loop, owning previews, local presentation and service actions; the loop retains scheduling and lock gates.
 */
#include "app_quick_layer.h"
#include "weather_service.h"
#include "app_navigation.h"
#include "app_device_settings_mode.h"
#include "app_transfer_mode.h"
#include "ble_page_turner.h"
#include "display.h"
#include "display_refresh.h"
#include "frontlight.h"
#include "ota_online.h"
#include "wifi_transfer.h"
#include "settings.h"
#include "ui_click_feedback.h"
#include "ui_kit.h"
#include "ui_quick_menu.h"
#include "usb_storage.h"

extern const app_desc_t app_transfer;
extern const app_desc_t app_device_settings;

static ui_quick_menu_t s_quick;
static unsigned s_quick_status;
static int s_quick_pressed=-1;
// 上一次推上屏的快捷层高度；收起前光卡片后按旧高度擦除，避免 224..480 残留。
// Height last presented; closing the frontlight card erases by the old height so 224..480 cannot remain.
static int s_quick_height;
// 回放期间只更新硬件，整批结束后再按最终值刷一行；否则一次拖动会被每个积压帧各刷一遍。
// During replay only hardware follows; one row is flushed at the end, or a single drag would present every queued frame.
static bool s_fl_pending;
static bool s_fl_pending_commit;
static ui_quick_control_t s_fl_pending_control;
// 屏幕上第四枚圆钮当前画出的开灯状态；点按翻转后据此决定是否重画。
// The frontlight state currently painted on the fourth circle; a tap uses it to decide the repaint.
static bool s_fl_shown_on;
// 只提交本层尚未保存的调节；关闭后的旧预览不能覆盖实体键已保存的新状态。
// Commit only unsaved changes made here; a closed layer's stale preview must not overwrite a saved key change.
static bool s_fl_dirty;
// 落盘只发生在松手、点选或关闭/读错时，一次拖动只写一遍 NVS。
// NVS writes happen on release, tap, close or a read error, once per drag.
static void quick_frontlight_persist(void) {
    if (!s_quick.frontlight || !s_fl_dirty) return;
    app_settings_set_frontlight_on(s_quick.fl_on);
    app_settings_set_frontlight_brightness(s_quick.fl_bri);
    app_settings_set_frontlight_cct(s_quick.fl_cct);
    s_fl_dirty = false;
}
void app_quick_layer_reset(void) {
    quick_frontlight_persist();
    ui_quick_menu_reset(&s_quick);
    ui_quick_menu_end();
    s_quick_pressed=-1;
    s_quick_height=0;
    s_fl_pending=false;
    s_fl_pending_commit=false;
    s_fl_pending_control=UI_QUICK_CTL_NONE;
    s_fl_shown_on=s_quick.fl_on;
}
static unsigned quick_status(void) {
    wifi_transfer_status_t status;
    wifi_transfer_get_status(&status);
    bool wifi = status.mode == WIFI_TRANSFER_MODE_STA && status.state != WIFI_TRANSFER_STOPPED;
    return (wifi ? 1u : 0u) | (app_settings_ble_turner() ? 2u : 0u) | (status.network_ready ? 4u : 0u);
}

static void quick_rotation_sync(app_ctx_t *ctx, const app_desc_t *current) {
    s_quick.rotation_available = current->rotation_available && current->rotation_available(ctx);
    s_quick.rotation_enabled = s_quick.rotation_available && current->rotation_enabled && current->rotation_enabled(ctx);
}

// 同步输出完再接收下一条触摸，不安排旧层回放；错误时关闭层并可靠全刷当前页。
// Complete presentation before accepting another contact, with no stale replay; close and full-refresh the current page on error.
void app_quick_layer_present(app_ctx_t *ctx, const app_desc_t *current, bool full) {
    ui_click_feedback_begin(ctx->fb);
    display_main_transition_cancel();
    // 整层重绘会带上最新前光状态，积压的局部行刷新作废。
    // A whole-layer repaint already carries the latest frontlight state, so queued row refreshes are dropped.
    s_fl_pending = false;
    s_fl_pending_commit = false;
    s_fl_pending_control = UI_QUICK_CTL_NONE;
    unsigned status=quick_status();
    if(full&&s_quick.open){
        // 已合成的页面直接全刷，只还原圆键按压，不重排底页或重算玻璃。
        // Refresh the composed framebuffer directly, restoring only the pressed tile without underlay layout or glass regeneration.
        for(int i=0;i<ui_quick_menu_button_count(&s_quick);++i){
            bool active=i<2?(status&(1u<<i))!=0
                :i==3&&s_quick.frontlight&&s_quick.fl_on;
            bool was_active=i<2?(s_quick_status&(1u<<i))!=0
                :i==3&&s_quick.frontlight&&s_fl_shown_on;
            if(i==s_quick_pressed||active!=was_active){
                EpdRect dirty;(void)ui_quick_menu_draw_button(ctx->fb,&s_quick,i,active,false,&dirty);
            }
        }
        if((status^s_quick_status)&6u)ui_quick_menu_draw_status(ctx->fb);
    }else{
        if (current->render) current->render(ctx, ctx->fb);
        if(!s_quick.open)ui_quick_menu_end();
        else ui_quick_menu_draw(ctx->fb,&s_quick,status&1u,status&2u);
    }
    s_fl_shown_on = s_quick.fl_on;
    s_quick_status=status;
    s_quick_pressed=-1;
    const int quick_height = ui_quick_menu_rect(&s_quick).height;
    const int area_height = quick_height > s_quick_height ? quick_height : s_quick_height;
    s_quick_height = quick_height;
    // 收起后整屏归一，统一覆盖区内外的字芯驱动；展开仍只推快捷栏覆盖带。
    // 是否保护图片灰阶由当前页面决定：只有阅读背景正文需要，其他页面完整归一。
    // Normalize the whole screen on close so covered and uncovered glyph cores share the
    // same drive history; opening pushes only the quick band. The visible page decides
    // whether to protect picture grays: only a background reading body does.
    display_refresh_intent_t intent = DISPLAY_REFRESH_OVERLAY;
    unsigned flags = DISPLAY_REFRESH_DEFAULT;
    EpdRect area = {0, 0, epd_rotated_display_width(), area_height};
    if (!s_quick.open) {
        intent = DISPLAY_REFRESH_RESTORE;
        if (current->normalize_hold_gray && current->normalize_hold_gray(ctx))
            flags = DISPLAY_REFRESH_HOLD_GRAY;
        area.height = epd_rotated_display_height();
    }
    enum EpdDrawError error = full ? update_display_full(ctx->hl)
        : display_refresh_area(ctx->hl, intent, area, flags);
    guard_draw_result(ctx->hl, error);
    if (error != EPD_DRAW_SUCCESS) {
        app_quick_layer_reset();
        app_present(ctx, current, APP_REDRAW_FULL);
    }
}

// 菜单打开期间只改圆键和必要的状态栏；按下不排字、不重画页面或重算玻璃。
// While open, update only keys and the necessary status strip; pressing never shapes text, renders a page or regenerates glass.
void app_quick_layer_present_buttons(app_ctx_t *ctx,const app_desc_t *current) {
    if(!s_quick.open)return;
    unsigned status=quick_status();
    int pressed=ui_quick_menu_pressed(&s_quick);
    enum EpdDrawError error=EPD_DRAW_SUCCESS;
    for(int i=0;i<ui_quick_menu_button_count(&s_quick);++i){
        bool active=i<2?(status&(1u<<i))!=0
            :i==3&&s_quick.frontlight&&s_quick.fl_on;
        bool was_active=i<2?(s_quick_status&(1u<<i))!=0
            :i==3&&s_quick.frontlight&&s_fl_shown_on;
        if(active==was_active&&(pressed==i)==(s_quick_pressed==i))continue;
        EpdRect dirty;
        if(ui_quick_menu_draw_button(ctx->fb,&s_quick,i,active,pressed==i,&dirty)){
            error=display_refresh_area(ctx->hl, DISPLAY_REFRESH_FEEDBACK, dirty, DISPLAY_REFRESH_DEFAULT);
            guard_draw_result(ctx->hl,error);
            if(error!=EPD_DRAW_SUCCESS)break;
        }
    }
    bool network_changed=(status&5u)==5u,was_network=(s_quick_status&5u)==5u;
    if(error==EPD_DRAW_SUCCESS&&(((status^s_quick_status)&2u)||network_changed!=was_network)){
        ui_quick_menu_draw_status(ctx->fb);
        error=display_refresh_area(ctx->hl, DISPLAY_REFRESH_DETAIL, (EpdRect){0,0,epd_rotated_display_width(),76}, DISPLAY_REFRESH_DEFAULT);
        guard_draw_result(ctx->hl,error);
    }
    if(error!=EPD_DRAW_SUCCESS){
        app_quick_layer_reset();app_present(ctx,current,APP_REDRAW_FULL);return;
    }
    s_quick_status=status;s_fl_shown_on=s_quick.fl_on;s_quick_pressed=pressed;
}

static void quick_toggle_wifi(void) {
    app_weather_cancel_join();
    wifi_transfer_status_t status;
    wifi_transfer_get_status(&status);
    if (status.state != WIFI_TRANSFER_STOPPED) {
        // 上传期间拒绝断开，复用存储任务已有的原子准入保护。
        // Refuse disconnecting during upload using the storage owner's atomic admission guard.
        (void)wifi_transfer_try_stop_if_idle();
    } else {
        char ssid[33] = {0}; bool configured = false;
        if (wifi_transfer_get_saved_wifi(ssid, &configured) == ESP_OK && configured) {
            // 先释放蓝牙占用，再用既有异步连接/重试流程；不新开上传服务器或改写凭据。
            // Release Bluetooth resources first, then reuse connection/retry handling without starting uploads or rewriting credentials.
            ble_pt_sync(false);
            const wifi_transfer_cfg_t cfg = {.mode = WIFI_TRANSFER_MODE_STA, .network_only = true};
            (void)wifi_transfer_start(&cfg);
        }
    }
}

// 前光开关改变层高时整层重绘；黑白滑杆拖动与松手共用完整 DU 差分，不追加定稿。
// Repaint the layer when the light switch changes its height; binary slider motion and release share complete DU differences without a settle pass.
static void quick_frontlight_present(app_ctx_t *ctx, const app_desc_t *current,
                                     const ui_quick_event_t *event) {
    EpdRect dirty;
    enum EpdDrawError error = EPD_DRAW_SUCCESS;
    if (event->control == UI_QUICK_CTL_ON) {
        app_quick_layer_present(ctx, current, false);
        return;
    }
    if (event->commit) {
        // 松手只提交积压终态；已显示时差分为空。/ Release submits only a coalesced final state; an already displayed state has no differences.
        if (ui_quick_menu_draw_control(ctx->fb, &s_quick, event->control, &dirty)) {
            error = display_refresh_area(ctx->hl, DISPLAY_REFRESH_CAPSULE, dirty, DISPLAY_REFRESH_DEFAULT);
            guard_draw_result(ctx->hl, error);
        }
        // 提交完成，脏范围收回当前档位；跟踪保留，便于恢复定稿时直接使用。
        // Committed: the dirty extent shrinks back to the current level; the
        // tracking stays so a restored settle can use it directly.
        if (error == EPD_DRAW_SUCCESS) {
            if (event->control == UI_QUICK_CTL_BRIGHTNESS) s_quick.fl_bri_dirty = s_quick.fl_bri;
            else if (event->control == UI_QUICK_CTL_CCT) s_quick.fl_cct_dirty = s_quick.fl_cct;
        }
    } else {
        if (!ui_quick_menu_draw_control(ctx->fb, &s_quick, event->control, &dirty)) return;
        // 固定图标与胶囊轮廓保持，只有黑白填充边界改变。/ Hold icons and capsule outlines; only the binary fill boundary changes.
        error = display_refresh_area(ctx->hl, DISPLAY_REFRESH_CAPSULE, dirty, DISPLAY_REFRESH_DEFAULT);
        guard_draw_result(ctx->hl, error);
    }
    if (error != EPD_DRAW_SUCCESS) {
        app_quick_layer_reset();
        app_present(ctx, current, APP_REDRAW_FULL);
    }
}

// coalesce：回放积压帧时只更新硬件，等整批结束由 app_quick_layer_flush_frontlight 刷一次。
// coalesce: while replaying queued frames only hardware follows; one flush paints the final row.
static void quick_frontlight_event(app_ctx_t *ctx, const app_desc_t *current,
                                   const ui_quick_event_t *event, bool coalesce) {
    if (!s_quick.frontlight) return;
    s_fl_dirty = true;
    (void)frontlight_apply(s_quick.fl_on, s_quick.fl_bri, s_quick.fl_cct);
    if (event->commit) quick_frontlight_persist();
    if (!coalesce) {
        quick_frontlight_present(ctx, current, event);
        return;
    }
    s_fl_pending = true;
    s_fl_pending_control = event->control;
    s_fl_pending_commit = s_fl_pending_commit || event->commit;
}

void app_quick_layer_flush_frontlight(app_ctx_t *ctx, const app_desc_t *current) {
    if (!s_fl_pending) return;
    const ui_quick_event_t event = {
        .control = s_fl_pending_control,
        .commit = s_fl_pending_commit,
    };
    s_fl_pending = false;
    s_fl_pending_commit = false;
    s_fl_pending_control = UI_QUICK_CTL_NONE;
    quick_frontlight_present(ctx, current, &event);
}

// 目标页就是当前页：重进一次让 on_enter 消费待定子视图，再整页重绘。快捷层已在
// 输入层收起，这里只负责把页面画回来；通用切页分支保持「同页请求不动作」。
// The target page is already current: re-enter so on_enter consumes the pending
// sub-view, then repaint the page. The quick layer already closed in the input
// layer, and the generic transition branch keeps treating a same-page request as
// a no-op (tapping the active tab must drop a pending switch).
static void quick_reenter_page(app_ctx_t *ctx, const app_desc_t *current) {
    if (current->on_exit) current->on_exit(ctx);
    if (current->on_enter) current->on_enter(ctx);
    app_present(ctx, current, APP_REDRAW_PAGE);
}


// 快捷层单帧输入：识别并执行动作；本帧由快捷层持有输入时返回 true。
// 正常 tick 与采样回放共用同一入口，回放不能只看最后一帧，否则按下沿会丢。
// One quick-layer input frame: recognize and run the action; true when the layer owns
// the frame. The normal tick and sampler replay share it so replay never drops a press edge.
bool app_quick_layer_feed_frame(app_ctx_t *ctx, const app_desc_t *current, bool *lock_requested,
                             bool coalesce) {
    quick_rotation_sync(ctx, current);
    ui_quick_event_t event;
    bool owned = ui_quick_menu_feed(&s_quick, ctx,
        current->render && !current->holds_pmu && !usb_storage_active() && !pico_online_busy(),
        &event);
    if (owned) ctx->consumed = true;
    if (event.action == UI_QUICK_OPEN || event.action == UI_QUICK_CLOSE) {
        if (event.action == UI_QUICK_OPEN) {
            // 阅读键可以直接开关前光；开层前读回设置，预览和关层落盘都不会写成旧状态。
            // A reading key can toggle the frontlight directly; reload the settings before opening
            // so neither the preview nor the close-time write resurrects a stale state.
            ui_quick_menu_set_frontlight(&s_quick, frontlight_ready(), app_settings_frontlight_on(),
                                         app_settings_frontlight_brightness(), app_settings_frontlight_cct());
            s_fl_shown_on = s_quick.fl_on;
        } else {
            // 收起前把预览值落盘：权限变化或 USB 抢占导致的关闭也走这里。
            // Persist previews before closing, including permission changes or USB preemption.
            quick_frontlight_persist();
        }
        app_quick_layer_present(ctx, current, false);
    }
    // 灯灭时继续下拉临时展开胶囊：不碰设置与前光硬件，只按新层高整层重排；
    // 重排出口已按新旧高度并集推 GL16，展开 264→480 由同一机制覆盖。
    // Pulling the open layer further down with the light off expands the
    // capsules for this opening: no settings or frontlight hardware change, just
    // a whole-layer re-composite; its push already covers the new/old height
    // union with GL16, which carries the 264→480 growth.
    else if (event.action == UI_QUICK_EXPAND) app_quick_layer_present(ctx, current, false);
    else if (event.action == UI_QUICK_WIFI) quick_toggle_wifi();
    else if (event.action == UI_QUICK_BLUETOOTH) {
        bool enable = !app_settings_ble_turner();
        app_settings_set_ble_turner(enable);
        if (enable) ble_pt_reconnect_recent();
        else { ble_pt_cancel_recent(); ble_pt_sync(false); }
    } else if (event.action == UI_QUICK_WIFI_SETTINGS) {
        // 长按 WiFi 圆键：与设置页「WiFi」行同一入口，直接进入配网设置页。已在
        // 该页时重进一次，让 on_enter 重新消费配网请求。
        // WiFi hold: the same entry as the settings page's WiFi row, straight into
        // provisioning. When already there, re-enter so on_enter consumes the request again.
        app_transfer_request_wifi_setup();
        if (current == &app_transfer) quick_reenter_page(ctx, current);
        else {
            app_navigation_capture_reader_return(current);
            ctx->request_app = &app_transfer;
        }
    } else if (event.action == UI_QUICK_BLUETOOTH_SETTINGS) {
        // 长按蓝牙圆键：直接进入设置页的蓝牙翻页器子页；已在设置页时同上。从其他
        // 页面进入时，子页左上角返回回到来源页。
        // Bluetooth hold: straight into the settings page's Bluetooth turner sub-page;
        // when already in settings, re-enter the same way. Entering from another page
        // makes the sub-page's top-left back return to the origin.
        if (current == &app_device_settings) {
            app_device_settings_request_bluetooth(false);
            quick_reenter_page(ctx, current);
        } else {
            app_navigation_capture_reader_return(current);
            app_device_settings_request_bluetooth(true);
            ctx->request_app = &app_device_settings;
        }
    } else if (event.action == UI_QUICK_FULL) app_quick_layer_present(ctx, current, true);
    else if (event.action == UI_QUICK_LOCK) *lock_requested = true;
    else if (event.action == UI_QUICK_ROTATION && s_quick.rotation_available && current->on_rotation_toggle) {
        current->on_rotation_toggle(ctx);
        quick_rotation_sync(ctx, current);
        EpdRect dirty;
        if (ui_quick_menu_draw_button(ctx->fb, &s_quick, 4, s_quick.rotation_enabled, false, &dirty)) {
            enum EpdDrawError error = display_refresh_area(ctx->hl, DISPLAY_REFRESH_FEEDBACK, dirty, DISPLAY_REFRESH_DEFAULT);
            guard_draw_result(ctx->hl, error);
            if (error != EPD_DRAW_SUCCESS) { app_quick_layer_reset(); app_present(ctx,current,APP_REDRAW_FULL); }
        }
    }
    if (event.control != UI_QUICK_CTL_NONE) quick_frontlight_event(ctx, current, &event, coalesce);
    return owned;
}


void app_quick_layer_init(void) {
    app_quick_layer_reset();
    // 本板有没有前光决定层高和卡片；预览值来自已加载的设置，开机恢复由 app_main 写入硬件。
    // Board capability decides layer height and card presence; previews come from settings and app_main restored the hardware at boot.
    ui_quick_menu_set_frontlight(&s_quick, frontlight_ready(), app_settings_frontlight_on(),
                                 app_settings_frontlight_brightness(), app_settings_frontlight_cct());
    s_fl_shown_on = s_quick.fl_on;
}

bool app_quick_layer_is_open(void) {
    return s_quick.open;
}

void app_quick_layer_discard_frontlight(void) {
    s_fl_pending = false;
    s_fl_pending_commit = false;
    s_fl_pending_control = UI_QUICK_CTL_NONE;
}

bool app_quick_layer_cancel_input(void) {
    ui_quick_menu_cancel_input(&s_quick);
    // 读点错误取消拖动时把当前档位落盘，避免硬件与 NVS 长期不一致。
    // Persist the current step when a read error cancels a drag so hardware and NVS do not diverge.
    quick_frontlight_persist();
    return s_quick.open;
}

void app_quick_layer_sync_status(app_ctx_t *ctx, const app_desc_t *current) {
    if (s_quick.open && quick_status() != s_quick_status) app_quick_layer_present_buttons(ctx,current);
}
