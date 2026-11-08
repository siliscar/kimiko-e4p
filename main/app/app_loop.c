/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 事件循环。只做和页面无关的事：读触摸并去抖、三个实体键与菜单把手的路由、切页
 * 时调 on_exit/on_enter、按回调返回值决定怎么刷屏、电源键锁屏、SD 卡上的字体延迟
 * 加载。页面自己的状态和刷新节奏都在 main/apps/ 各自的文件里。
 *
 * Event loop for page-agnostic work: touch debounce, the three keys and
 * the menu handle, on_exit/on_enter, present, lock, delayed SD font load.
 * Page state and refresh cadence stay in main/apps/; gesture recognition is shared.
 *
 * 冻结（用户2026-10-16修订）：四个主页面左右键循环切换，中键长按全刷；书架中键短按切换左右键的分页控制。
 * 主页面中键短按延后到抬手，长按不执行短按；离开主页面立即取消按住状态。
 * 阅读和子页面沿用自己的映射与手势，把手按下即触发。
 * Frozen (user revision 2026-10-16): main roots cycle via side keys; middle hold refreshes fully, and a shelf middle tap toggles paging control.
 * Defer the root middle tap until release, suppress it after a hold, and cancel held input on root exit.
 * Reader/subpages retain their mappings and gestures; the menu handle still fires on contact.
 * 用户修订：四个主页面底栏作为统一导航路由在按下时报点即响应，模态和阅读手势保持原归属。
 * User revision: root-tab routing commits on initial contact; modal controls and reader gestures retain their owners.
 * 用户修订：系统页面切换使用 GL16，不再每次 GC16 全刷；锁屏、手动强刷和周期清残影仍可全刷。
 * User revision: system page changes use GL16 instead of GC16; lock, manual refresh and periodic ghost cleanup may still use full refresh.
 * 用户修订：设置只保留普通与水波纹，默认水波纹；快刷按用户 2026-10-21 要求撤下，旧设置与旧备份加载时回落默认，黑白渲染代码保留但不再可达。普通保留原灰阶路线。用户反馈残影和黑带后，书架退出只同向擦白，主页水波纹改白底/24窄带且端点单向推动，中灰保留校准序列；五行横条局部移动、图标固定，没有异步补刷。
 * User revision: settings keep ordinary and water only, defaulting to water; fast refresh was withdrawn per the user on 2026-10-21, so stored settings and older backups fall back to the default and the monochrome renderer remains but is unreachable. Ordinary retains the original gray path. Following reported ghosts and dark bands, shelf exit whitens directionally; main water uses white backgrounds, 24 narrow bands and directional endpoints, retaining calibrated midgrays. Move the five-row marker locally, retain icons, and schedule no asynchronous completion.
 * 用户修订：普通模式也固定主页面底栏，仅局部移动横条。普通及水波纹保留灰阶，首次进入及解锁仍完整绘制底栏。
 * User revision: ordinary main screens also retain navigation and move only its marker locally. Ordinary/water retain grays; initial entry and unlock still draw complete navigation.
 * 冻结：返回消费最近切页来源，恢复原页及菜单位置，不跳固定首页。
 * Frozen: Return consumes the latest page origin, restoring its page and menu position.
 * 用户采用顶部快捷层：仅顶部32px起手下滑72px才接管，覆盖时冻结页内输入/轮询，WiFi直接连接最近保存网络/再次断开，蓝牙开启并异步连接最近成功的翻页器，全刷和锁屏复用原路径。用户要求三个模式均真实灰阶并加深轮廓；关闭重绘当前页并局部整理，不修改底栏。
 * User adopts the top quick layer: capture only a 72px downward swipe starting within the top 32px; suspend page input/ticks while covered. WiFi connects saved credentials or disconnects on the next tap; Bluetooth enables and queues a connection to the most recently successful turner. Reuse full refresh and lock. Per the user, all modes retain grays with clearer outlines. Closing redraws and locally settles the underlay, retaining navigation.
 * 用户修订：快捷层只在展开/收起重绘顶部；圆键按下立即局部DU反馈，状态变化不重排底页或生成玻璃。
 * User revision: only opening/closing repaints the top; key presses receive immediate local DU feedback without underlay re-layout or glass regeneration.
 * 用户新增：快捷层 WiFi / 蓝牙圆键按住 0.5 秒直达对应设置页（配网页 / 蓝牙翻页器子页），点按仍只切换开关。
 * User addition: a 0.5s hold on the quick layer's WiFi / Bluetooth circle opens its settings page (provisioning / Bluetooth turner sub-page); a tap still only toggles the switch.
 * 用户点击反馈：底栏及圆形返回键按下局部放大，释放复位；只暂存一个小控件，切页、全刷及锁屏丢弃缓存，不回放旧画面。
 * User click feedback: locally enlarge navigation/round backs on press and restore on release. Retain one small control only; navigation, full refresh and lock discard it without replaying old frames.
 * 用户修订：确认主动锁屏后，拼贴样式先给局部圆锁反馈，再保存/等待阅读预渲染；自动休眠和直接关机/重启不加此反馈。
 * User revision: acknowledge accepted manual collage locks before saves/reader joins; omit the badge for automatic sleep and direct shutdown/restart.
 */

#include "../diagnostics/reader_perf.h"
#include "app_loop.h"
#include "app_lifecycle.h"
#include "platform/s31_input.h"
#include "app_input.h"
#include "app_media.h"
#include "app_navigation.h"
#include "auto_lock.h"
#include "crash_note.h"

#include <string.h>

#include "app_registry.h"
#include "app_content_open.h"
#include "app_device_settings_mode.h"
#include "app_transfer_mode.h"
#include "boot_state.h"
#include "ble_page_turner.h"
#include "ota_online.h"
#include "display.h"
#include "display_refresh.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_hardware.h"
#include "board_crashlog.h"
#include "device_power.h"
#include "wifi_transfer.h"
#include "settings.h"
#include "haptics.h"
#include "sleep.h"
#include "frontlight.h"
#include "time_service.h"
#include "weather_service.h"
#include "ota_auto.h"
#include "ui_kit.h"
#include "ui_menu.h"
#include "ui_nav.h"
#include "ui_gesture.h"
#include "ui_power_dialog.h"
#include "ui_quick_menu.h"
#include "app_quick_layer.h"
#include "ui_click_feedback.h"
#include "usb_storage.h"
#include "touch_diag.h"
#include "touch_sampler.h"
#if CONFIG_TOUCH_DIAG && CONFIG_TOUCH_DIAG_BEEP
#include "board_buzzer.h"
#endif

#define TAG "app_loop"

#if CONFIG_TOUCH_DIAG
// 丢点诊断计数与峰值；单实例，文件级即可。/ Drop-diag counters; single instance.
static int64_t td_last_read_us;      // 上次读触摸时刻 / previous cst836u_read time
static int64_t td_stats_ms;          // 上次统计输出时刻 / previous stats line
static int64_t td_err_last_ms;       // READ_ERR 限流 / READ_ERR rate limit
static unsigned td_err_burst;        // 限流窗口内堆积次数 / bursts hidden by the limit
static unsigned td_read_err, td_kill_err, td_stall, td_present_over;
static unsigned td_replays, td_replay_frames;  // F4 回放事件与帧数 / replay events and frames
static int32_t td_max_stall_ms, td_max_present_ms;
#if CONFIG_TOUCH_DIAG_BEEP
static void td_beep_task(void* arg) {
    (void)arg;
    (void)board_buzzer_tone(2200, 60);
    vTaskDelete(NULL);
}
void touch_diag_beep(void) {
    if (xTaskCreate(td_beep_task, "tdbeep", 3072, NULL, 3, NULL) != pdPASS) return;
}
#else
void touch_diag_beep(void) { }
#endif
void touch_diag_block(const char* src, int64_t t0_us) {
    int32_t ms = (int32_t)((esp_timer_get_time() - t0_us) / 1000);
    if (ms > 50) TD_LOGI("BLOCK src=%s ms=%d", src, ms);
}
#endif  // CONFIG_TOUCH_DIAG

// 触摸芯片报点率远高于屏幕，主循环空转一轮只等这么久。
// Touch reports far faster than the panel; idle this long per loop.
#define LOOP_TICK_MS 5
// SD 卡插上或者刚挂好时，隔一会儿再试一次存在设置里的字体。
// Retry the saved SD font shortly after a card appears or mounts.
#define FONT_RETRY_INTERVAL_MS 750
#define MEDIA_POLL_INTERVAL_MS 500

static int64_t s_lock_ignore_until_ms;
extern const app_desc_t app_transfer;
extern const app_desc_t app_device_settings;
extern const app_desc_t app_book;

static const app_desc_t* s_presented_main;
// 锁屏占用整屏后旧主页面底栏不再是物理基准；解锁第一帧必须完整绘制。
// A lock screen replaces the old navigation baseline; the first unlocked frame must render complete navigation.
static void main_lock_boundary(void) {
    ui_click_feedback_reset();
    app_quick_layer_reset();
    s_presented_main = NULL;
    display_main_transition_cancel();
}

static void main_present_finished(app_ctx_t* ctx, const app_desc_t* app, app_redraw_t redraw) {
    if (redraw == APP_REDRAW_DONE || !app_navigation_main_page_ready(ctx, app)) s_presented_main = NULL;
    else if (redraw != APP_REDRAW_NONE) s_presented_main = app;
    display_main_transition_disarm();
}

void app_lock_ignore_for(int64_t ms) {
    s_lock_ignore_until_ms = esp_timer_get_time() / 1000 + ms;
}

#if CONFIG_TOUCH_DIAG
static void app_present_impl(app_ctx_t* ctx, const app_desc_t* app, app_redraw_t redraw) {
#else
void app_present(app_ctx_t* ctx, const app_desc_t* app, app_redraw_t redraw) {
#endif
    enum EpdDrawError result = EPD_DRAW_SUCCESS;
    if (redraw == APP_REDRAW_PAGE && ui_click_feedback_active()) {
        // 切页固定底栏之前先复位真实按压像素，避免按下底色留在新页。
        // Restore physical pressed pixels before freezing navigation, so a pressed tint never carries into the new page.
        EpdRect area;
        if (ui_click_feedback_release(ctx->fb, &area)) {
            display_main_transition_cancel();
            result = display_refresh_area(ctx->hl, DISPLAY_REFRESH_FEEDBACK, area, DISPLAY_REFRESH_DEFAULT);
            guard_draw_result(ctx->hl, result);
            if (result != EPD_DRAW_SUCCESS) redraw = APP_REDRAW_FULL;
        }
    }
    if(redraw==APP_REDRAW_FULL || (redraw==APP_REDRAW_PAGE && !app_navigation_main_page_ready(ctx,app)))
        ui_click_feedback_reset();
    if (redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_FULL) ui_click_feedback_begin(ctx->fb);
    ui_text_set_system_scale(true);
    // 后台目录/封面完成及滚动仍属于当前主页面，不能借普通整页出口闪到底栏。
    // Background directory/cover completion and scrolling remain on this main page and must not flash navigation via ordinary page output.
    if ((redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_AREA) &&
        s_presented_main == app && app_navigation_main_page_ready(ctx, app)) display_main_transition_arm(app, false, false);
    if (app->present != NULL && app->present(ctx, redraw)) {
        main_present_finished(ctx, app, redraw);
        return;
    }
    switch (redraw) {
        case APP_REDRAW_NONE:
        case APP_REDRAW_DONE:
            main_present_finished(ctx, app, redraw);
            return;
        case APP_REDRAW_AREA: {
            // 回调已经画好了 fb，这里只负责把它那一块推上屏。
            // The callback already painted fb; push that rectangle only.
            EpdRect area = app->area_hint != NULL
                ? display_rect_to_epd(app->area_hint(ctx))
                : ui_content_refresh_area();
            result = display_refresh_area(ctx->hl, DISPLAY_REFRESH_DYNAMIC, area, DISPLAY_REFRESH_DEFAULT);
            break;
        }
        case APP_REDRAW_FULL:
            if (app->render != NULL) app->render(ctx, ctx->fb);
            result = APP_PAGE_FORCE_FULL
                ? update_display_full(ctx->hl)
                : update_display_mode(ctx->hl, APP_PAGE_REFRESH_MODE);
            break;
        case APP_REDRAW_PAGE:
        default:
            if (app->render != NULL) app->render(ctx, ctx->fb);
            result = update_display_fast_page(ctx->hl);
            break;
    }
    guard_draw_result(ctx->hl, result);
    main_present_finished(ctx, app, redraw);
}

#if CONFIG_TOUCH_DIAG
void app_present(app_ctx_t* ctx, const app_desc_t* app, app_redraw_t redraw) {
    int64_t t0 = esp_timer_get_time();
    app_present_impl(ctx, app, redraw);
    int32_t ms = (int32_t)((esp_timer_get_time() - t0) / 1000);
    // 刷新阻塞窗口是“点击落空”的头号嫌疑，超过 100ms 的都留痕。
    // Refresh blocking is the prime suspect for lost taps; record every window over 100ms.
    if (ms > 100) {
        if (ms > td_max_present_ms) td_max_present_ms = ms;
        td_present_over++;
        TD_LOGI("PRESENT ms=%d redraw=%d", ms, (int)redraw);
    }
}
#endif
// 反馈只推控件附近，失败后重新绘制当前页面；不占用底栏切页许可。
// Push feedback only near its control, repainting the current page on failure without consuming navigation permission.
static void click_present(app_ctx_t *ctx, const app_desc_t *current, EpdRect area) {
    enum EpdDrawError error = update_display_click_feedback(ctx->hl,area);
    guard_draw_result(ctx->hl, error);
    if (error != EPD_DRAW_SUCCESS) app_present(ctx, current, APP_REDRAW_FULL);
}

static void present_page(app_ctx_t* ctx, const app_desc_t* app,
                         ui_gesture_t* gesture, app_redraw_t redraw) {
    if (redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_FULL)
        app_input_cancel_gesture(ctx, app, gesture);
    app_present(ctx, app, redraw);
}

typedef struct {
    unsigned updates;
    int64_t last_ms;
    EpdRect area;
} menu_feedback_t;

// 菜单反馈只推叶内单行。/ Menu feedback pushes one local row only.
static void menu_feedback(app_ctx_t* ctx, const app_desc_t* current,
                          int leaf, int hit, bool pressed, menu_feedback_t* feedback) {
    EpdRect area;
    int row = hit - leaf * UI_MENU_ITEMS_PER_PAGE;
    if (!ui_menu_row_rect(leaf, row, &area)) return;
    ui_draw_menu_row_pressed(ctx->fb, current, leaf, row, pressed);
    if (feedback->updates == 0) feedback->area = area;
    else {
        int bottom = feedback->area.y + feedback->area.height;
        int next_bottom = area.y + area.height;
        if (area.y < feedback->area.y) feedback->area.y = area.y;
        feedback->area.height = (bottom > next_bottom ? bottom : next_bottom) - feedback->area.y;
    }
    feedback->updates++;
    feedback->last_ms = ctx->now_ms;
    guard_draw_result(ctx->hl, display_refresh_area(ctx->hl, DISPLAY_REFRESH_DYNAMIC, area, DISPLAY_REFRESH_DEFAULT));
    if (feedback->updates >= UI_SETTLE_DU_MAX) {
        guard_draw_result(ctx->hl, display_refresh_area(ctx->hl, DISPLAY_REFRESH_LAYOUT, feedback->area, DISPLAY_REFRESH_DEFAULT));
        feedback->updates = 0;
    }
}



// 菜单页不是 app，单独画。/ The menu is not an app; present it here.
static void present_menu(app_ctx_t* ctx, const app_desc_t* current, int leaf, menu_feedback_t* feedback) {
    if (current->on_system_overlay) current->on_system_overlay(ctx);
    s_presented_main = NULL;
    feedback->updates = 0;
    if (display_take_white_exit()) {
        guard_draw_result(ctx->hl, update_display_white(ctx->hl));
    }
    ui_draw_menu_page(ctx->fb, current, leaf);
    guard_draw_result(ctx->hl, update_display_fast_page(ctx->hl));
}

typedef struct {
    int key;
    bool main_page;
    int64_t since_ms;
} panel_key_hold_t;

// 直读和回放共用三键路由，按下沿只执行一次；屏幕触摸仍由原管线处理。
// Live and replayed panel keys share one route per press edge; screen touches keep their existing pipeline.
static void panel_key_press(app_ctx_t* ctx, const app_desc_t* current, ui_gesture_t* gesture,
                            int key, panel_key_hold_t* hold, bool* menu_open, int* menu_leaf,
                            int* menu_pressed, menu_feedback_t* feedback) {
    hold->key = -1;
    ctx->consumed = true;
    app_input_cancel_gesture(ctx, current, gesture);
    if (*menu_pressed >= 0) menu_feedback(ctx, current, *menu_leaf, *menu_pressed, false, feedback);
    *menu_pressed = UI_MENU_HIT_NONE;
    if (current->owns_keys && !*menu_open) {
        bool main_keys = app_navigation_main_page_ready(ctx, current);
        app_redraw_t redraw = main_keys
            ? (key == UI_KEY_2 ? APP_REDRAW_NONE : app_navigation_main_key_action(ctx, current, key, false))
            : current->on_key && !(key == UI_KEY_2 && current->defer_middle_short)
                ? current->on_key(ctx, key) : APP_REDRAW_NONE;
        present_page(ctx, current, gesture, redraw);
        if (ctx->touch->count == 1 && ((main_keys && key == UI_KEY_2) || (!main_keys && current->on_key_long))) {
            *hold = (panel_key_hold_t){key, main_keys, ctx->now_ms};
        }
    } else if (key == UI_KEY_2) {
        if (*menu_open) {
            feedback->updates = 0;
            ui_draw_menu_page(ctx->fb, current, *menu_leaf);
            guard_draw_result(ctx->hl, update_display_full(ctx->hl));
        } else if (current->present != NULL && current->present(ctx, APP_REDRAW_FULL)) {
        } else if (current->render != NULL) {
            current->render(ctx, ctx->fb);
            guard_draw_result(ctx->hl, update_display_full(ctx->hl));
        }
    } else if (key == UI_KEY_3 && !usb_storage_active()) {
        *menu_open = !*menu_open;
        if (*menu_open) {
            *menu_leaf = ui_menu_leaf_for_app(current);
            present_menu(ctx, current, *menu_leaf, feedback);
        } else app_present(ctx, current, APP_REDRAW_PAGE);
    } else if (*menu_open) {
        int next = *menu_leaf + (key == UI_KEY_1 ? -1 : 1);
        if (next >= 0 && next < ui_menu_leaf_count()) {
            *menu_leaf = next;
            present_menu(ctx, current, *menu_leaf, feedback);
        }
    } else {
        app_redraw_t redraw = current->on_key ? current->on_key(ctx, key) : APP_REDRAW_NONE;
        if (redraw == APP_REDRAW_NONE)
            ESP_LOGI(TAG, "KEY%d pressed, no action bound", key + 1);
        present_page(ctx, current, gesture, redraw);
    }
}

// 回放按采样时间判断保持和抬手，排队时间不计入长按。
// Replay evaluates holds and releases in sample time, excluding queueing delay.
static void panel_key_hold(app_ctx_t* ctx, const app_desc_t* current, ui_gesture_t* gesture,
                           const app_touch_t* touch, bool menu_open, panel_key_hold_t* hold) {
    if (hold->key < 0) return;
    if (hold->main_page && !app_navigation_main_page_ready(ctx, current)) {
        hold->key = -1;
        return;
    }
    if (menu_open || ctx->request_app || ctx->request_menu || ctx->request_return || ctx->now_ms < hold->since_ms) {
        hold->key = -1;
        return;
    }
    if (ctx->released && hold->key == UI_KEY_2 && ui_key_hit_test(ctx->touch->x, ctx->touch->y) == UI_KEY_2 &&
        (current->defer_middle_short || app_navigation_main_page_ready(ctx, current))) {
        int key = hold->key;
        hold->key = -1;
        ctx->consumed = true;
        bool held = ctx->now_ms - hold->since_ms >= UI_LONG_PRESS_MS;
        app_redraw_t redraw = app_navigation_main_page_ready(ctx, current)
            ? app_navigation_main_key_action(ctx, current, key, held)
            : held ? current->on_key_long(ctx, key) : current->on_key(ctx, key);
        present_page(ctx, current, gesture, redraw);
    } else if (!touch->touched || touch->count != 1 || ui_key_hit_test(touch->x, touch->y) != hold->key) {
        hold->key = -1;
    } else if (!ctx->pressed && ctx->now_ms - hold->since_ms >= UI_LONG_PRESS_MS) {
        int key = hold->key;
        hold->key = -1;
        ctx->consumed = true;
        present_page(ctx, current, gesture, app_navigation_main_page_ready(ctx, current)
            ? app_navigation_main_key_action(ctx, current, key, true) : current->on_key_long(ctx, key));
    }
}

// 导航借用原菜单入口，反馈状态仍由主循环持有。
// Navigation borrows the original menu presenter while the loop retains feedback state.
static void navigation_present_menu(app_ctx_t *ctx, const app_desc_t *current,
                                    int leaf, void *menu_context) {
    present_menu(ctx, current, leaf, menu_context);
}

// 最近采样帧仍由主循环持有，回放逐帧同步，锁屏边界清零。
// The loop retains the latest sampled frame, synchronizes it during replay and clears it at lock boundaries.
static touch_frame_t s_last_frame;

// 连翻打断：积压里若已排着另一个按键的按下沿（反向翻页键或菜单键），返回它相对
// 队首的偏移；没有则返回 -1。回放据此丢弃它之前的同向点击，让最新按键立刻生效。
// 是否打断由页面自己判定（key_interrupts_backlog），非翻页页面保持严格回放顺序。
// Backlog interruption: offset of the first press edge on a different panel key (reverse
// turn or menu) still queued, or -1 when none. Replay drops the stale same-direction taps
// ahead of it so the newest key takes effect immediately. The page decides whether a key
// pair really interrupts; pages without the hook keep strict replay order.
static int backlog_other_key_press(app_ctx_t* ctx, const app_desc_t* app,
                                   int current_key, bool touched_before) {
    if (app->key_interrupts_backlog == NULL) return -1;
    touch_frame_t f;
    bool touched = touched_before;
    for (unsigned i = 0; touch_sampler_peek(i, &f); ++i) {
        const bool edge = f.touched && !touched;
        touched = f.touched != 0;
        if (!edge || f.count != 1) continue;
        const int key = ui_key_hit_test(f.x, f.y);
        if (key >= 0 && key != current_key && app->key_interrupts_backlog(ctx, current_key, key))
            return (int)i;
    }
    return -1;
}

// 电源动作先保存当前页面，再定稿一张不带时钟的静态画面；避免关机后留下会误读的冻结时间。
// Save the current page first, then settle a clock-free static frame so shutdown never leaves a misleading frozen time.
// 锁屏前先由页面保存进度，再持久化一次恢复上下文；不在翻页或点击时写入。
// Flush page progress before persisting one lock checkpoint; never write it on page turns or taps.
// 仅浅睡和定时档先浅睡；只有彻底关机档在锁屏出图后立即断电交接。
// Light-only and timed styles start with light sleep; only immediate-off hands off right after the lock art.
static bool lock_light_only(void) {
    return app_settings_shutdown_style() >= APP_SHUTDOWN_LIGHT;
}

static void prepare_lock(app_ctx_t *ctx, const app_desc_t *current, bool feedback) {
    const int64_t lock_prepare_start = esp_timer_get_time();
    // 浅睡期间定时器不再走完，锁屏前必须同步关断马达。
    // / Timers do not finish in light sleep, so cut the motor before the lock face.
    haptics_stop();
    if (feedback) app_lock_feedback(ctx->hl);
    app_lifecycle_checkpoint(ctx, current);
    // 锁屏前落盘（阅读进度/票根统计/恢复点）在画面之前，单独计时。
    // Pre-lock persistence (progress, ticket stats, resume point) runs before the lock art; time it separately.
    ESP_LOGI(TAG, "lock prepare feedback=%d total=%lldms", (int)feedback,
             (long long)((esp_timer_get_time() - lock_prepare_start) / 1000));
}
static void run_power_action(app_ctx_t *ctx, const app_desc_t *current, bool restart) {
    prepare_lock(ctx, current, false);
    ui_power_final_draw(ctx->fb, restart);
    guard_draw_result(ctx->hl, update_display_full(ctx->hl));
    app_lock_wait_key_idle(800);
    epd_poweroff();
    pico_boot_clear_resume();
    if (restart) app_restart_host();
    esp_err_t error = app_enter_host_sleep(APP_SLEEP_OFF);
    if (error != ESP_OK) ESP_LOGW(TAG, "power off failed: %s", esp_err_to_name(error));
    // 关机交接失败时回到可操作原页；不关闭书籍后再重新自动开书。/ A failed shutdown returns to the operable current page without closing/reopening the book.
    app_present(ctx, current, APP_REDRAW_PAGE);
}

// 锁屏期间的输入到不了主循环：密码层自己直读触摸，采样任务却照常把每次点击写入
// 队列，解锁后主循环会把积压帧整批回放到已解锁页面（实测密码四连点在主页乱跳）。
// 锁屏返回即作废全部积压帧，并把最近帧与边沿状态归零，复用路径才不会拿阻塞前的
// 旧按下帧制造假按下沿。锁屏被传书推迟的罕见路径会多丢一次反馈窗口内的点击，可接受。
// / Lock-context input never reaches the main loop: the PIN layer reads touch
// directly while the sampler task keeps queueing every tap, and after unlock the
// loop replays the backlog onto the unlocked page (measured: four PIN taps jump
// the home screen). Discard the whole backlog on lock return and zero the last
// frame and edge state so the reuse path cannot synthesize a stale press edge. A
// postponed lock loses the taps inside its feedback window, which is acceptable.
static void discard_lock_input(app_ctx_t* ctx, app_touch_t* latest, bool* was_touched) {
    touch_frame_t frame;
    while (touch_sampler_pop(&frame)) {}
    memset(&s_last_frame, 0, sizeof(s_last_frame));
    memset(latest, 0, sizeof(*latest));
    *was_touched = false;
    ctx->pressed = false;
    ctx->released = false;
    ctx->consumed = true;
}


// 上键单击的全局兜底。阅读正文内由 app_book 的 tick 按阅读映射处理（含翻页等动作），
// 这里不重复轮询；其余页面（主页、书架、子页）只兑现「前光开关」绑定，翻页等阅读
// 动作不上身。边沿规则与阅读侧对齐（app_book.c 的 READER_UP_KEY_*）：按下沿去抖，
// 松开时仍在短按窗口内才执行，按住不重复；绑定不是前光开关或板上无前光时不轮询
// GPIO，本机读取失败按抬起处理保持静默。退出阅读留在书架时正文已不可见，同样走这里。
// / Global up-key fallback. The reader body dispatches through app_book's tick
// (page turns etc.), so no double polling here; every other page (home, shelf,
// subpages) honours only the frontlight binding and never reader actions. Edge
// rules mirror the reader side (app_book.c READER_UP_KEY_*): debounced press,
// fire on release inside the short window, holds never repeat; the GPIO is only
// polled when the binding is the frontlight toggle and the board has one, and a
// failed read is treated as release. Leaving the reader for the shelf also lands
// here because the body is no longer visible.
#define UP_KEY_POLL_MS 30
#define UP_KEY_DEBOUNCE_MS 30
#define UP_KEY_LONG_MS 1000
static void up_key_global_tick(app_ctx_t* ctx, const app_desc_t* current) {
    static int64_t s_poll_ms;
    static bool s_down;
    static int64_t s_down_ms;
    if (current == &app_book && app_book_reader_body_visible()) {
        s_down = false;
        return;
    }
    if (app_settings_reader_up_action() != APP_READER_KEY_FRONTLIGHT || !frontlight_ready()) {
        s_down = false;
        return;
    }
    if (ctx->now_ms - s_poll_ms < UP_KEY_POLL_MS) return;
    s_poll_ms = ctx->now_ms;
    bool pressed = false;
    if (board_read_up_key(&pressed) != ESP_OK) {
        s_down = false;
        return;
    }
    if (pressed) {
        if (!s_down) { s_down = true; s_down_ms = ctx->now_ms; }
        return;
    }
    if (!s_down) return;
    s_down = false;
    int64_t held = ctx->now_ms - s_down_ms;
    if (held < UP_KEY_DEBOUNCE_MS || held >= UP_KEY_LONG_MS) return;
    // 与阅读侧同款：只驱动灯不重画；快捷层开层前会重读设置，预览不会复活旧状态。
    // / Same as the reader side: drive the light only, no repaint; the quick
    // layer reloads settings on open, so its preview never resurrects a stale state.
    const bool on = !app_settings_frontlight_on();
    app_settings_set_frontlight_on(on);
    (void)frontlight_apply(on, app_settings_frontlight_brightness(), app_settings_frontlight_cct());
    ctx->user_activity = true;
}

void app_loop_run(const app_loop_config_t* config) {
    s_presented_main = NULL;
    app_navigation_reset_contact();
    const app_desc_t* current = config->first_app != NULL
        ? config->first_app
        : app_home_page();

    cst836u_info_t touch_info = { 0 };
    cst836u_get_info(config->tp, &touch_info);
    app_touch_t latest = { 0 };

    app_ctx_t ctx = {
        .hl = config->hl,
        .fb = config->fb,
        .read_touch = config->tp ? s31_input_read_touch : NULL,
        .touch_context = config->tp,
        .touch = &latest,
        .acc = config->acc,
    };

    bool was_touched = false;
    app_quick_layer_init();
    bool menu_open = false;
    ui_gesture_t gesture = {0};
    ui_power_dialog_t power_dialog = {0};
    bool power_dialog_open = false;
    int menu_pressed = UI_MENU_HIT_NONE;
    menu_feedback_t feedback = {0};
    int menu_leaf = 0;
    app_navigation_history_t navigation = {0};
    panel_key_hold_t key_hold = {.key = -1};
    int64_t last_lock_poll_ms = 0;
    int64_t last_font_retry_ms = 0;
    int64_t last_media_poll_ms = 0;
    int64_t last_network_poll_ms = 0;
    int64_t last_input_ms = esp_timer_get_time() / 1000;
    uint32_t last_ble_input = ble_pt_input_serial();
    unsigned idle_minutes = app_settings_auto_lock_minutes();
    unsigned cover_policy = (unsigned)app_settings_main_refresh_mode() | ((unsigned)app_settings_system_contrast() << 8);
    bool media_mounted = app_media_initial_mounted();
    bool media_invalidated = false;
    app_lock_ignore_for(APP_LOCK_IGNORE_BOOT_MS);

    // The SD probe starts during board setup and is usually ready by the first product frame.
    // Retry here so the very first bookshelf paint already has a full CJK system face.
    app_media_retry_font(false);
    rp_boot_mark("sdfont");
    if (current->on_enter != NULL) current->on_enter(&ctx);
    rp_boot_mark("enter");
    app_media_poll(&ctx, current, &media_mounted, &media_invalidated);
    // 首帧先采样；暂时读不到时，后续状态变化会自动补上有效电量。
    // Sample before the first frame; a transient failure is repaired automatically when valid power data arrives.
    if (device_power_ready() && !current->holds_pmu) (void)device_power_refresh();
    // 首帧必须整屏 GC16：fb 里还是 app_main 画的开机图，DU 盖不掉。
    // First frame must be full GC16: fb still holds the splash; DU cannot cover it.
    app_present(&ctx, current, APP_REDRAW_FULL);
    rp_boot_mark("frame");
    pico_boot_ready();
    ESP_LOGI(TAG, "UI ready on %s", current->title);
    // 上次崩溃的现场（异常类型与触发地址）：有的话打一遍，串口和屏幕各有一份。
    // The previous panic scene, when there was one: logged here, and shown on the home screen.
    crash_note_log();

    // F4：主循环被刷屏等操作阻塞期间由采样任务持续读触摸，恢复后逐帧回放，边沿
    // 不丢；任务创建失败时 active() 为假，循环内自动回退直读（原行为）。
    // / F4: a dedicated task keeps sampling while the loop is blocked and frames
    // are replayed on wake; if the task cannot start, the loop reads directly.
    touch_sampler_start(config->tp);
    (void)device_power_key_sampler_start();

    // 上一轮是否切过页/换过屏；只有这种残留导航才丢弃，普通刷新阻塞期间的点击必须放行。
    // Whether the previous iteration changed screens; only such stale navigation is dropped.
    bool just_transitioned = false;
    while (true) {
        // 先清旧切页请求，再摄取新帧；新页绘制期间积压的按键不能被清掉。
        // Clear stale transition requests before ingesting new frames, not keys buffered while the new page drew.
        if (just_transitioned) {
            just_transitioned = false;
            ctx.request_menu = false;
            ctx.request_return = false;
        }
        const bool sample_power_key = device_power_ready() && !current->holds_pmu && !usb_storage_active();
        (void)device_power_key_sampler_enable(sample_power_key);
#if CONFIG_READER_PERF
        reader_perf_input_begin();
#endif
        unsigned requested_cover_policy = (unsigned)app_settings_main_refresh_mode() | ((unsigned)app_settings_system_contrast() << 8);
        if (requested_cover_policy != cover_policy) {
            app_home_cover_mode_changed(); app_book_cover_mode_changed();
            cover_policy = requested_cover_policy;
        }
        app_touch_t touch = { 0 };
        esp_err_t err = ESP_OK;
        int64_t frame_ms = esp_timer_get_time() / 1000;
        int64_t key_frame_ms = frame_ms;
        bool replayed = false;
        bool pressed = false, released = false;
        bool quick_lock_requested = false;
        bool quick_input_owned = false;
        ui_power_action_t replay_power_action = UI_POWER_ACTION_NONE;
        EpdRect replay_power_dirty = {0};
#if CONFIG_TOUCH_DIAG
        int64_t td_gap_ms = td_last_read_us ? (esp_timer_get_time() - td_last_read_us) / 1000 : 0;
        td_last_read_us = esp_timer_get_time();
#endif
        if (touch_sampler_active()) {
            touch_frame_t f;
            if (touch_sampler_pending() > 1) {
                // 从阻塞中恢复：一次性排空积压，逐帧走摄取+手势管线（回放）。
                // 回放期间 ctx.now_ms 取帧时间戳（队列内单调，不触发手势机倒退
                // 保护），排空后恢复真实时间；必须回放全部帧而非只回放边沿——
                // 手势 moved 判定依赖中间点轨迹（方案 §3.2）。出现页面切换请求
                // 即停止排空：剩余帧留待切页后下一轮，避免旧页多吃输入。
#if CONFIG_TOUCH_DIAG
                uint32_t n = 0;
                int64_t first_ms = 0, last_ms = 0;
#endif
                while (touch_sampler_pop(&f)) {
                    // 回放必须同步最近帧：复用路径的「边沿天然无变化」前提依赖
                    // s_last_frame 与 was_touched 同步；否则阻塞前残留的按下帧
                    // 会在回放排空后制造假按下沿（实测工具栏连点链，根因）。
                    // / Replay must keep the last-frame cache in sync: the reuse
                    // path assumes edges are unchanged, which breaks if the cache
                    // still holds a stale press frame (measured toolbar chain).
                    s_last_frame = f;
                    app_input_frame_to_touch(&f, &touch);
                    app_input_rotate_touch(&touch);
                    app_input_ingest_touch_frame(&ctx, &latest, &touch, ESP_OK, &was_touched,
                                       (int64_t)f.t_ms, &last_input_ms, &pressed, &released);
                    // 连翻打断：积压里已经排着另一个按键的按下沿（反向翻页键或菜单键）时，
                    // 丢弃它之前的同向点击，下一轮直接执行最新按键——连点上一页后点下一页
                    // 应立即停在当前页向下翻一页，而不是先把整队旧点击翻完。
                    // Backlog interruption: when a press edge on another panel key (reverse
                    // turn or menu) is already queued, drop the stale same-direction taps
                    // ahead of it so the newest key acts at once instead of after the queue.
                    if (pressed) {
                        const int key = ui_key_hit_test(touch.x, touch.y);
                        if (key >= 0) {
                            const int skip = backlog_other_key_press(&ctx, current, key, touch.touched);
                            if (skip >= 0) {
                                touch_sampler_discard((unsigned)skip);
                                s_last_frame.touched = 0;
                                was_touched = false;
                                key_hold.key = -1;
                                continue;
                            }
                        }
                    }
                    // 连翻/刷屏阻塞期间的点击全部走回放：按下沿同样给一记短震，
                    // 否则连续翻页从头到尾没有触觉反馈。s_pulsing 守卫把密集边沿
                    // 收敛为不重叠的单记脉冲。
                    // / Taps landing inside a blocked refresh all arrive via replay: fire the
                    // short pulse on press edges here too, or continuous page turns never
                    // vibrate at all. The s_pulsing guard collapses dense edges into
                    // non-overlapping single pulses.
                    if (pressed && touch.count == 1 && !usb_storage_active()) haptics_pulse();
                    // 快捷层与电源弹窗都按帧回放：只在排空后喂最后一帧会丢掉按下沿，
                    // 快速点按需要再点一次才生效。弹窗动作留到本 tick 的弹窗分支执行。
                    // Replay the quick layer and the power dialog frame by frame: feeding
                    // only the last frame drops press edges, so a fast tap needs a second
                    // click. A dialog action runs in this tick's dialog branch.
                    if (power_dialog_open) {
                        ctx.consumed = false;
                        EpdRect dirty;
                        ui_power_action_t action = ui_power_dialog_feed(&power_dialog, &ctx, &dirty);
                        if (action == UI_POWER_ACTION_NONE && dirty.height > 0) {
                            // 回放只攒 dirty 不推屏：一次扫描 55–90ms 而触摸帧 5ms 一帧，逐帧推
                            // 屏永远追不上手指，滑块越拖越落后。排空后统一推一次合并区域。
                            // Replay only accumulates the dirty rect: one scan costs 55-90 ms while
                            // touch frames arrive every 5 ms, so per-frame pushes never catch the
                            // finger and the slider falls further behind. One merged push after the drain.
                            replay_power_dirty = replay_power_dirty.height > 0
                                ? ui_rect_union(replay_power_dirty, dirty) : dirty;
                        } else if (action != UI_POWER_ACTION_NONE) {
                            replay_power_action = action;
                        }
                        ctx.consumed = true;
                    } else if (!menu_open) {
                        quick_input_owned = app_quick_layer_feed_frame(&ctx, current, &quick_lock_requested, true);
                        if (quick_input_owned) {
                            key_hold.key = -1;
                            app_input_cancel_gesture(&ctx, current, &gesture);
                        }
                    }
                    if (!power_dialog_open && !quick_input_owned) {
                        const int key = ui_key_hit_test(touch.x, touch.y);
                        if (pressed && key >= 0)
                            panel_key_press(&ctx, current, &gesture, key, &key_hold,
                                            &menu_open, &menu_leaf, &menu_pressed, &feedback);
                        else if (pressed) key_hold.key = -1;
                        panel_key_hold(&ctx, current, &gesture, &touch, menu_open, &key_hold);
                    }
                    app_input_dispatch_gesture_frame(&ctx, current, &gesture, ESP_OK,
                                           power_dialog_open || quick_input_owned || menu_open, present_page);
                    was_touched = touch.touched;
#if CONFIG_TOUCH_DIAG
                    n++;
                    if (n == 1) first_ms = (int64_t)f.t_ms;
                    last_ms = (int64_t)f.t_ms;
#endif
                    if (ctx.request_app || ctx.request_menu || ctx.request_return) break;
                }
                // 回放期间只写硬件；这里按最终值刷新一次前光行，页面切换请求则丢弃这一行。
                // Replay only drove hardware; paint the final frontlight row once, or drop it when a page switch is queued.
                if (!ctx.request_app && !ctx.request_menu && !ctx.request_return) {
                    app_quick_layer_flush_frontlight(&ctx, current);
                } else {
                    app_quick_layer_discard_frontlight();
                }
                // 排空后统一推一次电源弹窗滑块；动作仍延迟到本 tick 的弹窗分支执行。
                // Push the power-dialog slider once after the drain; actions still defer to this tick's dialog branch.
                if (power_dialog_open && replay_power_action == UI_POWER_ACTION_NONE &&
                    replay_power_dirty.height > 0) {
                    ui_power_dialog_draw_controls(ctx.fb, &power_dialog, replay_power_dirty);
                    enum EpdDrawError result = display_refresh_area(ctx.hl, DISPLAY_REFRESH_FEEDBACK, replay_power_dirty, DISPLAY_REFRESH_DEFAULT);
                    guard_draw_result(ctx.hl, result);
                    if (result != EPD_DRAW_SUCCESS) {
                        power_dialog_open = false;
                        ui_power_dialog_end();
                        ui_power_dialog_reset(&power_dialog, touch.touched);
                        app_present(&ctx, current, APP_REDRAW_FULL);
                    }
                }
                replayed = true;
                frame_ms = esp_timer_get_time() / 1000;
#if CONFIG_TOUCH_DIAG
                td_replays++;
                td_replay_frames += n;
                TD_LOGI("REPLAY n=%u ms=%d", (unsigned)n, (int)(last_ms - first_ms));
#endif
            } else if (touch_sampler_pop(&f)) {
                s_last_frame = f;
                key_frame_ms = f.t_ms;
                app_input_frame_to_touch(&s_last_frame, &touch);
            } else {
                // 无新帧：复用最近帧（采样节拍 5ms 与循环节拍不完全同相），边沿天然无变化。
                // / No new frame: reuse the last one; edges are unchanged by construction.
                app_input_frame_to_touch(&s_last_frame, &touch);
            }
        } else {
            err = s31_input_read_touch(config->tp, &touch);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "CST836U read failed: %s", esp_err_to_name(err));
#if CONFIG_TOUCH_DIAG
                td_read_err++;
                // 手指按着时出错：本轮点击注定报废（后续边沿丢失）。单独计数。
                // Error while the finger is down: this tap is doomed (edge lost). Counted alone.
                if (was_touched) td_kill_err++;
                {
                    int64_t now_ms = esp_timer_get_time() / 1000;
                    if (now_ms - td_err_last_ms > 500) {
                        TD_LOGI("READ_ERR err=%s finger=%d kill=%u burst=%u",
                                esp_err_to_name(err), (int)was_touched, td_kill_err, td_err_burst);
                        td_err_last_ms = now_ms;
                        td_err_burst = 0;
                    } else td_err_burst++;
                }
#endif
                memset(&touch, 0, sizeof(touch));
            }
            frame_ms = esp_timer_get_time() / 1000;
            key_frame_ms = frame_ms;
        }
#if CONFIG_TOUCH_DIAG
        // STALL 仅正常迭代累计；回放迭代的阻塞窗口由 REPLAY 行承担证据链（方案 §3.3）。
        // 相邻两次处理间隔超过 50ms：这轮里循环被别的活儿阻塞（刷屏、字体、PMU…）。
        if (!replayed && td_gap_ms > 50) {
            td_stall++;
            if (td_gap_ms > td_max_stall_ms) td_max_stall_ms = (int32_t)td_gap_ms;
            TD_LOGI("STALL gap=%dms", (int)td_gap_ms);
        }
#endif
        if (!replayed) {
            app_input_rotate_touch(&touch);
            app_input_ingest_touch_frame(&ctx, &latest, &touch, err, &was_touched,
                               frame_ms, &last_input_ms, &pressed, &released);
            // 单指按下沿给一记短震；回放帧不重复，多点、读错与读卡模式不震。
            // / One short pulse per single-finger press edge; replayed frames never repeat and multitouch, read errors and USB storage stay silent.
            if (err == ESP_OK && pressed && touch.count == 1 && !usb_storage_active()) haptics_pulse();
        }
        // 触摸已由 app_input_ingest_touch_frame 去抖并更新 ctx；这里只复位上游新增的导航按压状态。
        // Touch is debounced in app_input_ingest_touch_frame; only the upstream nav-press state resets here.
        app_navigation_observe_contact(err, touch.touched);
        if (ctx.now_ms - last_network_poll_ms >= 500) {
            last_network_poll_ms = ctx.now_ms;
            TD_TIME("transfer_poll", wifi_transfer_service_poll());
            pico_ota_auto_poll(!app_weather_busy() && !touch.touched && ctx.now_ms - last_input_ms >= 10000 &&
                !current->holds_pmu && !usb_storage_active() && !ble_pt_connected() &&
                current != &app_transfer && !power_dialog_open && !menu_open &&
                (current != &app_device_settings || app_navigation_main_page_ready(&ctx, current)) &&
                !app_quick_layer_is_open() && !ctx.request_app && !ctx.request_return && !ctx.request_menu);
            // 后台对时是系统服务（同 app_media_poll / 字体重试），守卫在 time_service 内部；
            // 空闲门控沿用上游：触压、近期输入、独占 PMU 的页面、USB 与联网升级期间不动。
            // Background sync is a system service like app_media_poll or the font retry, with its
            // guards inside time_service; idle gating stays upstream: no touch, no recent input,
            // pages holding the PMU, USB or an online update.
            app_time_service_poll(!app_weather_busy() && !touch.touched && ctx.now_ms - last_input_ms >= 10000 &&
                !current->holds_pmu && !usb_storage_active() && !pico_online_busy() && !ble_pt_connected());
        }
        bool selected_from_menu = false;
        const bool power_input_owned = power_dialog_open;

        if (ctx.now_ms - last_media_poll_ms >= MEDIA_POLL_INTERVAL_MS) {
            last_media_poll_ms = ctx.now_ms;
            bool media_changed;
            TD_TIME("media_poll", media_changed = app_media_poll(&ctx, current, &media_mounted, &media_invalidated));
            if (media_changed) {
                power_dialog_open = false;
                ui_power_dialog_end();
                ui_power_dialog_reset(&power_dialog, touch.touched);
                app_quick_layer_reset();
                key_hold.key = -1;
                app_input_cancel_gesture(&ctx, current, &gesture);
                menu_pressed = UI_MENU_HIT_NONE;
                ctx.consumed = true;
                ctx.pressed = ctx.released = false;
                was_touched = touch.touched;
                if (menu_open) present_menu(&ctx, current, menu_leaf, &feedback);
                else app_present(&ctx, current, APP_REDRAW_FULL);
                // 本轮旧输入不再触发动作，下一轮恢复正常事件处理。
                // Discard this tick's stale input, then resume normal dispatch next tick.
                just_transitioned = true;
                vTaskDelay(pdMS_TO_TICKS(LOOP_TICK_MS));
                continue;
            }
        }

        if (!power_input_owned && !menu_open) {
            if (err != ESP_OK) {
                quick_input_owned = app_quick_layer_cancel_input();
            }
            else {
                quick_input_owned = app_quick_layer_feed_frame(&ctx, current, &quick_lock_requested, false);
                if (quick_input_owned) {
                    key_hold.key = -1;
                    app_input_cancel_gesture(&ctx, current, &gesture);
                }
            }
            app_quick_layer_present_buttons(&ctx,current);
        }

        if (power_input_owned && err == ESP_OK) {
            ctx.consumed = false;
            EpdRect dirty;
            ui_power_action_t action = ui_power_dialog_feed(&power_dialog, &ctx, &dirty);
            if (action == UI_POWER_ACTION_NONE) action = replay_power_action;
            if (action == UI_POWER_ACTION_NONE && dirty.height > 0) {
                ui_power_dialog_draw_controls(ctx.fb, &power_dialog, dirty);
                enum EpdDrawError result = display_refresh_area(ctx.hl, DISPLAY_REFRESH_FEEDBACK, dirty, DISPLAY_REFRESH_DEFAULT);
                guard_draw_result(ctx.hl, result);
                if (result != EPD_DRAW_SUCCESS) {
                    power_dialog_open = false;
                    ui_power_dialog_end();
                    ui_power_dialog_reset(&power_dialog, touch.touched);
                    app_present(&ctx, current, APP_REDRAW_FULL);
                }
            } else {
                if (action == UI_POWER_ACTION_CANCEL) {
                    power_dialog_open = false;
                    ui_power_dialog_end();
                    ui_power_dialog_reset(&power_dialog, false);
                    if (menu_open) present_menu(&ctx, current, menu_leaf, &feedback);
                    else app_present(&ctx, current, APP_REDRAW_PAGE);
                } else if (action == UI_POWER_ACTION_SHUTDOWN) {
                    power_dialog_open = false;
                    ui_power_dialog_end();
                    ui_power_dialog_reset(&power_dialog, false);
                    // 「关机」固定立即断电；「彻底关机」待机只决定锁屏是否立即断电，与本按钮无关。
                    // 关机 always powers off immediately; the "full off" standby style only decides whether locking powers off at once.
                    run_power_action(&ctx, current, false);
                } else if (action == UI_POWER_ACTION_RESTART) {
                    power_dialog_open = false;
                    ui_power_dialog_end();
                    ui_power_dialog_reset(&power_dialog, false);
                    run_power_action(&ctx, current, true);
                }
            }
            ctx.consumed = true;
        }

        if (err != ESP_OK) {
            key_hold.key = -1;
#if CONFIG_TOUCH_DIAG
            // I2C 出错强杀手势跟踪：单指场景下等价于这次点击报废。
            // I2C error force-kills tracking: with one finger this forfeits the tap.
            if (gesture.active) {
                TD_LOGI("KILL_BY_ERR finger=%d", (int)was_touched);
                touch_diag_beep();
            }
#endif
            app_input_cancel_gesture(&ctx, current, &gesture);
            EpdRect dirty;
            ui_power_dialog_cancel_input(&power_dialog, &dirty);
            if (power_dialog_open && dirty.height > 0) {
                ui_power_dialog_draw_controls(ctx.fb, &power_dialog, dirty);
                guard_draw_result(ctx.hl, display_refresh_area(ctx.hl, DISPLAY_REFRESH_FEEDBACK, dirty, DISPLAY_REFRESH_DEFAULT));
            }
            ctx.consumed = true;
            if (menu_pressed >= 0) menu_feedback(&ctx, current, menu_leaf, menu_pressed, false, &feedback);
            menu_pressed = UI_MENU_HIT_NONE;
        }
        EpdRect click_area;
        if (ui_click_feedback_active() && (err != ESP_OK || touch.count > 1 ||
            quick_input_owned || power_input_owned || menu_open ||
            released || touch.count != 1 ||
            ui_click_feedback_cancel_at(touch.x, touch.y))) {
            if (ui_click_feedback_release(ctx.fb, &click_area)) click_present(&ctx, current, click_area);
        }
        // 主页面底栏在首次单指报点就确认；抬手、轻滑和停留不再重复切页。
        // Commit root navigation on the first single contact; lift, small motion and holds never repeat it.
        bool nav_press_owned=app_navigation_contact_polled()&&touch.touched&&touch.count==1&&app_navigation_main_page_ready(&ctx,current)&&ui_nav_hit(touch.x,touch.y)>=0;
        if(nav_press_owned)ctx.consumed=true;
        if (err == ESP_OK && pressed && !nav_press_owned && touch.count == 1 && !power_input_owned &&
            !quick_input_owned && !menu_open && !usb_storage_active() &&
            app_navigation_main_page_ready(&ctx, current)) {
            const int tab = ui_nav_hit(touch.x, touch.y);
            if (tab >= 0) {
                key_hold.key = -1;
                app_input_cancel_gesture(&ctx, current, &gesture);
                ctx.consumed = true;
                ctx.user_activity = true;
                nav_press_owned = true;
                app_navigation_claim_contact();
                if(tab!=app_index_of(current)||ctx.request_app)ui_nav_request(&ctx,tab);
            }
        }
        if (err == ESP_OK && pressed && touch.count == 1 && !nav_press_owned && !power_input_owned &&
            !quick_input_owned && !menu_open && !usb_storage_active() &&
            ui_click_feedback_press(ctx.fb, touch.x, touch.y, &click_area))
            click_present(&ctx, current, click_area);
        if (!power_input_owned && !quick_input_owned && !nav_press_owned && pressed && err == ESP_OK &&
            (!replayed || ui_key_hit_test(touch.x, touch.y) < 0)) {
            key_hold.key = -1;
            const int key = ui_key_hit_test(touch.x, touch.y);
            const bool handle_hit = ui_menu_handle_hit_test(touch.x, touch.y) &&
                (!current->menu_handle_enabled || current->menu_handle_enabled(&ctx));
            ctx.consumed = true;
            if (key < 0 && handle_hit) {
                app_input_cancel_gesture(&ctx, current, &gesture);
                if (menu_pressed >= 0) menu_feedback(&ctx, current, menu_leaf, menu_pressed, false, &feedback);
                menu_pressed = UI_MENU_HIT_NONE;
            }
            if (key >= 0) {
                const int64_t now_ms = ctx.now_ms;
                ctx.now_ms = key_frame_ms;
                panel_key_press(&ctx, current, &gesture, key, &key_hold,
                                &menu_open, &menu_leaf, &menu_pressed, &feedback);
                ctx.now_ms = now_ms;
            } else if (handle_hit && !usb_storage_active()) {
                menu_open = !menu_open;
                if (menu_open) {
                    menu_leaf = ui_menu_leaf_for_app(current);
                    present_menu(&ctx, current, menu_leaf, &feedback);
                } else app_present(&ctx, current, APP_REDRAW_PAGE);
            } else if (menu_open) {
                int hit = ui_menu_hit_test(touch.x, touch.y, menu_leaf);
                if (hit == UI_MENU_HIT_PREV || hit == UI_MENU_HIT_NEXT) {
                    menu_leaf += hit == UI_MENU_HIT_NEXT ? 1 : -1;
                    present_menu(&ctx, current, menu_leaf, &feedback);
                } else if (hit >= 0 && touch.count == 1) {
                    menu_pressed = hit;
                    menu_feedback(&ctx, current, menu_leaf, hit, true, &feedback);
                }
            } else if (current->on_gesture) {
                ctx.consumed = false;
            } else {
                // 旧页按下先收到 consumed=true，仅 NONE 为同轮 tick 放行。
                // Legacy press sees consumed=true; only NONE clears it for this tick.
                app_redraw_t redraw = current->on_touch
                    ? current->on_touch(&ctx, &latest) : APP_REDRAW_NONE;
                app_present(&ctx, current, redraw);
                if (redraw == APP_REDRAW_NONE) ctx.consumed = false;
            }
        }
        if (err == ESP_OK) was_touched = touch.touched;
        if (!power_input_owned && !quick_input_owned && menu_open && menu_pressed >= 0) {
            int hit = ui_menu_hit_test(latest.x, latest.y, menu_leaf);
            if (touch.count > 1 || hit != menu_pressed || released) {
                int chosen = menu_pressed;
                menu_feedback(&ctx, current, menu_leaf, chosen, false, &feedback);
                menu_pressed = UI_MENU_HIT_NONE;
                if (released && err == ESP_OK && hit == chosen) {
                    menu_open = false;
                    ctx.request_app = app_at(chosen);
                    selected_from_menu = true;
                    if (ctx.request_app == current) app_present(&ctx, current, APP_REDRAW_PAGE);
                }
            }
        }
        app_input_dispatch_gesture_frame(&ctx, current, &gesture, err,
                               power_input_owned || quick_input_owned || menu_open, present_page);

        ctx.now_ms = esp_timer_get_time() / 1000;

        // 电源键短按默认锁屏；阅读正文可选择将它用于下一页。
        // A short power press normally locks; the reader body may use it for the next page.
        if (quick_lock_requested || (device_power_ready() && !current->holds_pmu && !usb_storage_active()
            && ctx.now_ms - last_lock_poll_ms >= APP_LOCK_POLL_MS)) {
            last_lock_poll_ms = ctx.now_ms;
            device_power_key_action_t key_action;
            if (quick_lock_requested) {
                key_action = DEVICE_POWER_KEY_SHORT;
            } else {
                TD_TIME("pmu_poll", key_action = device_power_take_key_action());
            }
            if (key_action != DEVICE_POWER_KEY_NONE) {
                last_input_ms = ctx.now_ms;
                bool quick_was_open = app_quick_layer_is_open();
                if (quick_was_open && !quick_lock_requested) {
                    app_quick_layer_reset();
                    app_quick_layer_present(&ctx, current, false);
                }
                if (!quick_lock_requested && ctx.now_ms < s_lock_ignore_until_ms) {
                    ESP_LOGI(TAG, "ignore boot power-key action %d", (int)key_action);
                } else if (key_action == DEVICE_POWER_KEY_LONG) {
                    key_hold.key = -1;
                    app_input_cancel_gesture(&ctx, current, &gesture);
                    if (menu_pressed >= 0) menu_feedback(&ctx, current, menu_leaf, menu_pressed, false, &feedback);
                    menu_pressed = UI_MENU_HIT_NONE;
                    extern const app_desc_t app_book;
                    bool reader_power_lock = !menu_open && current == &app_book &&
                        app_book_reader_body_visible() && app_settings_reader_power_turn();
                    if (reader_power_lock) {
                        prepare_lock(&ctx, current, true);
                        main_lock_boundary();
                        enter_lock_and_sleep(ctx.hl, &s_lock_ignore_until_ms, config->acc, config->tp, true, lock_light_only());
                        discard_lock_input(&ctx, &latest, &was_touched);
                        display_request_full_refresh();
                        ctx.now_ms = esp_timer_get_time() / 1000;
                        last_input_ms = ctx.now_ms;
                        app_media_poll(&ctx, current, &media_mounted, &media_invalidated);
                        last_media_poll_ms = ctx.now_ms;
                        ctx.consumed = true;
                        ctx.pressed = false;
                        ctx.released = false;
                        app_present(&ctx, current, APP_REDRAW_PAGE);
                    } else {
                        const bool underlay_changed = current->on_system_overlay && current->on_system_overlay(&ctx);
                        power_dialog_open = true;
                        ui_power_dialog_reset(&power_dialog, touch.touched);
                        ctx.request_app = NULL;
                        ctx.request_return = ctx.request_menu = false;
                        main_lock_boundary();
                        ui_text_set_system_scale(true);
                        ui_power_dialog_draw(ctx.fb, &power_dialog);
                        const EpdRect dialog_area = underlay_changed
                            ? (EpdRect){0, 0, epd_rotated_display_width(), epd_rotated_display_height()}
                            : ui_power_dialog_rect();
                        enum EpdDrawError result = display_refresh_area(ctx.hl, DISPLAY_REFRESH_LAYOUT, dialog_area, DISPLAY_REFRESH_DEFAULT);
                        guard_draw_result(ctx.hl, result);
                        if (result != EPD_DRAW_SUCCESS) {
                            power_dialog_open = false;
                            ui_power_dialog_end();
                            ui_power_dialog_reset(&power_dialog, touch.touched);
                            app_present(&ctx, current, APP_REDRAW_FULL);
                        }
                    }
                } else if (!power_dialog_open) {
                    key_hold.key = -1;
                    app_input_cancel_gesture(&ctx, current, &gesture);
                    if (menu_pressed >= 0) menu_feedback(&ctx, current, menu_leaf, menu_pressed, false, &feedback);
                    menu_pressed = UI_MENU_HIT_NONE;
                    app_redraw_t power_redraw = !quick_lock_requested && !quick_was_open && !menu_open && current->on_power_short
                        ? current->on_power_short(&ctx) : APP_REDRAW_NONE;
                    if (power_redraw != APP_REDRAW_NONE) {
                        present_page(&ctx, current, &gesture, power_redraw);
                        ctx.consumed = true;
                    } else {
                        prepare_lock(&ctx, current, true);
                        extern const app_desc_t app_book;
                        main_lock_boundary();
                        enter_lock_and_sleep(ctx.hl, &s_lock_ignore_until_ms, config->acc, config->tp,
                                             !menu_open && current == &app_book && app_book_reader_body_visible(), lock_light_only());
                        discard_lock_input(&ctx, &latest, &was_touched);
                        display_request_full_refresh();
                        ctx.now_ms = esp_timer_get_time() / 1000;
                        last_input_ms = ctx.now_ms;
                        app_media_poll(&ctx, current, &media_mounted, &media_invalidated);
                        last_media_poll_ms = ctx.now_ms;
                        ctx.consumed = true;
                        ctx.pressed = false;
                        ctx.released = false;
                        // 醒来还在同一页，重画一次免得留着锁屏图。
                        // Still the same page; redraw so the lock image does not stay.
                        if (menu_open) present_menu(&ctx, current, menu_leaf, &feedback);
                        else app_present(&ctx, current, APP_REDRAW_PAGE);
                    }
                }
            }
        }

        // 设置里存的字体在 SD 卡上，开机时卡可能还没挂好，这里定期重试。
        // The saved font lives on the card; retry until the mount is ready.
        // The saved font lives on the card; retry until the mount is ready.
        if (!power_dialog_open && !app_quick_layer_is_open() && !usb_storage_active() && current != &app_transfer &&
            ctx.now_ms - last_font_retry_ms >= FONT_RETRY_INTERVAL_MS) {
            last_font_retry_ms = ctx.now_ms;
            bool font_ready;
            TD_TIME("font_retry", font_ready = app_media_retry_font(!media_invalidated));
            if (font_ready) {
                key_hold.key = -1;
                app_input_cancel_gesture(&ctx, current, &gesture);
                ctx.consumed = true;
                menu_pressed = UI_MENU_HIT_NONE;
                if (menu_open) {
                    present_menu(&ctx, current, menu_leaf, &feedback);
                } else {
                    app_present(&ctx, current, APP_REDRAW_FULL);
                }
                ESP_LOGI(TAG, "TTF font ready");
            }
        }

        if (!power_dialog_open && !replayed) {
            // 单帧也用采样时间，防止与下一轮回放的时间基准不一致。
            // Single frames also use sample time so the next replay shares the same clock.
            const int64_t now_ms = ctx.now_ms;
            ctx.now_ms = key_frame_ms;
            panel_key_hold(&ctx, current, &gesture, &touch, menu_open, &key_hold);
            ctx.now_ms = now_ms;
        }
        // 蓝牙翻页器：BLE 栈只在这里跟随设置启停。必须走 ble_pt_sync()——自己写
        // 「设置与状态不一致就 start()」会让启动失败的那次判断每轮都成立，变成每秒
        // 几十次重复初始化并把内存耗光。sync() 内部有内存闸门、失败退避和崩溃自锁。
        // Bluetooth page-turner: the BLE stack follows the setting only here, and only through
        // ble_pt_sync(). Rolling our own "state differs from setting, so start()" makes a failed
        // start true on every tick, re-initialising dozens of times a second until memory runs
        // out; sync() carries the memory gate, the failure back-off and the crash-loop lock.
        // WiFi 在 C6、BLE 在 S3；传输仍与 BLE 竞争 S3 内部内存，网络占用先拆 BLE，结束后恢复。
        // WiFi runs on C6 and BLE on S3; both consume S3 internal memory, so network leases pause BLE and release it afterwards.
        wifi_transfer_status_t transfer;
        wifi_transfer_get_status(&transfer);
        const bool wifi_busy = transfer.state != WIFI_TRANSFER_STOPPED ||
            pico_online_busy();
        ble_pt_sync(app_settings_ble_turner() && !wifi_busy);
        TD_TIME("ble_poll", ble_pt_poll());
        if (app_quick_layer_is_open() || quick_input_owned) {
            // 覆盖层期间丢弃页内蓝牙输入，关闭后不重放翻页或学习事件。
            // Discard page BLE input while covered, without replaying turns or learning edges after closing.
            ble_pt_event_t key_event; ble_pt_raw_t raw_event;
            while (ble_pt_pop_key(&key_event)) {}
            while (ble_pt_pop_raw(&raw_event)) {}
        }
        app_quick_layer_sync_status(&ctx, current);
        // 上键全局兜底与 on_tick 同门控：弹窗、快捷层、菜单持有输入时不响应。
        // / Global up-key fallback gated like on_tick: silent while the dialog,
        // quick layer or menu owns input.
        if (!power_dialog_open && !app_quick_layer_is_open() && !quick_input_owned && !menu_open &&
            !ctx.request_app && !ctx.request_menu && !ctx.request_return)
            up_key_global_tick(&ctx, current);
        if (!power_dialog_open && !app_quick_layer_is_open() && !quick_input_owned && !ui_click_feedback_active() && !menu_open && !ctx.request_app && !ctx.request_menu && !ctx.request_return && current->on_tick != NULL) {
            app_redraw_t redraw = current->on_tick(&ctx);
            if (redraw == APP_REDRAW_PAGE || redraw == APP_REDRAW_FULL) key_hold.key = -1;
            present_page(&ctx, current, &gesture, redraw);
        }
        // 主页面和快捷层共享白底状态栏；只在电量变化且没有输入覆盖时局部推屏。
        // Main roots and the quick layer share a white status strip; present power changes locally when input is uncovered.
        if (!power_dialog_open && !menu_open && (!quick_input_owned || app_quick_layer_is_open()) && !touch.touched &&
            !ui_click_feedback_active() && !ctx.request_app && !ctx.request_menu && !ctx.request_return &&
            (app_quick_layer_is_open() || app_navigation_main_page_ready(&ctx, current)) &&
            ui_nav_status_power_changed()) {
            EpdRect area = {0, 0, epd_rotated_display_width(), 76};
            if (app_quick_layer_is_open()) ui_quick_menu_draw_status(ctx.fb);
            else {
                ui_fill_rect_fast(area, 0xff, ctx.fb);
                ui_nav_status(ctx.fb);
            }
            enum EpdDrawError error = display_refresh_area(ctx.hl, DISPLAY_REFRESH_DETAIL, area, DISPLAY_REFRESH_DEFAULT);
            guard_draw_result(ctx.hl, error);
            if (error != EPD_DRAW_SUCCESS) {
                app_quick_layer_reset();
                app_present(&ctx, current, APP_REDRAW_FULL);
            }
        }
        if (ctx.user_activity) { last_input_ms = ctx.now_ms; ctx.user_activity = false; }
        uint32_t ble_input = ble_pt_input_serial();
        if (ble_input != last_ble_input) { last_ble_input = ble_input; last_input_ms = ctx.now_ms; }
        unsigned requested_idle = app_settings_auto_lock_minutes();
        bool idle_blocked = usb_storage_active() || wifi_busy || current == &app_transfer ||
            current->holds_pmu || power_dialog_open || touch.touched ||
            ctx.request_app || ctx.request_menu || ctx.request_return;
        if (idle_blocked || requested_idle != idle_minutes) last_input_ms = ctx.now_ms;
        idle_minutes = requested_idle;
        if (!idle_blocked && ctx.now_ms >= s_lock_ignore_until_ms &&
            auto_lock_due(ctx.now_ms, last_input_ms, idle_minutes)) {
            key_hold.key = -1;
            app_input_cancel_gesture(&ctx, current, &gesture);
            if (menu_pressed >= 0) menu_feedback(&ctx, current, menu_leaf, menu_pressed, false, &feedback);
            menu_pressed = UI_MENU_HIT_NONE;
            prepare_lock(&ctx, current, false);
            extern const app_desc_t app_book;
            main_lock_boundary();
            enter_lock_and_sleep(ctx.hl, &s_lock_ignore_until_ms, config->acc, config->tp,
                                 !menu_open && current == &app_book && app_book_reader_body_visible(), lock_light_only());
            discard_lock_input(&ctx, &latest, &was_touched);
            display_request_full_refresh();
            ctx.now_ms = esp_timer_get_time() / 1000;
            last_input_ms = ctx.now_ms;
            app_media_poll(&ctx, current, &media_mounted, &media_invalidated);
            last_media_poll_ms = ctx.now_ms;
            ctx.pressed = ctx.released = false;
            just_transitioned = true;
            if (menu_open) present_menu(&ctx, current, menu_leaf, &feedback);
            else app_present(&ctx, current, APP_REDRAW_PAGE);
        }
        if (menu_open && feedback.updates > 0
            && ctx.now_ms - feedback.last_ms >= UI_SETTLE_IDLE_MS) {
            guard_draw_result(ctx.hl, display_refresh_area(ctx.hl, DISPLAY_REFRESH_LAYOUT, feedback.area, DISPLAY_REFRESH_DEFAULT));
            feedback.updates = 0;
        }
        if (!menu_open) feedback.updates = 0;
        // 返回来源优先；普通请求仍按切页优先处理。
        // Return requests win; ordinary requests still prioritize page switches.
        // USB MSC 持有 TF 卡时禁止任何页面切换；必须先安全停止并重新挂载。
        // Never navigate while USB MSC owns the card; stop and remount it first.
        if (usb_storage_active()) {
            ctx.request_return = false;
            ctx.request_app = NULL;
            ctx.request_menu = false;
        }
        if (pico_ota_auto_pending() && !power_dialog_open && !menu_open && !quick_input_owned &&
            !app_quick_layer_is_open() && !touch.touched && !ui_click_feedback_active() &&
            !usb_storage_active() && !ble_pt_connected() && !current->holds_pmu && current != &app_transfer &&
            (current == &app_device_settings || app_navigation_main_page_ready(&ctx, current) ||
             (current == &app_book && app_book_reader_body_visible())) &&
            !ctx.request_return && !ctx.request_app && !ctx.request_menu &&
            app_device_settings_offer_update(current == &app_device_settings)) {
            pico_ota_auto_acknowledge();
            if (current == &app_device_settings) present_page(&ctx, current, &gesture, APP_REDRAW_PAGE);
            else {
                app_navigation_capture_reader_return(current);
                ctx.request_app = &app_device_settings;
            }
        }
        if (!power_dialog_open && (ctx.request_return || ctx.request_app || ctx.request_menu)) {
            if (ui_click_feedback_release(ctx.fb, &click_area))
                click_present(&ctx, current, click_area);
            app_quick_layer_reset();
            key_hold.key = -1;
            const app_desc_t* next = ctx.request_app;
            const bool go_back = ctx.request_return;
            app_input_cancel_gesture(&ctx, current, &gesture);
            menu_pressed = UI_MENU_HIT_NONE;
            ctx.request_app = NULL;
            ctx.request_menu = false;
            ctx.request_return = false;
            ctx.consumed = true;
            just_transitioned = true;
            (void)device_power_key_sampler_enable(false);
            app_navigation_apply_request(&ctx, &current, &menu_open, &menu_leaf,
                &navigation, (app_navigation_request_t){next, go_back, selected_from_menu},
                navigation_present_menu, &feedback);
        }
#if CONFIG_TOUCH_DIAG
        if (ctx.now_ms - td_stats_ms >= 60000) {
            td_stats_ms = ctx.now_ms;
            const app_input_diag_counts_t input_counts = app_input_diag_counts();
            TD_LOGI("STATS press=%u tap=%u swipe=%u long=%u cancel=%u err=%u kill=%u multi=%u stall=%u maxstall=%dms present>100ms=%u maxpresent=%dms replay_evt=%u replay_frames=%u",
                    input_counts.press, input_counts.tap, input_counts.swipe, input_counts.longp, input_counts.cancel, td_read_err, td_kill_err,
                    input_counts.multi, td_stall, td_max_stall_ms, td_present_over, td_max_present_ms,
                    td_replays, td_replay_frames);
        }
#endif
        // 连续列表相位期间缩短等待；每次仍读触摸，并给其他任务让出一个调度周期。
        // Reduce waits during continuous list phases; poll touch each time and yield one scheduling tick.
#if CONFIG_READER_PERF
        reader_perf_finish(false);
#endif
        vTaskDelay(pdMS_TO_TICKS(LOOP_TICK_MS));
    }
}
